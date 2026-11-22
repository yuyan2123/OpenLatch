#pragma once
#include <ddk/store/CredentialStore.h>
#include <functional>
#include <memory>
#include <vector>
inline int fakeHomeKeySessions=0;
namespace ddk {
using ApduCommand=std::vector<uint8_t>;
struct ApduResponse{std::vector<uint8_t> data;bool accepted=false;bool ok()const{return accepted;}};
class NfcChannel{
 public:
 using Callback=std::function<bool(std::vector<uint8_t>&,std::vector<uint8_t>&)>;
 explicit NfcChannel(Callback callback):callback_(std::move(callback)){}
 virtual ~NfcChannel()=default;
 virtual size_t max_command_payload()const{return 246;}
 ApduResponse transceive(std::vector<uint8_t> command){
   std::vector<uint8_t> response;if(!callback_(command,response)||response.size()<2)return {};
   bool ok=response[response.size()-2]==0x90&&response.back()==0;response.resize(response.size()-2);return {response,ok};
 }
 virtual ApduResponse transceive_full(const ApduCommand& command,bool=false,size_t=255){return transceive(command);}
 private:Callback callback_;
};
class Session{
 public:Session(std::shared_ptr<NfcChannel> channel,CredentialStore& store):channel(channel),store(store){++fakeHomeKeySessions;}
 std::shared_ptr<NfcChannel> channel;CredentialStore& store;
};
enum class FlowState{Selected,Done,Failed};
enum class FailureReason{None,Invalid};
}
