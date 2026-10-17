#pragma once
#include "drive_replay_data.h"
#include "../app/telemetry_pipeline.h"
namespace obd {
class DriveReplay {
public:
    void begin(uint32_t now){epoch_=now;offset_=index_=0;paused_=false;}
    uint32_t position(uint32_t now) const {
        uint32_t p=offset_+(paused_?0:uint32_t(now-epoch_));
        return p<kReplayDuration?p:kReplayDuration;
    }
    bool paused()const{return paused_;}
    uint32_t index()const{return index_;}
    void pause(uint32_t now){offset_=position(now);paused_=true;}
    void play(uint32_t now){if(paused_){epoch_=now;paused_=false;}}
    void apply(VehicleSnapshot& s,AlarmState (&alarms)[ALARM_COUNT],uint32_t now){
        const uint32_t pos=position(now);
        if(pos==kReplayDuration&&!paused_)pause(now);
        while(index_+1<kReplayCount&&kReplayRows[index_+1].ms<=pos)++index_;
        const auto& r=kReplayRows[index_];
        s.rpm=r.values[0];s.oilTempC=r.values[1];s.coolantC=r.values[2];s.oilPressureBar=r.values[3];
        s.afr=r.values[4];s.throttle=r.values[5];s.speed=r.values[6];s.volts=r.values[7];
        s.validMask=r.fresh;s.linkQuality=r.quality;s.linkUp=r.state&64;s.transportState=(r.state&32)!=0;
        s.elmState=(r.state>>2)&7;s.totalOk=r.ok;s.totalFail=r.fail;
        for(uint8_t i=0;i<VF_COUNT;++i)s.valueMs[i]=now-(pos-r.ms);
        const uint8_t ids[]={1,3,2};
        for(uint8_t i=0;i<3;++i){const uint8_t slot=ids[i];s.candidateStates[slot]=uint8_t((r.candidateMask&(1u<<i))?ProbeState::Valid:ProbeState::NotRun);s.candidateValues[slot]=r.candidates[i];s.candidateMs[slot]=now-(pos-r.ms);}
        uint8_t fresh=0;
        if(valueFresh(s,VF_COOLANT,now))fresh|=1u<<ALARM_COOLANT;
        if(valueFresh(s,VF_RPM,now))fresh|=1u<<ALARM_RPM;
        evaluateSnapshot(s,alarms,s.settings,fresh,now);expireAlarms(s,alarms,now);
        if(paused_) {memset(alarms,0,sizeof(alarms));s.alarmFlags=0;}
        s.testPhaseMs=pos;s.testCycle=1;
    }
private:
    uint32_t epoch_=0,offset_=0,index_=0;bool paused_=false;
};
}
