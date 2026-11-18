// Compile the actual firmware GPIO/timer adapter against controlled OS fakes.
// These tests exercise callback interleavings, not electrical characteristics.
#include <cstdlib>
#include <iostream>
#include "../firmware/OpenLatch/TimedRelay.h"
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << " failed: " #x "\n"; std::exit(1); } } while (0)
TimedRelay* current = nullptr;
int main() {
  fakeGpioLatch = 1;  // Must preload OFF before enabling the pad.
  TimedRelay relay;
  current = &relay;
  CHECK(relay.begin()); CHECK(fakeGpio == 0);
  CHECK(fakeHighWrites == 0); CHECK(fakeGpioMuxSelected); CHECK(fakeGpioOutput);
  CHECK(fakeGpioConfig.pin_bit_mask == (uint64_t(1) << door::kRelayPin));
  CHECK(fakeGpioConfig.pull_up_en == GPIO_PULLUP_DISABLE);
  CHECK(fakeGpioConfig.pull_down_en == GPIO_PULLDOWN_ENABLE);
  CHECK(fakeGpioConfig.intr_type == GPIO_INTR_DISABLE);
  CHECK(fakeGpioDrive == GPIO_DRIVE_CAP_3);
  CHECK(!relay.start(400));  // Default permission OFF.
  relay.permit(true);
  CHECK(!relay.start(0)); CHECK(!relay.start(801));
  CHECK(fakeGpio == 0);
  CHECK(!relay.start(200, door::Source::Nfc));
  relay.permitNfc(true); CHECK(relay.start(200, door::Source::Nfc)); CHECK(fakeGpio == 1);
  relay.permit(false); CHECK(fakeGpio == 1); relay.permitNfc(false); CHECK(fakeGpio == 0);
  relay.permit(true);
  CHECK(door::kPulseMs == 200);
  CHECK(relay.start(door::kPulseMs)); CHECK(fakeTimer.armed);
  CHECK(fakeTimer.duration == 200000); CHECK(fakeGpio == 1);
  fakeExpire(); CHECK(!relay.active()); CHECK(fakeGpio == 0);
  for (uint32_t ms : {1u, 375u, 800u}) {
    CHECK(relay.start(ms)); CHECK(fakeTimer.duration == uint64_t(ms) * 1000);
    fakeExpire(); CHECK(!relay.active()); CHECK(!fakeGpio);
  }

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

  CHECK(!relay.start(400, door::Source::Infrared));
  relay.permitInfrared(true);
  CHECK(relay.start(400, door::Source::Infrared));
  relay.permit(false);  // Local pulses do not depend on network permission.
  CHECK(fakeGpio == 1); CHECK(relay.active());
  fakeExpire(); CHECK(fakeGpio == 0);
  CHECK(!relay.start(400));  // Local permission cannot authorize HomeKit.
  CHECK(relay.start(400, door::Source::Infrared));
  relay.permitInfrared(false); CHECK(fakeGpio == 0);
  CHECK(!relay.start(400, door::Source::Infrared));
  relay.permit(true);
  CHECK(relay.start(400));
  relay.permitInfrared(false); CHECK(fakeGpio == 1);
  relay.stop(); CHECK(fakeGpio == 0);
  relay.permitInfrared(true);
  fakeAfterArm = []() { current->permitInfrared(false); };
  CHECK(!relay.start(400, door::Source::Infrared)); CHECK(fakeGpio == 0);
  relay.permitInfrared(true);
  fakeAfterArm = []() { current->permit(false); };
  CHECK(relay.start(400, door::Source::Infrared)); CHECK(fakeGpio == 1);
  fakeExpire(); CHECK(fakeGpio == 0);
  fakeAfterArm = nullptr;

  TimedRelay noConfig;
  fakeGpioConfigFails = true;
  CHECK(!noConfig.begin()); noConfig.permit(true);
  CHECK(!noConfig.start(200)); CHECK(fakeGpio == 0);
  fakeGpioConfigFails = false;
  TimedRelay noLevel;
  fakeGpioLevelFails = true;
  CHECK(!noLevel.begin()); noLevel.permitInfrared(true);
  CHECK(!noLevel.start(200, door::Source::Infrared)); CHECK(fakeGpio == 0);
  fakeGpioLevelFails = false;
  TimedRelay noDrive;
  fakeGpioDriveFails = true;
  CHECK(!noDrive.begin()); noDrive.permit(true); noDrive.permitInfrared(true);
  CHECK(!noDrive.start(200)); CHECK(!noDrive.start(200, door::Source::Infrared));
  CHECK(fakeGpio == 0); fakeGpioDriveFails = false;

  TimedRelay broken;
  fakeCreateFails = true;
  CHECK(!broken.begin()); broken.permit(true);
  CHECK(!broken.start(400)); CHECK(fakeGpio == 0);
  std::cout << "PASS: actual TimedRelay adapter, GPIO mux/drive initialization, 200 ms timing, source permissions, timer failure and callback interleavings.\n";
}
