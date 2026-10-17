#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "alarms.h"
#include "build_mode.h"
namespace obd {
struct PresentationOptions {
    uint8_t layout=0,sounds=7; // Stable layout; startup / RPM / coolant.
    bool valid()const{return layout<=1&&sounds<=7;}
};
constexpr unsigned kMaxRpmSetting=OBD_AUTO_ADAPT?9999:7000;
struct DeviceSettings {
    uint16_t rpmWarn=5000,rpmCrit=6000,tempWarn=100,tempCrit=108;
    uint8_t muted=OBD_DATA_TEST?1:0,bootAnimation=OBD_DATA_TEST?0:1,soundLevel=1,reserved=0;
};
inline bool validSettings(const DeviceSettings& s) {
    return s.rpmWarn>=2000 && s.rpmCrit<=kMaxRpmSetting && s.rpmCrit>=s.rpmWarn+100 &&
        s.tempWarn>=80 && s.tempCrit<=125 && s.tempCrit>=s.tempWarn+2 &&
        s.muted<=1 && s.bootAnimation<=1 && s.soundLevel<=3 && !s.reserved;
}
inline uint32_t recordHash(const void* data,size_t size) {
    auto p=static_cast<const uint8_t*>(data);uint32_t h=2166136261u;
    while(size--) h=(h^*p++)*16777619u;
    return h;
}
struct SettingsRecord {
    uint32_t magic=0x32474643,version=1;
    DeviceSettings value{};
    uint32_t checksum=0;
    void seal() { checksum=recordHash(this,offsetof(SettingsRecord,checksum)); }
    bool valid() const { return magic==0x32474643 && version==1 && validSettings(value) &&
        checksum==recordHash(this,offsetof(SettingsRecord,checksum)); }
};
inline AlarmCfg configuredAlarm(AlarmId id,const DeviceSettings& s) {
    AlarmCfg cfg=kDefaultAlarms[id];
    if(id==ALARM_RPM) {cfg.warn=s.rpmWarn;cfg.crit=s.rpmCrit;}
    if(id==ALARM_COOLANT) {cfg.warn=s.tempWarn;cfg.crit=s.tempCrit;}
    return cfg;
}
// Strict full-record update: invalid/truncated requests never apply partially.
inline bool parseSettings(const char* text,DeviceSettings& out) {
    unsigned values[7]={};
    for(unsigned i=0;i<7;++i) {
        if(*text<'0' || *text>'9')return false;
        while(*text>='0' && *text<='9') {
            values[i]=values[i]*10+unsigned(*text++-'0');
            if(values[i]>kMaxRpmSetting)return false;
        }
        if(i<6) {if(*text++!=' ')return false;}
        else if(*text)return false;
    }
    unsigned a=values[0],b=values[1],c=values[2],d=values[3],e=values[4],f=values[5],g=values[6];
    if(a>kMaxRpmSetting || b>kMaxRpmSetting || c>125 || d>125 || e>1 || f>1 || g>3)return false;
    DeviceSettings s;s.rpmWarn=a;s.rpmCrit=b;s.tempWarn=c;s.tempCrit=d;s.muted=e;s.bootAnimation=f;s.soundLevel=g;
    if(!validSettings(s))return false;
    out=s;return true;
}
}
