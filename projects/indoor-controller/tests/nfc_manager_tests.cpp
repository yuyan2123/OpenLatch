// Actual manager/services and paired transport, with host radio/task/crypto adapters.
// The profile substitute controls outcomes; this is not an Apple cryptography test.
#include <cstdlib>
#include <iostream>
#include <future>
#include <string>
#include <thread>
#include "../firmware/OpenLatch/HomeKeyManager.h"
#include "../firmware/OpenLatch/HomeKeyServices.h"
#include "../firmware/OpenLatch/TimedRelay.h"
#include "ReaderSession.h"
#define CHECK(x) do { if (!(x)) { std::cerr << __func__ << ':' << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)
struct TestAuth : outdoor::Auth {
  bool sign(const uint8_t* bytes,size_t size,uint8_t* output) override {
    uint8_t hash[32]{}; mbedtls_md_hmac(nullptr,nullptr,0,bytes,size,hash); std::copy_n(hash,16,output); return true;
  }
};
struct TestReader : outdoor::Reader {
  int exchanges = 0; bool fail = false;
  bool configure(const std::array<uint8_t,8>&) override { return true; }
  bool exchange(const uint8_t*,size_t,uint8_t* response,size_t& size) override {
    ++exchanges; if(fail)return false; response[0]=0x90;response[1]=0;size=2;return true;
  }
  void release() override {}
};
struct TestOutput : outdoor::Output {
  TestAuth auth; outdoor::Codec codec{auth,credentials::indoorMac,credentials::outdoorMac};
  bool overWifi = false;
  const outdoor::LanPeer lanPeer{1};
  int frames = 0; outdoor::Frame last{};
  void send(const outdoor::Frame& frame) override {
    ++frames; last=frame; outdoor::Wire wire; CHECK(codec.encode(frame,false,wire));
    if (overWifi) {
      outdoor::fakeLanIncoming.emplace_back(wire, lanPeer);
      return;
    }
    esp_now_recv_info_t info{credentials::outdoorMac.data(),credentials::indoorMac.data()};
    if(fakeReceive)fakeReceive(&info,wire.bytes.data(),int(wire.length));
  }
  outdoor::Nonce randomNonce() override { outdoor::Nonce nonce{};nonce[0]=uint8_t(esp_random());return nonce; }
};
struct Fixture {
  HomeKeyStore store; door::NfcAttemptLimiter attempts; HomeKeyManager manager{store,attempts};
  TestReader reader; TestOutput output; outdoor::ReaderSession outside{reader,output};
  explicit Fixture(bool preferWifi = false, bool provisionHomeKey = true) {
    fakeMillis=0;WiFi={};WiFi.connected=true;fakeQueues.clear();fakeReceive=nullptr;
    fakeQueueFailure=fakeTaskFailure=fakeRadioFailure=fakeSendFailure=fakeShaFailure=false;
    fakeRadioStarts=fakeProfileRuns=0;fakeCredentialValid=true;fakeProfileStalls=false;fakeDelayHook={};fakeVerificationHook={};fakeProvisionHook={};
    fakeProvisionReply={1,1,0};fakeStationMac=credentials::indoorMac;
    outdoor::fakeLanIncoming.clear();outdoor::fakeLanSend={};
    nvsRecord.clear();nvsPending.clear();nvsWrites=0;
    nvsOpenFails=nvsReadFails=nvsWriteFails=nvsCommitFails=nvsCorruptReadback=false;
    attempts.begin();CHECK(manager.begin());
    ddk::ReaderIdentity identity;identity.private_key.assign(32,1);identity.public_key.assign(65,4);
    identity.public_key_x.assign(32,2);identity.group_identifier.assign(8,3);identity.sub_identifier.assign(8,4);
    store.provision_identity(identity);
    homeSpan.controllers.clear();FakeHapController controller;controller.key.fill(6);homeSpan.controllers.push_back(controller);
    std::vector<uint8_t> input{'k','e','y','-','i','d','e','n','t','i','f','i','e','r'};
    input.insert(input.end(),controller.key.begin(),controller.key.end());uint8_t hash[32]{};mbedtls_sha256(input.data(),input.size(),hash,0);
    ddk::Issuer issuer;issuer.id.assign(hash,hash+8);issuer.public_key.assign(32,6);
    ddk::Endpoint endpoint;endpoint.id.assign(6,7);endpoint.public_key.assign(65,4);
    endpoint.public_key_x.assign(32,8);endpoint.persistent_key.assign(32,9);issuer.endpoints.push_back(endpoint);
    if (provisionHomeKey) { store.issuerList().push_back(issuer);store.save();CHECK(store.provisioned()); }
    manager.enabled(true);
    fakeRadioSend=[this](const uint8_t* bytes,size_t size){outdoor::Frame frame;CHECK(output.codec.decode(bytes,size,true,frame));outside.receive(fakeMillis,frame);};
    outside.begin(0);CHECK(manager.poll()>0);
    if (preferWifi) {
      outdoor::fakeLanSend = [this](const outdoor::Wire& wire, outdoor::LanPeer peer) {
        CHECK(peer == output.lanPeer);
        outdoor::Frame frame;
        CHECK(output.codec.decode(wire.bytes.data(), wire.length, true, frame));
        outside.receive(fakeMillis, frame);
      };
      outside.hello(); // An ESP-NOW discovery packet is already queued.
      output.overWifi = true;
    }
    outside.hello();manager.poll();manager.poll();CHECK(manager.connected());
    homeKeyControllersPending=false;
  }
  void tap(uint8_t sak = 0x20, uint8_t firstByte = 1) {
    manager.poll();manager.poll();
    outside.presence(fakeMillis,false);fakeMillis+=500;outside.presence(fakeMillis,false);
    const uint8_t uid[]{firstByte,2,3,4};CHECK(outside.detected(fakeMillis,uid,sizeof(uid),{},sak));manager.poll();manager.poll();
  }
};
void verified_results_are_single_use_and_expire() {
  {Fixture f;f.tap();CHECK(fakeProfileRuns==1);CHECK(f.reader.exchanges==2);CHECK(f.manager.takeVerified(fakeMillis));CHECK(!f.manager.takeVerified(fakeMillis));}
  {Fixture f;f.tap();CHECK(!f.manager.takeVerified(fakeMillis+250));CHECK(!f.manager.takeVerified(fakeMillis));}
  {Fixture f;f.tap();CHECK(f.manager.takeVerified(fakeMillis+249));}
  {Fixture f;f.tap();f.manager.enabled(false);f.manager.enabled(true);CHECK(!f.manager.takeVerified(fakeMillis));}
  {Fixture f;f.tap();f.store.save();CHECK(!f.manager.takeVerified(fakeMillis));}
  {Fixture f;f.tap();WiFi.connected=false;CHECK(!f.manager.takeVerified(fakeMillis));f.manager.poll();CHECK(!f.manager.connected());}
  {Fixture f;f.tap();f.tap();CHECK(!f.manager.takeVerified(fakeMillis));f.tap();CHECK(f.manager.takeVerified(fakeMillis));}
}
void wifi_takes_priority_and_completes_one_tap() {
  Fixture f(true);
  CHECK(std::string(f.manager.transport()) == "wifi");
  f.tap();
  CHECK(f.reader.exchanges == 2);
  CHECK(f.manager.takeVerified(fakeMillis));
  CHECK(!f.manager.takeVerified(fakeMillis));
  const outdoor::Frame lastWifiFrame = f.output.last;
  f.output.overWifi = false;
  f.output.send(lastWifiFrame);
  f.manager.poll();
  CHECK(f.manager.connected());
  CHECK(!f.manager.takeVerified(fakeMillis));

  fakeMillis += door::IndoorSession::kLeaseMs;
  outdoor::fakeLanIncoming.clear();
  f.manager.poll();
  CHECK(!f.manager.connected());
  f.outside.begin(fakeMillis);
  f.outside.hello();
  f.manager.poll();
  f.manager.poll();
  CHECK(std::string(f.manager.transport()) == "esp-now");
}
void invalid_and_aborted_attempts_are_separate() {
  {Fixture f;fakeCredentialValid=false;f.tap();CHECK(!f.manager.takeVerified(fakeMillis));CHECK(f.attempts.failures(fakeMillis)==1);
    f.output.send(f.output.last);f.manager.poll();CHECK(f.attempts.failures(fakeMillis)==1);}
  {Fixture f;f.reader.fail=true;f.tap();CHECK(!f.manager.takeVerified(fakeMillis));CHECK(f.attempts.failures(fakeMillis)==0);}
  {Fixture f;fakeVerificationHook=[&]{f.manager.enabled(false);};f.tap();CHECK(!f.manager.takeVerified(fakeMillis));CHECK(f.attempts.failures(fakeMillis)==0);}
  {Fixture f;fakeDelayHook=[&]{f.manager.enabled(false);};f.tap();CHECK(!f.manager.takeVerified(fakeMillis));CHECK(f.attempts.failures(fakeMillis)==0);}
  {Fixture f;fakeVerificationHook=[&]{nvsWriteFails=true;f.store.save();};f.tap();CHECK(!f.manager.takeVerified(fakeMillis));CHECK(f.attempts.failures(fakeMillis)==0);}
  {Fixture f;fakeProfileStalls=true;f.tap();CHECK(fakeProfileRuns==8);CHECK(!f.manager.takeVerified(fakeMillis));}
  {Fixture f;f.attempts.begin(1,60);fakeCredentialValid=false;f.tap();CHECK(f.attempts.locked(fakeMillis));
    const int runs=fakeProfileRuns;f.tap();CHECK(fakeProfileRuns==runs);CHECK(f.attempts.failures(fakeMillis)==1);}
}
void tag_before_ping_ack_is_not_discarded() {
  Fixture f;fakeMillis=1000;f.manager.poll();
  f.outside.presence(fakeMillis,false);fakeMillis+=500;f.outside.presence(fakeMillis,false);
  const uint8_t uid[]{1,2,3,4};CHECK(f.outside.detected(fakeMillis,uid,sizeof(uid),{},0x20));
  f.manager.poll();CHECK(fakeProfileRuns==0);
  f.manager.poll();CHECK(fakeProfileRuns==1);CHECK(f.manager.takeVerified(fakeMillis));
}
void physical_fobs_use_fresh_paired_taps_without_home_key() {
  Fixture f(true, false);
  door::RfidUid uid;
  CHECK(!f.manager.provisioned());
  f.manager.fobAdmission(1, true);
  f.tap(0x08);
  CHECK(f.manager.takeFob(fakeMillis, uid)); CHECK(uid.length == 4 && uid.bytes[0] == 1);
  CHECK(!f.manager.takeFob(fakeMillis, uid));
  f.tap(0x08); CHECK(!f.manager.takeFob(fakeMillis + 250, uid));
  f.tap(0x08); f.manager.fobAdmission(2, true); CHECK(!f.manager.takeFob(fakeMillis, uid));
  f.tap(0x08, 0x08); CHECK(!f.manager.takeFob(fakeMillis, uid)); // Random UID.
  f.tap(0x20); CHECK(!f.manager.takeFob(fakeMillis, uid)); // ISO-DEP target.
  CHECK(!f.manager.takeVerified(fakeMillis)); CHECK(f.reader.exchanges == 0);
  CHECK(f.attempts.failures(fakeMillis) == 0);
}
void pairing_changes_revoke_pending_results() {
  Fixture f;f.tap();const uint32_t generation=f.store.generation();
  homeKeyControllersChanged();syncHomeKeyControllers(f.store);CHECK(f.store.generation()==generation);CHECK(f.store.issuers()[0].endpoints.size()==1);
  homeSpan.controllers.clear();homeKeyControllersChanged();
  std::promise<void> locked,release;auto released=release.get_future();
  std::thread owner([&]{std::unique_lock<std::mutex> lock(f.store.mutex);locked.set_value();released.wait();});
  locked.get_future().wait();syncHomeKeyControllers(f.store);CHECK(homeKeyControllersPending);
  release.set_value();owner.join();syncHomeKeyControllers(f.store);
  CHECK(!homeKeyControllersPending);CHECK(!f.store.provisioned());CHECK(!f.manager.takeVerified(fakeMillis));
  f.manager.poll();CHECK(!f.manager.connected());
}
void provisioning_is_bounded_and_storage_failure_is_closed() {
  Fixture f;HomeKeyAccessService access(f.store);access.point->pending=true;
  CHECK(!access.update());access.point->input.assign(2049,1);CHECK(!access.update());
  access.point->input={1,1,0};CHECK(access.update());CHECK(!access.point->response.empty());
  access.point->pending=true;fakeProvisionHook=[](ddk::CredentialStore& store){store.save();};nvsWriteFails=true;
  CHECK(!access.update());CHECK(!f.store.healthy());CHECK(!f.manager.takeVerified(fakeMillis));
}
void reader_key_query_before_provisioning_returns_empty_tlv() {
  Fixture f;
  HomeKeyAccessService access(f.store);
  const std::vector<uint8_t> readReaderKey{0x01, 0x01, 0x01, 0x06, 0x00};
  access.point->input = readReaderKey;
  access.point->pending = true;
  CHECK(access.update());
  CHECK(access.point->response == fakeProvisionReply);

  f.store.clearIdentity();
  const auto writesBeforeQuery = nvsWrites;
  fakeProvisionReply.clear();
  access.point->pending = true;
  CHECK(access.update());
  CHECK(access.point->response.empty());
  CHECK(!f.store.provisioned());
  CHECK(nvsWrites == writesBeforeQuery);

  access.point->pending = true;
  access.point->input = {0x01, 0x01, 0x02, 0x06, 0x00};
  CHECK(!access.update());
  access.point->input = {0x01, 0x01, 0x01};
  CHECK(!access.update());
}
void initialization_and_traffic_failures() {
  {Fixture f;fakeSendFailure=true;fakeMillis+=1000;f.manager.poll();f.manager.poll();fakeMillis+=1600;f.manager.poll();CHECK(!f.manager.connected());}
  {Fixture f;outdoor::Wire wire;CHECK(f.output.codec.encode(f.output.last,false,wire));
    auto wrong=credentials::outdoorMac;wrong[5]^=1;esp_now_recv_info_t info{wrong.data(),credentials::indoorMac.data()};
    const size_t queued=fakeQueues.front()->items.size();fakeReceive(&info,wire.bytes.data(),int(wire.length));CHECK(fakeQueues.front()->items.size()==queued);
    info.src_addr=credentials::outdoorMac.data();for(int i=0;i<20;++i)fakeReceive(&info,wire.bytes.data(),int(wire.length));CHECK(fakeQueues.front()->items.size()==8);
    f.manager.poll();CHECK(fakeQueues.front()->items.size()<=4);}
  fakeQueues.clear();fakeReceive=nullptr;nvsRecord.clear();nvsPending.clear();nvsWriteFails=false;
  HomeKeyStore store;door::NfcAttemptLimiter attempts;attempts.begin();
  {fakeQueueFailure=true;HomeKeyManager manager(store,attempts);CHECK(!manager.begin());CHECK(!manager.configured());fakeQueueFailure=false;}
  {fakeTaskFailure=true;HomeKeyManager manager(store,attempts);CHECK(!manager.begin());CHECK(!manager.configured());fakeTaskFailure=false;}
  {HomeKeyManager manager(store,attempts);CHECK(manager.begin());fakeRadioFailure=true;CHECK(manager.poll()==0);CHECK(!manager.configured());fakeRadioFailure=false;}
}
int main() {
  verified_results_are_single_use_and_expire();wifi_takes_priority_and_completes_one_tap();invalid_and_aborted_attempts_are_separate();tag_before_ping_ack_is_not_discarded();pairing_changes_revoke_pending_results();
  provisioning_is_bounded_and_storage_failure_is_closed();reader_key_query_before_provisioning_returns_empty_tlv();initialization_and_traffic_failures();
  physical_fobs_use_fresh_paired_taps_without_home_key();
  std::cout<<"PASS: actual NFC manager/services, paired APDU transport, result expiry/revocation, limits, provisioning and radio/queue/task failures.\n";
}
