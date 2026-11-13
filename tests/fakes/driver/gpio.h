#pragma once
using gpio_num_t = int;
constexpr int GPIO_MODE_OUTPUT = 1;
inline int fakeGpio = 0;
inline int fakeHighWrites = 0;
inline int gpio_set_level(gpio_num_t, int level) {
  fakeGpio = level;
  if (level) ++fakeHighWrites;
  return 0;
}
inline int gpio_set_direction(gpio_num_t, int) { return 0; }
