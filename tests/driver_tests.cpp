// Compile the actual firmware GPIO/timer adapter against controlled OS fakes.
// These tests exercise callback interleavings, not electrical characteristics.
#include <cstdlib>
#include <iostream>
#include "../firmware/DoorRelease/TimedRelay.h"
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << " failed: " #x "\n"; std::exit(1); } } while (0)
TimedRelay* current = nullptr;
int main() {
  TimedRelay relay;
  current = &relay;
  CHECK(relay.begin()); CHECK(fakeGpio == 0);
  CHECK(!relay.start(400));  // Default permission OFF.
  relay.permit(true);
  CHECK(!relay.start(0)); CHECK(!relay.start(801));
  CHECK(fakeGpio == 0);
  CHECK(relay.start(400)); CHECK(fakeTimer.armed);
  CHECK(fakeTimer.duration == 400000); CHECK(fakeGpio == 1);
  fakeExpire(); CHECK(!relay.active()); CHECK(fakeGpio == 0);

  CHECK(relay.start(400)); relay.permit(false);
  CHECK(!relay.active()); CHECK(fakeGpio == 0);
  CHECK(!relay.start(400));

  relay.permit(true); fakeStartFails = true;
  CHECK(!relay.start(400)); CHECK(fakeGpio == 0); CHECK(!relay.active());
  fakeStartFails = false;

  int writes = fakeHighWrites;
  fakeAfterArm = fakeExpire;  // Timer expires before start() regains CPU.
  CHECK(!relay.start(400)); CHECK(fakeGpio == 0);
  CHECK(fakeHighWrites == writes);

  fakeAfterArm = []() { current->permit(false); };  // Wi-Fi loss in same window.
  CHECK(!relay.start(400)); CHECK(fakeGpio == 0);
  CHECK(fakeHighWrites == writes);
  fakeAfterArm = nullptr;

  TimedRelay broken;
  fakeCreateFails = true;
  CHECK(!broken.begin()); broken.permit(true);
  CHECK(!broken.start(400)); CHECK(fakeGpio == 0);
  std::cout << "PASS: actual TimedRelay adapter, timer failure and callback interleavings.\n";
}
