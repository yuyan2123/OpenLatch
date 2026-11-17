#pragma once
#include <atomic>
#include <cstddef>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include "loggable_message.hpp"
#include "loggable_os.hpp"

namespace loggable {

/**
 * @brief Thread-safe ring buffer with "drop oldest" overflow policy.
 *
 * When buffer is full, push() overwrites the oldest entry and increments
 * the dropped counter. If a backend is provided, pop() blocks until data is
 * available using semaphore signaling. Without a backend, pop() returns
 * immediately if empty.
 *
 * The semaphore is only given on an empty -> non-empty transition, so a
 * burst of pushes wakes the consumer once, not once per element.
 *
 * Capacity is fixed at construction time (heap-allocated storage).
 */
class RingBuffer {
public:
    /**
     * @brief Construct a ring buffer.
     * @param capacity Maximum number of elements held. Clamped to >= 1.
     * @param backend Optional async backend for semaphore operations.
     *                If nullptr, blocking operations are disabled.
     */
    explicit RingBuffer(size_t capacity,
                        os::IAsyncBackend* backend = nullptr) noexcept
        : _buffer(capacity > 0 ? capacity : 1), _backend(backend) {
        if (_backend) {
            _sem = _backend->semaphore_create_binary();
        }
    }

    ~RingBuffer() {
        if (_backend && _sem) {
            _backend->semaphore_destroy(_sem);
        }
    }

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    /**
     * @brief Push an item, dropping oldest if full.
     * @param item Item to push (moved into buffer)
     * @return true if space was available, false if oldest was dropped
     */
    bool push(LogMessage item) noexcept {
        return push_impl(std::move(item));
    }

    /**
     * @brief Pop an item, blocking until available or timeout.
     * @param timeout_ms Timeout in milliseconds (WAIT_FOREVER for infinite)
     * @return Item if available within timeout, nullopt otherwise
     */
    std::optional<LogMessage> pop(uint32_t timeout_ms = os::WAIT_FOREVER) noexcept {
        // Wait for signal that data is available (if backend exists)
        if (_backend && _sem && !_backend->semaphore_take(_sem, timeout_ms)) {
            return std::nullopt;
        }

        std::lock_guard<std::mutex> lock(_mutex);

        if (_count == 0) {
            return std::nullopt;
        }

        LogMessage item = std::move(_buffer[_tail]);
        _tail = (_tail + 1) % _buffer.size();
        --_count;

        // Re-signal only while data remains so the next pop() succeeds
        if (_count > 0 && _backend && _sem) {
            _backend->semaphore_give(_sem);
        }

        return item;
    }

    /**
     * @brief Move up to max_count items out in one call.
     *
     * Waits for the first item like pop(), then drains everything available
     * without further blocking. Used by batch-consuming sink tasks.
     *
     * @param out Output vector; items are appended.
     * @param max_count Upper bound on items appended.
     * @param timeout_ms Timeout in ms for the first item only.
     * @return Number of items appended.
     */
    size_t pop_batch(std::vector<LogMessage>& out, size_t max_count,
                     uint32_t timeout_ms = os::WAIT_FOREVER) noexcept {
        if (max_count == 0) {
            return 0;
        }

        if (_backend && _sem && !_backend->semaphore_take(_sem, timeout_ms)) {
            return 0;
        }

        std::lock_guard<std::mutex> lock(_mutex);

        size_t n = _count < max_count ? _count : max_count;
        for (size_t i = 0; i < n; ++i) {
            out.emplace_back(std::move(_buffer[_tail]));
            _tail = (_tail + 1) % _buffer.size();
        }
        _count -= n;

        // Re-signal only while data remains so the next pop_batch() succeeds
        if (_count > 0 && _backend && _sem) {
            _backend->semaphore_give(_sem);
        }

        return n;
    }

    /**
     * @brief Check if buffer is empty.
     */
    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard<std::mutex> lock(_mutex);
        return _count == 0;
    }

    /**
     * @brief Get current number of items in buffer.
     */
    [[nodiscard]] size_t size() const noexcept {
        std::lock_guard<std::mutex> lock(_mutex);
        return _count;
    }

    /**
     * @brief Get total number of dropped messages since creation.
     */
    [[nodiscard]] size_t dropped_count() const noexcept {
        return _dropped_count.load(std::memory_order_relaxed);
    }

    /**
     * @brief Get buffer capacity.
     */
    [[nodiscard]] size_t capacity() const noexcept { return _buffer.size(); }

    /**
     * @brief Signal to unblock any waiting pop() calls.
     */
    void signal() noexcept {
        if (_backend && _sem) {
            _backend->semaphore_give(_sem);
        }
    }

private:
    bool push_impl(LogMessage item) noexcept {
        std::lock_guard<std::mutex> lock(_mutex);

        bool was_empty = (_count == 0);
        bool dropped = false;
        if (_count == _buffer.size()) {
            // Buffer full - drop oldest by advancing tail
            _tail = (_tail + 1) % _buffer.size();
            --_count;
            _dropped_count.fetch_add(1, std::memory_order_relaxed);
            dropped = true;
        }

        _buffer[_head] = std::move(item);
        _head = (_head + 1) % _buffer.size();
        ++_count;

        // Signal the consumer only on empty -> non-empty transition so a
        // burst wakes it once and it drains everything in one pass.
        if (was_empty && _backend && _sem) {
            _backend->semaphore_give(_sem);
        }
        return !dropped;
    }

    std::vector<LogMessage> _buffer;
    size_t _head{0};
    size_t _tail{0};
    size_t _count{0};
    mutable std::mutex _mutex;
    std::atomic<size_t> _dropped_count{0};

    os::IAsyncBackend* _backend{nullptr};
    os::SemaphoreHandle _sem{};
};

} // namespace loggable
