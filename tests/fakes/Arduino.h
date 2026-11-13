#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <cctype>
#include <type_traits>
class String : public std::string {
 public:
  using std::string::string;
  String(const std::string& value) : std::string(value) {}
  template<class T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
  explicit String(T value) : std::string(std::to_string(value)) {}
  void toLowerCase() { for (char& c : *this) c = char(std::tolower(static_cast<unsigned char>(c))); }
  bool endsWith(const char* suffix) const {
    const std::string tail(suffix);
    return size() >= tail.size() && compare(size() - tail.size(), tail.size(), tail) == 0;
  }
  void remove(size_t index) { erase(index); }
};
inline uint32_t fakeMillis = 0;
inline uint32_t millis() { return fakeMillis; }
struct FakeSerial {
  template<class... T> void printf(const char*, T...) {}
  void println(const char*) {}
};
inline FakeSerial Serial;
struct FakeEsp {
  uint64_t getEfuseMac() const { return 0xaabbcc112233ULL; }
};
inline FakeEsp ESP;
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
