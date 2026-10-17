#pragma once
#include <math.h>
#include "vehicle_state.h"
#include "../obd/elm_parse.h"
namespace obd {
inline void acceptParsed(const ParsedPid* p,uint8_t n,VehicleSnapshot& s,uint8_t& freshMask,uint32_t now) {
    for(uint8_t i=0;i<n;++i) {
        if(!isfinite(p[i].value))continue;
        int8_t field=-1;
        switch(p[i].pid) {
        case 0x0C:s.rpm=p[i].value;field=VF_RPM;freshMask|=1u<<ALARM_RPM;break;
        case 0x0D:s.speed=p[i].value;field=VF_SPEED;freshMask|=1u<<ALARM_SPEED;break;
        case 0x05:s.coolantC=p[i].value;field=VF_COOLANT;freshMask|=1u<<ALARM_COOLANT;break;
        case 0x42:s.volts=p[i].value;field=VF_VOLTS;freshMask|=1u<<ALARM_VOLT_LOW;break;
        case 0x11:s.throttle=p[i].value;field=VF_THROTTLE;break;
case 0x5C:s.oilTempC=p[i].value;field=VF_OIL_TEMP;break;
#if OBD_BENCH_AUX
        case 0xF0:s.oilPressureBar=p[i].value;field=VF_OIL_PRESSURE;break;
        case 0xF1:s.afr=p[i].value;field=VF_AFR;break;
#endif
        case 0x0F:case 0x04:case 0x10:{
            const uint8_t c=p[i].pid==0x0F?1:p[i].pid==0x04?2:3;
            s.candidateValues[c]=p[i].value;s.candidateMs[c]=now;s.candidateStates[c]=uint8_t(ProbeState::Valid);break;}
        default:if(s.extraCount<kMaxDisplayPids)s.extra[s.extraCount++]=p[i].value;break;
        }
        if(field>=0) {s.validMask|=uint8_t(1u<<field);s.valueMs[field]=now;}
        ++s.totalOk;
    }
}
inline void evaluateSnapshot(VehicleSnapshot& s,AlarmState (&alarms)[ALARM_COUNT],const DeviceSettings& settings,uint8_t freshMask,uint32_t now) {
    const float values[]={s.coolantC,s.rpm,s.speed,s.volts};
    for(uint8_t i=0;i<(OBD_DATA_TEST?ALARM_COUNT:2);++i)
        if(freshMask&(1u<<i))evaluateAlarmAuto(configuredAlarm(AlarmId(i),settings),alarms[i],values[i],now);
    s.alarmFlags=packAlarmFlags(alarms);
}
inline void expireAlarms(VehicleSnapshot& s,AlarmState (&alarms)[ALARM_COUNT],uint32_t now) {
    const ValueField fields[]={VF_COOLANT,VF_RPM,VF_SPEED,VF_VOLTS};
    for(uint8_t i=0;i<ALARM_COUNT;++i)if(!valueFresh(s,fields[i],now))alarms[i]={};
    s.alarmFlags=packAlarmFlags(alarms);
}
}
