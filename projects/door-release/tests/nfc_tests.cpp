#include <cstdlib>
#include <iostream>
#include "../firmware/DoorRelease/NfcAttemptLimiter.h"
#include "../firmware/DoorRelease/IndoorSession.h"
#include "../firmware/DoorRelease/HomeKeyStore.h"
#include "ReaderSession.h"
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)
using Limiter = door::NfcAttemptLimiter;
using Verdict = Limiter::Verdict;

void limits_and_duplicates() {
  Limiter guard; CHECK(!guard.start(0)); guard.begin();
  uint64_t old = 0;
  for (uint32_t i = 0; i < 5; ++i) {
    const auto attempt = guard.start(i * 100); CHECK(attempt > old); CHECK(!guard.start(i * 100));
    CHECK(!guard.complete(i * 100, old, Verdict::Authorized));
    CHECK(!guard.complete(i * 100, attempt, Verdict::InvalidCredential));
    CHECK(guard.failures(i * 100) == i + 1);
    CHECK(!guard.complete(i * 100, attempt, Verdict::InvalidCredential));
    CHECK(guard.failures(i * 100) == i + 1); old = attempt;
  }
  CHECK(guard.locked(400)); CHECK(guard.remainingMs(400) == 60000);
  CHECK(!guard.start(60399)); CHECK(guard.remainingMs(60399) == 1);
  CHECK(!guard.complete(60399, old, Verdict::Authorized));
  CHECK(guard.remainingMs(60399) == 1);
  CHECK(guard.start(60400)); CHECK(!guard.locked(60400)); CHECK(guard.failures(60400) == 0);
}
void success_abort_and_expiry() {
  Limiter guard; guard.begin(2, 1);
  auto attempt = guard.start(0); CHECK(!guard.complete(0, attempt, Verdict::InvalidCredential));
  attempt = guard.start(1); CHECK(!guard.complete(1, attempt, Verdict::Aborted));
  CHECK(guard.failures(1) == 1);
  attempt = guard.start(2); CHECK(guard.complete(2, attempt, Verdict::Authorized));
  CHECK(guard.failures(2) == 0); CHECK(!guard.complete(2, attempt, Verdict::Authorized));
  attempt = guard.start(3); CHECK(!guard.complete(5003, attempt, Verdict::Authorized));
  CHECK(guard.failures(5003) == 0); CHECK(guard.start(5003) > attempt);
  guard.begin(1, 3600); attempt = guard.start(6000);
  CHECK(!guard.complete(6000, attempt, Verdict::InvalidCredential));
  CHECK(guard.remainingMs(6000) == 3600000); CHECK(!guard.start(3605999)); CHECK(guard.start(3606000));
}
void rollover_and_bad_configuration() {
  const uint32_t start = 0xfffffff0u;
  Limiter guard; guard.begin(1, 1); auto attempt = guard.start(start);
  CHECK(!guard.complete(start, attempt, Verdict::InvalidCredential));
  CHECK(guard.locked(start + 999)); CHECK(guard.remainingMs(start + 999) == 1);
  CHECK(!guard.locked(start + 1000)); CHECK(guard.start(start + 1000));
  for (const auto limit : {0u, 21u, 0xffffffffu}) {
    guard.begin(limit, 60); CHECK(!guard.ready()); CHECK(!guard.start(0));
  }
  for (const auto seconds : {0u, 3601u, 0xffffffffu}) {
    guard.begin(5, seconds); CHECK(!guard.ready()); CHECK(!guard.start(0));
  }
  guard.begin(20, 1); CHECK(guard.ready()); attempt = guard.start(0);
  guard.begin(20, 1); CHECK(!guard.complete(0, attempt, Verdict::Authorized));
  CHECK(guard.start(0) > attempt);
}
struct FakeReader : outdoor::Reader {
  int exchanges = 0;
  bool configure(const std::array<uint8_t,8>&) override { return true; }
  bool exchange(const uint8_t*, size_t, uint8_t* response, size_t& size) override {
    ++exchanges; response[0] = 0x90; response[1] = 0; size = 2; return true;
  }
  void release() override {}
};
struct Output : outdoor::Output {
  std::vector<outdoor::Frame> frames;
  void send(const outdoor::Frame& frame) override { frames.push_back(frame); }
  outdoor::Nonce randomNonce() override { outdoor::Nonce n{}; n[0] = 7; return n; }
};
void indoor_outdoor_transport() {
  FakeReader reader; Output output; outdoor::ReaderSession outside(reader, output);
  door::IndoorSession indoor;
  outside.begin(0); outside.hello(); outdoor::Nonce nonce{}; nonce[0] = 8;
  CHECK(indoor.configure(0, output.frames.back(), nonce, {}));
  outdoor::Frame request;
  CHECK(indoor.transmit(0, request)); outside.receive(0, request);
  indoor.receive(0, output.frames.back()); CHECK(indoor.bound());
  outside.presence(0, false); outside.presence(500, false);
  const uint8_t uid[]{1,2,3,4};
  CHECK(outside.detected(500, uid, 4, {}, 0x20)); const auto tag = output.frames.back();
  indoor.receive(500, tag); CHECK(indoor.takeTag(500)); indoor.receive(500, tag); CHECK(!indoor.takeTag(500));
  const uint8_t apdu[]{0,0xa4,4,0}; CHECK(indoor.exchange(500, apdu, 4));
  CHECK(indoor.transmit(500, request)); outside.receive(500, request);
  const auto response = output.frames.back(); CHECK(reader.exchanges == 1);
  CHECK(!indoor.transmit(1299, request)); CHECK(indoor.transmit(1300, request));
  outside.receive(1300, request); CHECK(reader.exchanges == 1);
  auto bad = response; bad.indoor[0] ^= 1; indoor.receive(1300, bad); CHECK(indoor.waiting());
  bad = response; bad.tap++; indoor.receive(1300, bad); CHECK(indoor.waiting());
  bad = response; bad.request++; indoor.receive(1300, bad); CHECK(indoor.waiting());
  indoor.receive(1300, response); CHECK(!indoor.waiting()); CHECK(indoor.takeResponse(bad)); CHECK(bad.length == 2);
  indoor.receive(1300, response); CHECK(!indoor.takeResponse(bad));
  CHECK(indoor.release(1300)); CHECK(indoor.transmit(1300, request)); outside.receive(1300, request);
  indoor.receive(1300, output.frames.back()); CHECK(!indoor.active());
  indoor.receive(1300, tag); CHECK(!indoor.takeTag(1300));
  CHECK(indoor.ping(2300)); CHECK(indoor.transmit(2300, request)); outside.receive(2300, request);
  // A newer Tag may arrive before an older cached Ping ACK.
  const auto ack = output.frames.back();
  outside.presence(2300, false); outside.presence(2800, false); CHECK(outside.detected(2800, uid, 4, {}, 0x20));
  indoor.receive(2800, output.frames.back()); CHECK(indoor.takeTag(2800));
  indoor.receive(2800, ack); CHECK(!indoor.waiting()); CHECK(indoor.exchange(2800, apdu, 4));
  indoor.tick(4400); CHECK(!indoor.bound()); CHECK(!indoor.active());
  CHECK(!indoor.exchange(4400, apdu, 4));
  indoor.receive(4400, response); CHECK(!indoor.takeResponse(bad));
}
void credential_storage() {
  nvsRecord.clear(); nvsPending.clear(); nvsWrites = 0;
  HomeKeyStore store; CHECK(store.begin()); CHECK(store.healthy()); CHECK(!store.provisioned());
  ddk::ReaderIdentity identity;
  identity.private_key.assign(32, 1); identity.public_key.assign(65, 4); identity.public_key_x.assign(32, 2);
  identity.group_identifier.assign(8, 3); identity.sub_identifier.assign(8, 4); store.provision_identity(identity);
  ddk::Issuer issuer; issuer.id.assign(8, 5); issuer.public_key.assign(32, 6);
  ddk::Endpoint endpoint; endpoint.id.assign(6, 7); endpoint.public_key.assign(65, 4);
  endpoint.public_key_x.assign(32, 8); endpoint.persistent_key.assign(32, 9);
  issuer.endpoints.push_back(endpoint); store.issuerList().push_back(issuer); store.save();
  CHECK(store.healthy()); CHECK(store.provisioned());
  const auto record = nvsRecord;
  HomeKeyStore reboot; CHECK(reboot.begin()); CHECK(reboot.provisioned());
  CHECK(reboot.reader_identity().private_key == identity.private_key);
  CHECK(reboot.issuers()[0].endpoints[0].persistent_key == endpoint.persistent_key);
  for (size_t i = 0; i < record.size(); ++i) {
    nvsRecord = record; nvsRecord[i] ^= 1;
    HomeKeyStore corrupt; CHECK(!corrupt.begin()); CHECK(!corrupt.healthy());
  }
  nvsRecord = record; nvsWriteFails = true; store.save(); CHECK(!store.healthy()); CHECK(!store.provisioned());
  nvsWriteFails = false; store.save(); CHECK(store.healthy());
  nvsCorruptReadback = true; store.save(); CHECK(!store.healthy()); nvsCorruptReadback = false;
  store.save(); CHECK(store.healthy()); store.clearAll(); CHECK(store.healthy()); CHECK(!store.provisioned());
  HomeKeyStore empty; CHECK(empty.begin()); CHECK(!empty.provisioned());
}
int main() {
  limits_and_duplicates(); success_abort_and_expiry(); rollover_and_bad_configuration();
  indoor_outdoor_transport(); credential_storage();
  std::cout << "PASS: indoor NFC limits, single-use outcomes, deadlines/rollover, paired transport/retries and credential NVS/readback/corruption.\n";
}
