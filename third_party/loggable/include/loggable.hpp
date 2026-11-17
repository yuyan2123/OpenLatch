#pragma once

#include <atomic>
#include <chrono>
#include <fmt/core.h>
#include <fmt/format.h>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

#include "loggable_message.hpp"
#include "loggable_os.hpp"
#include "loggable_ringbuffer.hpp"

namespace loggable {

// Forward declarations
class Sinker;

/**
 * @brief Abstract interface for a log message sink.
 *
 * Any class that wants to receive log messages must implement this interface
 * and register itself with the central Sinker.
 */
class ISink {
public:
  virtual ~ISink() = default;

  /**
   * @brief Processes and outputs a log message.
   * @param message The log message to append.
   */
  virtual void consume(const LogMessage &message) = 0;

  /**
   * @brief Processes a batch of log messages.
   *
   * Sinks registered with async delivery receive batches of messages from
   * their dedicated consumer task. The default implementation loops over
   * consume(); override to amortize per-message work (e.g. serialize the
   * whole batch into one network frame).
   *
   * @param messages Array of log messages.
   * @param count Number of messages in the array.
   */
  virtual void consume_batch(const LogMessage *messages, size_t count) {
    for (size_t i = 0; i < count; ++i) {
      consume(messages[i]);
    }
  }
};

/**
 * @brief Metrics for monitoring the async logging system.
 */
struct SinkerMetrics {
  size_t dropped_count{0}; ///< Messages dropped due to full queue
  size_t queued_count{0};  ///< Messages currently in queue
  size_t capacity{0};      ///< Queue capacity
  bool is_running{false};  ///< Whether async dispatch is active
};

/**
 * @brief Metrics for a single registered sink.
 */
struct SinkMetrics {
  const char *name{nullptr};       ///< Sink name from SinkConfig
  bool delivery_sync{false};       ///< true if synchronous delivery
  size_t dropped_count{0};         ///< Messages dropped for this sink
  size_t queued_count{0};          ///< Messages currently queued (async only)
  size_t capacity{0};              ///< Queue capacity (async only)
};

/**
 * @brief Configuration for the async dispatch system.
 */
struct SinkerConfig {
  size_t task_stack_size = 4096;
  int task_priority = 10;
  int task_core = 1; ///< -1 = any core
};

/**
 * @brief Per-sink configuration for the dispatch system.
 */
struct SinkConfig {
  enum class Delivery {
    Sync,  ///< Deliver on the producer's task, immediately at dispatch() time
    Async, ///< Deliver from a dedicated consumer task via a per-sink queue
  };

  Delivery delivery = Delivery::Async;
  size_t queue_capacity = 128; ///< Async only: per-sink queue size
  size_t max_batch = 16;       ///< Async only: max messages per consume_batch call
  const char *name = "sink";   ///< Used for the consumer task name and metrics
  SinkerConfig task;           ///< Async only: consumer task parameters
};

/**
 * @brief The central hub for collecting and dispatching log messages.
 *
 * This class manages a list of sinkers and forwards each log message
 * to all registered sinkers, subject to level filtering.
 * It is implemented as a thread-safe singleton.
 *
 * Each sink chooses its delivery mode via SinkConfig:
 * - Sync: consume() is called inline on the producer's task. Delivery is
 *   immediate and cannot be delayed by other sinks, but the producer pays
 *   the sink's output cost.
 * - Async (default): the sink gets a dedicated queue and consumer task.
 *   Overflow drops the oldest queued message for that sink only, counted
 *   and reported to the sink as a synthetic "dropped N messages" notice.
 *
 * Call init() to enable async delivery (requires an OS backend). Without
 * init(), async sinks degrade to synchronous delivery.
 */
class Sinker {
public:
  Sinker(const Sinker &) = delete;
  Sinker &operator=(const Sinker &) = delete;
  ~Sinker() noexcept;

  /**
   * @brief Returns the singleton instance of the Sinker.
   */
  [[nodiscard]] static Sinker &instance() noexcept;

  /**
   * @brief Registers a new log sinker with default (async) configuration.
   *
   * @param sinker A shared pointer to an ISink implementation.
   * @return true if the sinker was registered.
   */
  bool add_sinker(std::shared_ptr<ISink> sinker) noexcept;

  /**
   * @brief Registers a new log sinker with explicit configuration.
   *
   * Async sinks get a dedicated queue and consumer task spawned immediately
   * (if a backend is registered); sync sinks are invoked inline at
   * dispatch() time.
   *
   * @param sinker A shared pointer to an ISink implementation.
   * @param config Delivery mode, queue size, task parameters.
   * @return true if the sinker was registered.
   */
  bool add_sinker(std::shared_ptr<ISink> sinker, const SinkConfig &config) noexcept;

  /**
   * @brief Unregisters a log sinker and stops its consumer task.
   * @param sinker The sinker to remove.
   */
  void remove_sinker(const std::shared_ptr<ISink> &sinker) noexcept;

  /**
   * @brief Sets the global minimum log level.
   * Messages with a lower severity will be discarded.
   * @param level The minimum level to allow.
   */
  void set_level(LogLevel level) noexcept;

  /**
   * @brief Gets the current global minimum log level.
   */
  [[nodiscard]] LogLevel get_level() const noexcept;

  /**
   * @brief Forwards a log message to all registered sinkers.
   *
   * Sync sinks are invoked inline; async sinks enqueue into their own
   * queue (never blocks, drops oldest on overflow).
   *
   * @param message The message to dispatch.
   */
  void dispatch(const LogMessage &message) noexcept;

  // --- Async API ---

  /**
   * @brief Initialize the async dispatch system.
   *
   * Must be called before registering async sinks if async behavior is
   * desired. Async sinks registered before init() start delivering
   * synchronously and are upgraded when init() succeeds. If not called
   * (or on platforms without OS support), all dispatch is synchronous.
   *
   * @param config Default task parameters for async sink consumer tasks.
   */
  void init(const SinkerConfig &config = {}) noexcept;

  /**
   * @brief Shutdown the async dispatch system.
   *
   * Flushes all queued messages to sinks before stopping.
   */
  void shutdown() noexcept;

  /**
   * @brief Flush all async sink queues synchronously.
   *
   * Blocks until every async queue is empty or timeout expires.
   *
   * @param timeout_ms Maximum time to wait in milliseconds.
   * @return true if all queues are empty, false if timeout expired.
   */
  [[nodiscard]] bool flush(uint32_t timeout_ms = 5000) noexcept;

  /**
   * @brief Check if async dispatch is running.
   */
  [[nodiscard]] bool is_running() const noexcept;

  /**
   * @brief Get aggregate metrics across all async sinks.
   */
  [[nodiscard]] SinkerMetrics get_metrics() const noexcept;

  /**
   * @brief Get per-sink metrics snapshot.
   */
  [[nodiscard]] std::vector<SinkMetrics> get_sink_metrics() const noexcept;

  /**
   * @brief Check whether the given task is one of the sink consumer tasks.
   *
   * Used by platform hooks to detect logging re-entering the dispatch
   * system from a sink callback running on its own consumer task.
   */
  [[nodiscard]] bool is_dispatcher_task(os::TaskHandle task) const noexcept;

private:
  Sinker() = default;

  // Opaque implementation detail; defined in loggable.cpp, never exposed
  // to users but accessible to the file-local helpers there.
  struct SinkEntry;
  friend struct SinkEntryAccess;

  bool _start_entry_task(SinkEntry &entry) noexcept;

  std::atomic<LogLevel> _global_level{LogLevel::Info};

  mutable std::mutex _sinkers_mutex;
  std::vector<std::shared_ptr<SinkEntry>> _sinkers;

  // Async infrastructure (shared defaults; per-sink state lives in SinkEntry)
  std::atomic<bool> _running{false};
  SinkerConfig _default_task_config{};

  static void _task_entry(void *arg) noexcept;
};

/**
 * @brief Lightweight logger that formats and dispatches log messages.
 *
 * Logger is a simple value type holding only a tag string. It can be
 * used standalone or as a member of Loggable-derived classes.
 */
class Logger {
public:
  explicit Logger(std::string_view tag) noexcept : _tag(tag) {}

  Logger(const Logger &) = default;
  Logger &operator=(const Logger &) = default;
  Logger(Logger &&) noexcept = default;
  Logger &operator=(Logger &&) noexcept = default;
  ~Logger() noexcept = default;

  /**
   * @brief Logs a pre-formatted message.
   * @param level The message's severity level.
   * @param message The message content.
   */
  void log(LogLevel level, std::string_view message) noexcept;

  /**
   * @brief Logs a fmt-style formatted message.
   * @param level The message's severity level.
   * @param format_str The fmt-style format string.
   * @param args Arguments for the format string.
   * @note Use the LOG macro to auto-prepend function name.
   */
  template <typename... Args>
  void logf(LogLevel level, fmt::format_string<Args...> format_str,
            Args &&...args) noexcept {
    if (!is_log_level_enabled(level, Sinker::instance().get_level())) {
      return;
    }
    fmt::memory_buffer buf;
    fmt::format_to(std::back_inserter(buf), format_str,
                   std::forward<Args>(args)...);
    log(level, std::string_view(buf.data(), buf.size()));
  }

private:
  std::string_view _tag;
};

/**
 * @brief Base class for any object that wishes to generate logs.
 *
 * Inheriting from this class provides a `logger()` method to access a
 * dedicated Logger instance.
 */
class Loggable {
public:
  explicit Loggable(std::string_view name) noexcept : _logger(name) {}
  Loggable(const Loggable &) = delete;
  Loggable &operator=(const Loggable &) = delete;
  Loggable(Loggable &&) noexcept = delete;
  Loggable &operator=(Loggable &&) noexcept = delete;
  virtual ~Loggable() = default;

  /**
   * @brief Returns a reference to the Logger instance for this object.
   */
  [[nodiscard]] Logger &logger() noexcept { return _logger; }

private:
  Logger _logger;
};

} // namespace loggable

/**
 * @brief Macro for logging with automatic function name prefix.
 * @param level The log level (e.g., LogLevel::Info).
 * @param format_str The fmt-style format string.
 * @param ... Arguments for the format string.
 *
 * Usage: LOGF(LogLevel::Info, "Hello, {}!", name);
 */
#if __cplusplus >= 202002L
#define LOG(level, format_str, ...)                                            \
  logger().logf(level, "{}: " format_str, __func__ __VA_OPT__(, ) __VA_ARGS__)
#else
// C++17 fallback: use ## to swallow the comma when __VA_ARGS__ is empty
#define LOG(level, format_str, ...)                                            \
  logger().logf(level, "{}: " format_str, __func__, ##__VA_ARGS__)
#endif
