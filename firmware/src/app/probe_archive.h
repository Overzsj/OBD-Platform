#pragma once
#include <Preferences.h>
#include "../obd/support_probe.h"
namespace obd {
class ProbeArchive {
public:
    bool available=false,writeOk=true;
    void begin(){available=prefs_.begin("obdprobe",false);}
    ProbeResult load(uint8_t i){
        ProbeResult r,empty;if(!available||i>=kProbeCount)return empty;
        char key[4];snprintf(key,sizeof(key),"q%u",i);
        if(prefs_.getBytes(key,&r,sizeof(r))==sizeof(r)&&r.magic==0x50524F31&&r.raw[191]==0&&uint8_t(r.state)<=uint8_t(ProbeState::Malformed))return r;
        return empty;
    }
    void save(uint8_t i,const ProbeResult& r){if(i>=kProbeCount)return;char key[4];snprintf(key,sizeof(key),"q%u",i);
        if(!available||prefs_.putBytes(key,&r,sizeof(r))!=sizeof(r))writeOk=false;
    }
private:Preferences prefs_;
};
}
