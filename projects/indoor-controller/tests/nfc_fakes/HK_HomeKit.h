#pragma once
#include <ddk/store/CredentialStore.h>
#include <functional>
#include <vector>
inline std::vector<uint8_t> fakeProvisionReply{1,1,0};
inline std::function<void(ddk::CredentialStore&)> fakeProvisionHook;
class HK_HomeKit{
 public:HK_HomeKit(ddk::CredentialStore& store,std::function<void()> clear,const std::vector<uint8_t>& bytes):store_(store),clear_(clear),bytes_(bytes){}
 std::vector<uint8_t> processResult(){if(!bytes_.empty()&&bytes_[0]==0)clear_();if(fakeProvisionHook)fakeProvisionHook(store_);return fakeProvisionReply;}
 private:ddk::CredentialStore& store_;std::function<void()> clear_;std::vector<uint8_t> bytes_;
};
