#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace loggable {

/**
 * @brief Defines the verbosity level of a log message.
 */
enum class LogLevel : std::uint8_t {
  None,
  Error,
  Warning,
  Info,
  Debug,
  Verbose
};

// Constexpr functions for LogLevel operations
[[nodiscard]] constexpr const char *
log_level_to_string(LogLevel level) noexcept {
  switch (level) {
  case LogLevel::Error:
    return "E";
  case LogLevel::Warning:
    return "W";
  case LogLevel::Info:
    return "I";
  case LogLevel::Debug:
    return "D";
  case LogLevel::Verbose:
    return "V";
  case LogLevel::None:
    return "N";
  }
  // All enum values handled above; this is unreachable
  static_assert(static_cast<int>(LogLevel::Verbose) == 5,
                "LogLevel enum changed, update switch");
  return "UNKNOWN"; // Satisfy compiler return requirement
}

[[nodiscard]] constexpr bool
is_log_level_enabled(LogLevel message_level, LogLevel global_level) noexcept {
  return message_level <= global_level;
}

/**
 * @brief A structure representing a single log entry.
 *
 * Owns its data by using std::string, ensuring lifetime correctness.
 */
class LogMessage {
public:
  LogMessage() noexcept = default;

  LogMessage(std::chrono::system_clock::time_point timestamp, LogLevel level,
             std::string tag, std::string message) noexcept
      : _timestamp(timestamp), _level(level), _tag(std::move(tag)),
        _message(std::move(message)) {}

  LogMessage(const LogMessage &) = default;
  LogMessage &operator=(const LogMessage &) = default;
  LogMessage(LogMessage &&) noexcept = default;
  LogMessage &operator=(LogMessage &&) noexcept = default;
  ~LogMessage() = default;

  [[nodiscard]] std::chrono::system_clock::time_point
  get_timestamp() const noexcept {
    return _timestamp;
  }
  [[nodiscard]] LogLevel get_level() const noexcept { return _level; }
  [[nodiscard]] const std::string &get_tag() const noexcept { return _tag; }
  [[nodiscard]] const std::string &get_message() const noexcept {
    return _message;
  }

private:
  std::chrono::system_clock::time_point _timestamp{};
  LogLevel _level{LogLevel::None};
  std::string _tag;
  std::string _message;
};

} // namespace loggable
