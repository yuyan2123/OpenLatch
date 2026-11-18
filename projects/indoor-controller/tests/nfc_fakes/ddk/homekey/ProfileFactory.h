#pragma once
#include <NfcEnvironment.h>
#include <ddk/transport/NfcChannel.h>
inline bool fakeCredentialValid=true;
inline bool fakeProfileStalls=false;
inline std::function<void()> fakeVerificationHook;
namespace ddk::homekey {
struct Outcome{bool allowed;Issuer* issuer;Endpoint* endpoint;explicit operator bool()const{return allowed;}};
class Profile{
 public:
 FailureReason validate_select(Session&,const std::vector<uint8_t>&){return FailureReason::None;}
 FlowState step(Session& session,FlowState){++fakeProfileRuns;if(!session.channel->transceive({0x80,0x80,0,0}).ok())return FlowState::Failed;return fakeProfileStalls?FlowState::Selected:FlowState::Done;}
 Outcome finalize(Session& session){
   if(fakeVerificationHook)fakeVerificationHook();
   auto issuers=session.store.issuers();
   return {fakeCredentialValid,issuers.empty()?nullptr:&issuers[0],issuers.empty()||issuers[0].endpoints.empty()?nullptr:&issuers[0].endpoints[0]};
 }
};
inline std::unique_ptr<Profile> make_profile(){return std::make_unique<Profile>();}
}
