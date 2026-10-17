#pragma once
#include <stdint.h>
#include <cmath>
namespace obd {
class ShiftLights {
public:
    static constexpr uint32_t kFrameMs=16;
    static float frequency(float rpm,float warn,float crit) {
        if(!std::isfinite(rpm) || rpm<warn)return 0;
        if(rpm<crit)return 2.0f+2.0f*(rpm-warn)/(crit>warn?crit-warn:1);
        const float extra=(rpm-crit)/800.0f;
        return 4.0f+2.0f*(extra>1?1:extra);
    }
    void tick(float rpm,uint32_t now,float warn,float crit) {
        uint32_t dt=started_?uint32_t(now-lastMs_):0;lastMs_=now;started_=true;
        const float next=frequency(rpm,warn,crit);
        if(next==0 || hz_==0)phase_=0.5f; // Begin a warning at full brightness.
        else {phase_+=(dt>100?100:dt)*next*.001f;phase_-=floorf(phase_);}
        hz_=next;
        const bool dim=hz_>0 && (phase_<.25f || phase_>.75f);
        if(dim!=dim_){dim_=dim;++transitions_;}
    }
    float frequency() const {return hz_;}
    uint32_t transitions() const {return transitions_;}
    uint8_t alpha(uint8_t lamp,float rpm,bool fresh,float warn,float maximum=7000) const {
        if(!fresh || !std::isfinite(rpm))return 38;
        const float thresholds[]={1100,1500,1800,2100,2500,2850,3200,3500,3750,4000,4600,5000,5500,5900,warn};
        const float factor=maximum/7000.0f;
        const float threshold=lamp==14?warn:thresholds[lamp]*factor;
        float level=(rpm-threshold+180*factor)/(180*factor);
        level=level<0?0:level>1?1:level;
        float wave=hz_>0?(phase_<.5f?phase_*2:2-phase_*2):1;
        wave=wave*wave*(3-2*wave); // Smooth brightness at both ends of the pulse.
        return uint8_t(38+217*level*wave+.5f);
    }
private:
    uint32_t lastMs_=0,transitions_=0;
    float hz_=0,phase_=.5f;
    bool started_=false,dim_=false;
};
}
