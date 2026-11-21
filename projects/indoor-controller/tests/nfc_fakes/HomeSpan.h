#pragma once
#include <NfcEnvironment.h>
#include <vector>
using boolean=bool;
class TLV8{
 public:std::vector<uint8_t> bytes;TLV8(void*,int){} void add(int tag,int value){bytes.push_back(uint8_t(tag));bytes.push_back(uint8_t(value));}
 size_t pack_size()const{return bytes.size();}void pack(uint8_t* out)const{std::copy(bytes.begin(),bytes.end(),out);}
 void unpack(const uint8_t* in,size_t size){bytes.assign(in,in+size);}

 struct Field {
   const uint8_t* header = nullptr;
   bool operator==(Field other) const { return header == other.header; }
   const uint8_t* operator*() const { return header + 2; }
 };
 Field end() const { return {}; }
 Field find(uint8_t tag) const {
   for (size_t offset = 0; offset + 2 <= bytes.size();) {
     const size_t next = offset + 2 + bytes[offset + 1];
     if (next > bytes.size()) break;
     if (bytes[offset] == tag) return {bytes.data() + offset};
     offset = next;
   }
   return end();
 }
 int len(Field field) const { return field.header ? field.header[1] : -1; }
};
class SpanCharacteristic{
 public:int value=1,newValue=1;bool pending=false;std::vector<uint8_t> input,response;
 SpanCharacteristic()=default;template<class T>explicit SpanCharacteristic(T){}
 bool updated()const{return pending;}int getNewVal()const{return newValue;}int getVal()const{return value;}void setVal(int input){value=input;}
 void getNewTLV(TLV8& tlv){tlv.bytes=input;}void setTLV(const TLV8& tlv,bool){response=tlv.bytes;pending=false;}
};
struct SpanService{std::vector<int*> req,opt;virtual ~SpanService()=default;virtual boolean update(){return true;}virtual void loop(){}void setPrimary(){}};
namespace Service {struct AccessoryInformation:SpanService{};struct LockMechanism:SpanService{};}
namespace Characteristic{struct Version:SpanCharacteristic{};struct LockCurrentState:SpanCharacteristic{using SpanCharacteristic::SpanCharacteristic;};struct LockTargetState:SpanCharacteristic{using SpanCharacteristic::SpanCharacteristic;};}
inline struct {int Version=0;}hapChars;
#define CUSTOM_CHAR(Name,...) namespace Characteristic{struct Name:SpanCharacteristic{using SpanCharacteristic::SpanCharacteristic;};} inline int _CUSTOM_##Name=0;
#define CUSTOM_SERV(Name,...) namespace Service{struct Name:SpanService{};}
struct FakeHapController{std::array<uint8_t,32> key{};const uint8_t* getLTPK()const{return key.data();}};
inline struct FakeHomeSpan{
 std::vector<FakeHapController> controllers;
 auto controllerListBegin(){return controllers.begin();}auto controllerListEnd(){return controllers.end();}
}homeSpan;
