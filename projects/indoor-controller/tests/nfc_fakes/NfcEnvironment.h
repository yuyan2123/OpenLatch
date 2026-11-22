#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_err.h>
#include <esp_system.h>
#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <vector>
#include <cstring>
inline bool fakeQueueFailure=false, fakeTaskFailure=false, fakeRadioFailure=false, fakeSendFailure=false;
inline int fakeRadioStarts=0, fakeProfileRuns=0;
inline std::function<void()> fakeDelayHook;
struct FakeQueue { size_t capacity, size; std::deque<std::vector<uint8_t>> items; };
using QueueHandle_t=FakeQueue*;
inline std::function<void(QueueHandle_t)> fakeQueueReceiveHook;
inline std::vector<std::unique_ptr<FakeQueue>> fakeQueues;
inline QueueHandle_t xQueueCreate(size_t capacity,size_t size) {
  if(fakeQueueFailure)return nullptr;
  fakeQueues.push_back(std::make_unique<FakeQueue>(FakeQueue{capacity,size,{}})); return fakeQueues.back().get();
}
inline int xQueueSend(QueueHandle_t queue,const void* input,int) {
  if(!queue||queue->items.size()>=queue->capacity)return 0;
  const auto* bytes=static_cast<const uint8_t*>(input); queue->items.emplace_back(bytes,bytes+queue->size); return 1;
}
inline int xQueueReceive(QueueHandle_t queue,void* output,int) {
  if(fakeQueueReceiveHook)fakeQueueReceiveHook(queue);
  if(!queue||queue->items.empty())return 0;
  std::memcpy(output,queue->items.front().data(),queue->size);queue->items.pop_front();return 1;
}
constexpr int pdTRUE=1,pdPASS=1;
inline uint32_t pdMS_TO_TICKS(uint32_t value){return value;}
inline int xTaskCreate(void(*)(void*),const char*,uint32_t,void*,int,void*){return fakeTaskFailure?0:1;}
inline void vTaskDelete(void*){}
inline void vTaskDelay(uint32_t delay){fakeMillis+=delay;if(fakeDelayHook)fakeDelayHook();}
constexpr int WIFI_IF_STA=0,ESP_MAC_WIFI_STA=0;
struct esp_now_recv_info_t{const uint8_t* src_addr;const uint8_t* des_addr;};
struct esp_now_peer_info_t{uint8_t peer_addr[6]{},lmk[16]{};int ifidx=0,channel=0;bool encrypt=false;};
using RecvCallback=void(*)(const esp_now_recv_info_t*,const uint8_t*,int);
inline RecvCallback fakeReceive=nullptr;
inline std::function<void(const uint8_t*,size_t)> fakeRadioSend;
inline std::array<uint8_t,6> fakeStationMac{2,0,0,0,0,1};
inline esp_err_t esp_read_mac(uint8_t* output,int){std::copy(fakeStationMac.begin(),fakeStationMac.end(),output);return ESP_OK;}
inline esp_err_t esp_now_init(){++fakeRadioStarts;return fakeRadioFailure?ESP_FAIL:ESP_OK;}
inline esp_err_t esp_now_deinit(){fakeReceive=nullptr;return ESP_OK;}
inline esp_err_t esp_now_get_version(uint32_t* version){*version=2;return ESP_OK;}
inline esp_err_t esp_now_set_pmk(const uint8_t*){return ESP_OK;}
inline esp_err_t esp_now_add_peer(const esp_now_peer_info_t*){return ESP_OK;}
inline esp_err_t esp_now_register_recv_cb(RecvCallback callback){fakeReceive=callback;return ESP_OK;}
inline esp_err_t esp_now_send(const uint8_t*,const uint8_t* bytes,size_t length){
  if(fakeSendFailure)return ESP_FAIL;if(fakeRadioSend)fakeRadioSend(bytes,length);return ESP_OK;
}
inline void esp_fill_random(void* output,size_t size){auto* bytes=static_cast<uint8_t*>(output);for(size_t i=0;i<size;++i)bytes[i]=uint8_t(esp_random());}
inline int psa_crypto_init(){return 0;}
constexpr int PSA_SUCCESS=0;
inline int sodium_init(){return 0;}
struct mbedtls_md_info_t{};
constexpr int MBEDTLS_MD_SHA256=0;
inline const mbedtls_md_info_t* mbedtls_md_info_from_type(int){static mbedtls_md_info_t info;return &info;}
inline int mbedtls_md_hmac(const mbedtls_md_info_t*,const uint8_t*,size_t,const uint8_t* bytes,size_t size,uint8_t* output){
  std::fill_n(output,32,uint8_t(0));for(size_t i=0;i<size;++i)output[i%32]^=bytes[i];return 0;
}
inline bool fakeShaFailure=false;
inline int mbedtls_sha256(const uint8_t* bytes,size_t size,uint8_t* output,int){
  if(fakeShaFailure)return -1;uint32_t hash=2166136261u;
  for(size_t i=0;i<size;++i)hash=(hash^bytes[i])*16777619u;
  for(size_t i=0;i<32;++i)output[i]=uint8_t((hash>>(8*(i%4)))+i);return 0;
}
