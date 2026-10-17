#pragma once
#include "device_settings.h"
namespace obd {
// Presentation only. This record never changes PID decoding or alarms.
constexpr uint8_t kDisplaySignals=11;
struct DisplaySettings {
 uint16_t maximum[kDisplaySignals]={120,120,200,100,100,7000,240,16,150,10,25};
 bool valid()const{
  const uint16_t low[]={10,40,1,1,1,1000,10,1,40,1,1};
  const uint16_t high[]={255,255,655,100,100,9999,255,66,255,20,40};
  for(uint8_t i=0;i<kDisplaySignals;++i)if(maximum[i]<low[i]||maximum[i]>high[i])return false;
  return true;
 }
};
struct DisplayRecord {
 uint32_t magic=0x44535031,version=1;DisplaySettings value{};uint16_t reserved=0;uint32_t checksum=0;
 void seal(){checksum=recordHash(this,offsetof(DisplayRecord,checksum));}
 bool valid()const{return magic==0x44535031&&version==1&&!reserved&&value.valid()&&checksum==recordHash(this,offsetof(DisplayRecord,checksum));}
};
inline bool parseDisplay(const char* text,DisplaySettings& out){
 DisplaySettings next;
 for(uint8_t i=0;i<kDisplaySignals;++i){unsigned v=0;if(*text<'0'||*text>'9')return false;
  while(*text>='0'&&*text<='9'){v=v*10+unsigned(*text++-'0');if(v>9999)return false;}
  next.maximum[i]=v;
  if(i+1<kDisplaySignals){if(*text++!=' ')return false;}else if(*text)return false;
 }if(!next.valid())return false;out=next;return true;
}
}
