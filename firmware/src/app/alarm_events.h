#pragma once
#include <math.h>
#include "device_settings.h"
namespace obd {
enum class EventEnd:uint8_t { Recovered=0,DataLost=1,SettingsChanged=2 };
struct AlarmEvent {
    uint32_t boot=0,startMs=0,durationMs=0;
    float trigger=0,peak=0,rpm=0;
    uint8_t alarm=0,severity=0,linkQuality=0,reason=0;
};
struct EventRecord {
    uint32_t magic=0x32545645,version=1,count=0;
    AlarmEvent rows[12]{};
    uint32_t checksum=0;
    void seal() {checksum=recordHash(this,offsetof(EventRecord,checksum));}
    bool valid() const {
        if(magic!=0x32545645 || version!=1 || count>12 || checksum!=recordHash(this,offsetof(EventRecord,checksum)))return false;
        for(uint32_t i=0;i<count;++i) if(rows[i].alarm>1 || rows[i].severity<1 || rows[i].severity>2 || rows[i].reason>2 ||
            !isfinite(rows[i].trigger) || !isfinite(rows[i].peak) || !isfinite(rows[i].rpm))return false;
        return true;
    }
};
class AlarmEvents {
public:
    EventRecord history{};
    AlarmEvent active[2]{};
    bool running[2]{false,false},dirty=false;
    void sample(uint8_t id,Sev sev,float value,float rpm,bool fresh,uint8_t quality,uint32_t boot,uint32_t now) {
        if(id>1)return;
        if(!fresh || sev==Sev::None) {finish(id,now,fresh?EventEnd::Recovered:EventEnd::DataLost);return;}
        if(!running[id]) {
            active[id]=AlarmEvent{};active[id].boot=boot;active[id].startMs=now;
            active[id].trigger=active[id].peak=value;active[id].rpm=rpm;
            active[id].alarm=id;active[id].linkQuality=quality;running[id]=true;
        }
        if(value>active[id].peak)active[id].peak=value;
        if(uint8_t(sev)>active[id].severity)active[id].severity=uint8_t(sev);
        active[id].durationMs=now-active[id].startMs;
    }
    void finish(uint8_t id,uint32_t now,EventEnd why) {
        if(!running[id])return;
        auto e=active[id];e.durationMs=now-e.startMs;e.reason=uint8_t(why);
        if(history.count==12) {memmove(history.rows,history.rows+1,11*sizeof(AlarmEvent));--history.count;}
        history.rows[history.count++]=e;running[id]=false;dirty=true;
    }
};
}
