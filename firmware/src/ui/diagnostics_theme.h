#pragma once
#include "data_lab_theme.h"
namespace obd {
template<class Canvas> void drawDiagnostics(Canvas& c,const VehicleSnapshot& s,uint32_t now,uint32_t heap) {
    const uint16_t bg=instrument::background,white=instrument::white,muted=instrument::muted,cyan=instrument::cyan;
    c.fillRect(0,0,480,320,bg);c.fillRect(0,0,480,32,instrument::header);c.fillRect(0,0,480,1,instrument::pink);c.fillRect(8,31,464,1,instrument::edge);
    zhText(c,12,6,"设备诊断",instrument::pink,ZhSize::Title);zhRight(c,468,10,kSimulatedSource?"模拟数据·非实车":"实车数据",cyan,ZhSize::Small);
    char line[160];snprintf(line,sizeof(line),"链路：%s  适配器：%s  数据：%s",s.transportState?"已连接":"未连接",zhElmState(s.elmState),s.linkUp?"就绪":"等待");zhText(c,14,44,line,white,ZhSize::Small);
    snprintf(line,sizeof(line),"运行 %lu秒  重初始化 %lu次  重连 %lu次",(unsigned long)(now/1000),(unsigned long)s.reinits,(unsigned long)s.reconnects);zhText(c,14,65,line,white,ZhSize::Small);
    const ValueField fields[]={VF_RPM,VF_SPEED,VF_COOLANT,VF_THROTTLE,VF_OIL_TEMP,VF_OIL_PRESSURE,VF_AFR,VF_VOLTS};
    const char* names[]={"发动机转速","车速","水温","节气门开度","机油温度","机油压力","空燃比","模块电压"};
    for(uint8_t i=0;i<8;++i) {
        const bool adapter=i==7&&!valueFresh(s,VF_VOLTS,now)&&adapterVoltageFresh(s,now);
        const bool fresh=adapter||valueFresh(s,fields[i],now),seen=adapter||(s.validMask&(1u<<fields[i]));
        const uint16_t color=fresh?cyan:muted;const int y=92+i*19;
        if(!(i%2))c.fillRect(8,y-1,464,18,instrument::panel);
        zhText(c,14,y,adapter?"适配器电压":names[i],muted);zhText(c,140,y,!s.linkUp?"已断开":fresh?"实时":seen?"已过期":probeWord(s,fields[i]),color);
        if(seen){snprintf(line,sizeof(line),"距更新 %lu毫秒",(unsigned long)(now-(adapter?s.adapterVoltsMs:s.valueMs[fields[i]])));zhText(c,250,y,line,fresh?white:muted);}
    }
    c.fillRect(8,250,464,23,instrument::header);
    snprintf(line,sizeof(line),"可用内存 %lu字节",(unsigned long)heap);zhText(c,14,254,line,instrument::yellow);
    snprintf(line,sizeof(line),"链路质量 %u%%",s.linkQuality);zhRight(c,468,254,line,cyan);
    snprintf(line,sizeof(line),"事件 %u条  挡位待标定",s.eventCount);zhText(c,14,278,line,muted,ZhSize::Small);if(adapterVoltageFresh(s,now)){snprintf(line,sizeof(line),"适配器电压 %.1f伏",s.adapterVolts);zhRight(c,468,278,line,cyan,ZhSize::Small);}else zhRight(c,468,278,"触摸未接入",muted,ZhSize::Small);
    zhText(c,14,302,"电脑切页：主仪表 / 数据总览 / 设备诊断",muted,ZhSize::Small);zhRight(c,468,302,"03/03",muted,ZhSize::Small);
}
}
