#include "loggable.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/core.h>
#include <fmt/format.h>

#include "fmt/color.h"
#include "loggable_os.hpp"

namespace loggable {

// --- Sinker::SinkEntry Implementation ---

struct Sinker::SinkEntry {
    std::shared_ptr<ISink> sink;
    SinkConfig config;

    // Async state (valid when queue != nullptr)
    std::unique_ptr<RingBuffer> queue;
    // Cleared by the consumer task right before it deletes itself; lets
    // other threads wait for task teardown without touching the task.
    std::atomic<void *> task_handle{nullptr};
    std::atomic<bool> stop_requested{false};
    std::atomic<size_t> dropped_since_creation{0};
    size_t last_reported_dropped = 0; // Consumer-task only watermark

    [[nodiscard]] os::TaskHandle task() const noexcept {
        return os::TaskHandle{task_handle.load(std::memory_order_acquire)};
    }
    void set_task(os::TaskHandle t) noexcept {
        task_handle.store(t._handle, std::memory_order_release);
    }
};

// Helpers needing access to the private Sinker::SinkEntry; the friend
// declaration in Sinker names this type. Internal-linkage-free but only
// referenced from this TU.
struct SinkEntryAccess {
    static void deliver_batch(Sinker::SinkEntry &entry, const LogMessage *messages,
                              size_t count) noexcept {
        if (count == 0 || !entry.sink) {
            return;
        }
        entry.sink->consume_batch(messages, count);
    }

    static void deliver_drop_notice(Sinker::SinkEntry &entry) noexcept {
        const size_t dropped_total =
            entry.dropped_since_creation.load(std::memory_order_relaxed);
        if (dropped_total <= entry.last_reported_dropped) {
            return;
        }
        const size_t newly_dropped = dropped_total - entry.last_reported_dropped;
        entry.last_reported_dropped = dropped_total;

        auto *backend = os::get_backend();
        std::chrono::system_clock::time_point now =
            backend ? std::chrono::system_clock::time_point(
                          std::chrono::milliseconds(backend->get_time_ms()))
                    : std::chrono::system_clock::now();
        LogMessage notice{now, LogLevel::Warning, "Loggable::Sinker",
                          fmt::format("Dropped {} log messages for this sink",
                                      newly_dropped)};
        entry.sink->consume(notice);
    }
};

// --- Sinker Implementation ---

Sinker &Sinker::instance() noexcept {
    static Sinker inst;
    return inst;
}

Sinker::~Sinker() noexcept {
    // Entries are shared_ptr-owned; consumer tasks stop via shutdown() or
    // remove_sinker(). The singleton lives for the whole program in
    // practice, so no teardown happens here.
}

bool Sinker::add_sinker(std::shared_ptr<ISink> sinker) noexcept {
    return add_sinker(std::move(sinker), SinkConfig{});
}

bool Sinker::add_sinker(std::shared_ptr<ISink> sinker,
                        const SinkConfig &config) noexcept {
    if (!sinker) {
        return false;
    }

    auto entry = std::make_shared<SinkEntry>();
    entry->sink = sinker;
    entry->config = config;
    if (entry->config.queue_capacity == 0) {
        entry->config.queue_capacity = 1;
    }
    if (entry->config.max_batch == 0) {
        entry->config.max_batch = 1;
    }

    std::lock_guard<std::mutex> lock(_sinkers_mutex);
    if (_running.load(std::memory_order_acquire) &&
        entry->config.delivery == SinkConfig::Delivery::Async) {
        _start_entry_task(*entry);
    }
    _sinkers.push_back(std::move(entry));
    return true;
}

void Sinker::remove_sinker(const std::shared_ptr<ISink> &sinker) noexcept {
    if (!sinker) {
        return;
    }

    std::shared_ptr<SinkEntry> to_stop;
    {
        std::lock_guard<std::mutex> lock(_sinkers_mutex);
        for (auto it = _sinkers.begin(); it != _sinkers.end(); ++it) {
            if (*it && (*it)->sink == sinker) {
                to_stop = *it;
                _sinkers.erase(it);
                break;
            }
        }
    }

    if (!to_stop) {
        return;
    }

    auto *backend = os::get_backend();
    if (backend && to_stop->queue) {
        to_stop->stop_requested.store(true, std::memory_order_release);
        to_stop->queue->signal();
        // Wait for the consumer to clear its task handle, then give the
        // task a moment to actually finish self-deletion before the last
        // reference (and the entry with it) goes away.
        while (to_stop->task_handle.load(std::memory_order_acquire)) {
            backend->delay_ms(1);
        }
        backend->delay_ms(2);
    }
    // Any messages still queued for a removed sink are dropped by design.
}

void Sinker::set_level(LogLevel level) noexcept {
    _global_level.store(level, std::memory_order_release);
}

LogLevel Sinker::get_level() const noexcept {
    return _global_level.load(std::memory_order_acquire);
}

void Sinker::dispatch(const LogMessage &message) noexcept {
    static thread_local bool is_dispatching = false;
    if (is_dispatching) {
        return;
    }
    is_dispatching = true;
    struct DispatchGuard {
        ~DispatchGuard() { is_dispatching = false; }
    } guard;

    // Snapshot the sink list without allocating in the common case so sink
    // callbacks may register/unregister sinks (or log) re-entrantly.
    constexpr size_t kInlineSlots = 8;
    std::array<std::shared_ptr<SinkEntry>, kInlineSlots> inline_snapshot;
    std::vector<std::shared_ptr<SinkEntry>> heap_snapshot;
    size_t count = 0;
    {
        std::lock_guard<std::mutex> lock(_sinkers_mutex);
        if (_sinkers.size() <= kInlineSlots) {
            for (const auto &entry : _sinkers) {
                inline_snapshot[count++] = entry;
            }
        } else {
            heap_snapshot = _sinkers;
        }
    }

    const auto process = [&](const std::shared_ptr<SinkEntry> &entry) {
        if (!entry || !entry->sink) [[unlikely]] {
            return;
        }
        if (entry->config.delivery == SinkConfig::Delivery::Sync) {
            // Synchronous sink: deliver immediately on the producer's task.
            entry->sink->consume(message);
        } else if (entry->queue) {
            // Async sink with active queue: enqueue without ever blocking.
            if (!entry->queue->push(message)) {
                entry->dropped_since_creation.fetch_add(1,
                                                        std::memory_order_relaxed);
            }
        } else {
            // Async sink before init(): degrade to synchronous delivery.
            entry->sink->consume(message);
        }
    };

    if (count > 0) {
        for (size_t i = 0; i < count; ++i) {
            process(inline_snapshot[i]);
        }
    } else {
        for (const auto &entry : heap_snapshot) {
            process(entry);
        }
    }
}

void Sinker::init(const SinkerConfig &config) noexcept {
    auto *backend = os::get_backend();
    if (!backend) {
        // No backend registered - stay in sync mode
        return;
    }

    bool expected = false;
    if (!_running.compare_exchange_strong(expected, true,
                                          std::memory_order_acq_rel)) {
        return; // Already running
    }

    _default_task_config = config;

    // Upgrade any async sinks registered before init() to real queues/tasks.
    std::lock_guard<std::mutex> lock(_sinkers_mutex);
    for (auto &entry : _sinkers) {
        if (entry && entry->config.delivery == SinkConfig::Delivery::Async &&
            !entry->queue) {
            _start_entry_task(*entry);
        }
    }
}

void Sinker::shutdown() noexcept {
    auto *backend = os::get_backend();
    if (!backend || !_running.load(std::memory_order_acquire)) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(_sinkers_mutex);
        for (auto &entry : _sinkers) {
            if (entry && entry->queue) {
                entry->stop_requested.store(true, std::memory_order_release);
                entry->queue->signal();
            }
        }
    }

    (void)flush(5000);

    // Consumer tasks delete themselves once they observe the stop flag with
    // an empty queue; wait for all handles to clear.
    for (;;) {
        bool all_stopped = true;
        {
            std::lock_guard<std::mutex> lock(_sinkers_mutex);
            for (auto &entry : _sinkers) {
                if (entry && entry->task_handle.load(std::memory_order_acquire)) {
                    all_stopped = false;
                    break;
                }
            }
        }
        if (all_stopped) {
            break;
        }
        backend->delay_ms(10);
    }

    // Tear down the per-sink queues so a later init() re-creates tasks for
    // every async sink (init() only starts entries without a queue).
    {
        std::lock_guard<std::mutex> lock(_sinkers_mutex);
        for (auto &entry : _sinkers) {
            if (entry) {
                entry->queue.reset();
            }
        }
    }

    _running.store(false, std::memory_order_release);
}

bool Sinker::flush(uint32_t timeout_ms) noexcept {
    if (!_running.load(std::memory_order_acquire)) {
        return true;
    }

    auto *backend = os::get_backend();

    constexpr uint32_t poll_interval = 10;
    uint32_t elapsed = 0;

    for (;;) {
        bool all_empty = true;
        {
            std::lock_guard<std::mutex> lock(_sinkers_mutex);
            for (auto &entry : _sinkers) {
                if (entry && entry->queue && !entry->queue->empty()) {
                    all_empty = false;
                    break;
                }
            }
        }
        if (all_empty) {
            return true;
        }
        if (timeout_ms > 0 && elapsed >= timeout_ms) {
            return false;
        }
        if (backend) {
            backend->delay_ms(poll_interval);
        }
        elapsed += poll_interval;
    }
}

bool Sinker::is_running() const noexcept {
    return _running.load(std::memory_order_acquire);
}

SinkerMetrics Sinker::get_metrics() const noexcept {
    size_t dropped = 0;
    size_t queued = 0;
    size_t capacity = 0;
    {
        std::lock_guard<std::mutex> lock(_sinkers_mutex);
        for (const auto &entry : _sinkers) {
            if (entry && entry->queue) {
                dropped += entry->queue->dropped_count();
                queued += entry->queue->size();
                capacity += entry->queue->capacity();
            }
        }
    }
    return SinkerMetrics{
        .dropped_count = dropped,
        .queued_count = queued,
        .capacity = capacity,
        .is_running = _running.load(std::memory_order_acquire)};
}

std::vector<SinkMetrics> Sinker::get_sink_metrics() const noexcept {
    std::vector<SinkMetrics> out;
    std::lock_guard<std::mutex> lock(_sinkers_mutex);
    out.reserve(_sinkers.size());
    for (const auto &entry : _sinkers) {
        if (!entry) {
            continue;
        }
        out.push_back(SinkMetrics{
            .name = entry->config.name,
            .delivery_sync = entry->config.delivery == SinkConfig::Delivery::Sync,
            .dropped_count = entry->queue ? entry->queue->dropped_count() : 0,
            .queued_count = entry->queue ? entry->queue->size() : 0,
            .capacity = entry->queue ? entry->queue->capacity() : 0,
        });
    }
    return out;
}

bool Sinker::is_dispatcher_task(os::TaskHandle task) const noexcept {
    if (!task) {
        return false;
    }
    std::lock_guard<std::mutex> lock(_sinkers_mutex);
    for (const auto &entry : _sinkers) {
        if (entry &&
            entry->task_handle.load(std::memory_order_acquire) == task._handle) {
            return true;
        }
    }
    return false;
}

bool Sinker::_start_entry_task(SinkEntry &entry) noexcept {
    auto *backend = os::get_backend();
    if (!backend) {
        return false;
    }

    entry.queue = std::make_unique<RingBuffer>(entry.config.queue_capacity, backend);
    // Clear stop BEFORE task_create: the task starts running the moment it
    // is created and must not observe a stale stop flag from a previous
    // shutdown.
    entry.stop_requested.store(false, std::memory_order_release);
    entry.last_reported_dropped = 0;

    const SinkerConfig &defaults = _default_task_config;
    const SinkerConfig &task = entry.config.task;
    os::TaskConfig task_cfg{
        .name = entry.config.name,
        .stack_size = task.task_stack_size ? task.task_stack_size
                                           : defaults.task_stack_size,
        .priority = task.task_priority ? task.task_priority
                                       : defaults.task_priority,
        .core = task.task_core >= 0 ? task.task_core : defaults.task_core};

    os::TaskHandle handle = backend->task_create(task_cfg, &Sinker::_task_entry, &entry);
    if (!handle) {
        entry.queue.reset();
        return false;
    }
    entry.set_task(handle);
    return true;
}

void Sinker::_task_entry(void *arg) noexcept {
    auto *entry = static_cast<SinkEntry *>(arg);
    auto *backend = os::get_backend();

    std::vector<LogMessage> batch;
    batch.reserve(entry->config.max_batch);

    while (!entry->stop_requested.load(std::memory_order_acquire)) {
        batch.clear();
        entry->queue->pop_batch(batch, entry->config.max_batch, 100);

        if (batch.empty()) {
            continue;
        }

        // Coalesced drop notice: anything dropped since the last drain is
        // reported as one synthetic message delivered to this sink only.
        SinkEntryAccess::deliver_drop_notice(*entry);
        SinkEntryAccess::deliver_batch(*entry, batch.data(), batch.size());
    }

    // Drain remaining on stop, including a final drop notice.
    for (;;) {
        batch.clear();
        if (entry->queue->pop_batch(batch, entry->config.max_batch, 0) == 0) {
            break;
        }
        SinkEntryAccess::deliver_drop_notice(*entry);
        SinkEntryAccess::deliver_batch(*entry, batch.data(), batch.size());
    }

    entry->task_handle.store(nullptr, std::memory_order_release);
    if (backend) {
        backend->task_delete(os::TaskHandle{});
    }
}

// --- Logger Implementation ---

void Logger::log(LogLevel level, std::string_view message) noexcept {
    if (!is_log_level_enabled(level, Sinker::instance().get_level())) {
        return;
    }
    auto now = std::chrono::system_clock::now();
    auto *backend = os::get_backend();
    if (backend) {
        now = std::chrono::system_clock::time_point(std::chrono::milliseconds(backend->get_time_ms()));
    }
    LogMessage msg(now, level, std::string(_tag), std::string(message));
    Sinker::instance().dispatch(msg);
}

} // namespace loggable
