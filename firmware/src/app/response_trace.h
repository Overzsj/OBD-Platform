#pragma once
#include <Preferences.h>
#include "vehicle_state.h"
namespace obd {
struct ResponseTrace {
    uint32_t magic=0x54524331,boot=0,ms=0;
    uint8_t pid=0,reason=0;bool prompt=false,truncated=false;
    float previous=NAN,value=NAN;
    char command[48]{},raw[192]{};
};
// Evidence capture only: thresholds flag investigation candidates, not ECU faults.
inline uint8_t traceReason(const ParsedPid& p,const VehicleSnapshot& s,uint32_t now,float& previous){
    ValueField field;
    if(p.pid==0x05){field=VF_COOLANT;previous=s.coolantC;}
    else if(p.pid==0x42){field=VF_VOLTS;previous=s.volts;}
    else if(p.pid==0x11){field=VF_THROTTLE;previous=s.throttle;}
    else return 0;
    if(!isfinite(p.value))return 0;
    if(p.pid==0x42&&p.value<9&&valueFresh(s,VF_RPM,now)&&s.rpm>500)return 1;
    if(!valueFresh(s,field,now)||uint32_t(now-s.valueMs[field])>2500)return 0;
    const float limit=p.pid==0x05?8:p.pid==0x42?3:50;
    return fabsf(p.value-previous)>limit?2:0;
}
class ResponseArchive {
public:
    bool available=false,writeOk=true;uint8_t writes=0;
    void begin(){available=prefs_.begin("obdtrace",false);}
    ResponseTrace load(uint8_t i){
        ResponseTrace empty,r;if(!available||i>=4)return empty;
        char key[4];snprintf(key,sizeof(key),"t%u",i);
        if(prefs_.getBytes(key,&r,sizeof(r))==sizeof(r)&&r.magic==0x54524331&&r.raw[191]==0&&r.command[47]==0)return r;
        return empty;
    }
    bool observe(const ParsedPid* parsed,uint8_t count,const VehicleSnapshot& s,uint32_t boot,uint32_t now,const char* cmd,const char* raw,bool prompt){
        if(writes>=4)return false; // At most four NVS writes per device boot.
        for(uint8_t i=0;i<count;++i){float previous=NAN;const uint8_t reason=traceReason(parsed[i],s,now,previous);if(!reason)continue;
            ResponseTrace r;r.boot=boot;r.ms=now;r.pid=parsed[i].pid;r.reason=reason;r.prompt=prompt;r.previous=previous;r.value=parsed[i].value;
            r.truncated=strlen(raw)>=sizeof(r.raw);snprintf(r.command,sizeof(r.command),"%s",cmd);snprintf(r.raw,sizeof(r.raw),"%s",raw);
            char key[4];snprintf(key,sizeof(key),"t%u",writes++);
            if(!available||prefs_.putBytes(key,&r,sizeof(r))!=sizeof(r))writeOk=false;
            return true;
        }
        return false;
    }
private:Preferences prefs_;
};
}
