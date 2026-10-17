#pragma once
#include <stdio.h>
#include <math.h>
#include "../app/vehicle_state.h"
#include "../test/data_script.h"
#include "page_navigation.h"
#include "zh_text.h"
#include "instrument_palette.h"
#include "../obd/support_probe.h"
namespace obd {
inline bool adapterVoltageFresh(const VehicleSnapshot& s,uint32_t now){return s.linkUp&&s.adapterVoltsValid&&isfinite(s.adapterVolts)&&uint32_t(now-s.adapterVoltsMs)<=5000;}
inline const char* probeWord(const VehicleSnapshot& s,ValueField field){
    const int slot=field==VF_OIL_TEMP?0:field==VF_VOLTS?1:-1;
    if(slot<0)return "未采集";
    const auto state=ProbeState(s.probeStates[slot]);
    if(state==ProbeState::Pending)return "探测中";
    if(state==ProbeState::Timeout)return "读取超时";
    if(state==ProbeState::Rejected)return "命令拒绝";
    if(state==ProbeState::Malformed)return "应答异常";
    if(state==ProbeState::NoData)return (slot==0?s.oilReported:s.voltsReported)==0?"未报告支持":"无数据";
    return "未采集";
}
// Same painter is used by the panel renderer and the exported native previews.
template<class Canvas> void drawDataLab(Canvas& c,const VehicleSnapshot& s,uint32_t now) {
    const uint16_t bg=instrument::background,panel=instrument::panel,white=instrument::white,muted=instrument::muted;
    const uint16_t cyan=instrument::cyan,amber=instrument::yellow,red=instrument::critical,edge=instrument::edge,pink=instrument::pink;
    c.fillRect(0,0,480,320,bg);
    c.fillRect(0,0,480,28,instrument::header);c.fillRect(0,0,480,1,pink);c.fillRect(8,27,464,1,edge);
    zhText(c,10,4,"数据总览",pink,ZhSize::Title);zhRight(c,469,7,kSimulatedSource?"模拟数据·非实车":"实车数据",cyan,ZhSize::Small);
    char line[160];
#if OBD_DATA_TEST
    snprintf(line,sizeof(line),"%02u/%02u  %s",unsigned(s.testStage+1),unsigned(s.testStage>=kLabStages?kLabScenarioCount:kLabStages),zhLabStage(s.testStage));
    zhText(c,10,33,line,white,ZhSize::Small);snprintf(line,sizeof(line),"%02lu/12秒  第%lu轮",(unsigned long)(s.testPhaseMs/1000),(unsigned long)s.testCycle);zhRight(c,469,33,line,muted,ZhSize::Small);
    c.fillRect(8,48,464,2,edge);c.fillRect(8,48,int(464*s.testPhaseMs/kLabStageMs),2,pink);
#else
    zhText(c,10,33,"数据新鲜度与报警",white,ZhSize::Small);zhRight(c,469,33,kSimulatedSource?"台架输入":"未支持项目不显示数值",muted,ZhSize::Small);
    c.fillRect(8,48,464,2,edge);
#endif
    const bool adapter= !valueFresh(s,VF_VOLTS,now)&&adapterVoltageFresh(s,now);
    const float values[]={s.rpm,s.speed,s.coolantC,s.throttle,s.oilTempC,s.oilPressureBar,s.afr,adapter?s.adapterVolts:s.volts};
    const ValueField fields[]={VF_RPM,VF_SPEED,VF_COOLANT,VF_THROTTLE,VF_OIL_TEMP,VF_OIL_PRESSURE,VF_AFR,VF_VOLTS};
    const char* names[]={"发动机转速","车速","水温","节气门开度","机油温度","机油压力","空燃比","模块电压"};
    const char* units[]={"转/分","公里/时","℃","%","℃","巴",":1","伏"};
    const int8_t alarms[]={ALARM_RPM,ALARM_SPEED,ALARM_COOLANT,-1,-1,-1,-1,ALARM_VOLT_LOW};
    for(uint8_t i=0;i<8;++i) {
        const int x=8+(i%4)*118,y=58+(i/4)*82;
        const bool seen=(i==7&&adapter)||(s.validMask&(1u<<fields[i])),fresh=((i==7&&adapter)||valueFresh(s,fields[i],now))&&isfinite(values[i]);
        const bool warn=alarms[i]>=0 && (s.alarmFlags&(1u<<alarms[i]));
        const bool crit=alarms[i]>=0 && (s.alarmFlags&(1u<<(alarms[i]+4)));
        const uint16_t color=!fresh?muted:crit?red:warn?amber:instrument::yellow;
        const uint16_t stateColor=!fresh?muted:crit?red:warn?amber:cyan;
        c.fillRect(x,y,112,76,panel);c.rect(x,y,112,76,(warn||crit)&&fresh?color:edge);c.fillRect(x,y,3,76,!fresh?muted:(warn||crit)?color:pink);
        zhText(c,x+8,y+5,i==7&&adapter?"适配器电压":names[i],white);
        if(!fresh)snprintf(line,sizeof(line),"--");
        else if(i==5 || i==6 || i==7)snprintf(line,sizeof(line),"%.1f",values[i]);
        else snprintf(line,sizeof(line),"%.0f",values[i]);
        c.text(x+8,y+24,line,3,color);zhText(c,x+8,y+46,units[i],muted,ZhSize::Small);
        zhRight(c,x+103,y+46,crit&&fresh?"严重":warn&&fresh?"预警":"",color,ZhSize::Small);
        if(fresh)snprintf(line,sizeof(line),"实时 %lu毫秒",(unsigned long)(now-(i==7&&adapter?s.adapterVoltsMs:s.valueMs[fields[i]])));
        else snprintf(line,sizeof(line),"%s",!s.linkUp?"已断开":seen?"已过期":probeWord(s,fields[i]));
        zhText(c,x+8,y+61,line,stateColor,ZhSize::Small);
    }
    const char* candidateNames[]={"目标当量比","进气温度","发动机负荷","空气流量"};
    const char* candidateUnits[]={"目标值","℃","%","克/秒"};
    for(uint8_t i=0;i<4;++i){
        const int x=8+118*i;const bool fresh=candidateFresh(s,i,now);
        c.fillRect(x,224,112,39,panel);c.rect(x,224,112,39,edge);
        zhText(c,x+5,226,candidateNames[i],white,ZhSize::Small);
        if(fresh)snprintf(line,sizeof(line),i==0?"%.3f":i==3?"%.2f":"%.1f",s.candidateValues[i]);
        else snprintf(line,sizeof(line),"--");
        c.text(x+5,242,line,1,fresh?amber:muted);
        const auto state=ProbeState(s.candidateStates[i]);
        const char* status=fresh?candidateUnits[i]:!s.linkUp?"已断开":state==ProbeState::Valid?"已过期":state==ProbeState::NoData?"无数据":state==ProbeState::Timeout?"读取超时":state==ProbeState::Malformed?"应答异常":state==ProbeState::Rejected?"命令拒绝":"待验证";
        zhRight(c,x+107,246,status,fresh?cyan:muted,ZhSize::Small);
    }
    snprintf(line,sizeof(line),"链路：%s  质量：%u%%  事件：%u条",s.linkUp?"就绪":"等待",s.linkQuality,s.eventCount);zhText(c,10,267,line,white);
    snprintf(line,sizeof(line),"转速 %u/%u  水温 %u/%u",s.settings.rpmWarn,s.settings.rpmCrit,s.settings.tempWarn,s.settings.tempCrit);zhText(c,10,285,line,muted,ZhSize::Small);
    zhText(c,10,303,"标准PID·IAT/MAF/LOAD",muted,ZhSize::Small);zhRight(c,469,303,s.settings.muted?"声音：已静音":"声音：已开启",muted,ZhSize::Small);
    zhText(c,224,303,"02/03",muted,ZhSize::Small);
}
}
