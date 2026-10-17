#pragma once
#include <stdint.h>
namespace obd {
// Protocol inputs, not direct UI values. Every displayed value must be decoded.
struct LabSample {
    float rpm=850,speed=0,water=40,volts=14.2f,tps=4,oil=35,pressure=1.3f,afr=14.7f;
    float intake=27,maf=2.9f,load=22;
    bool waterMissing=false,silent=false,connected=true;
};
constexpr uint8_t kLabScenarioCount=14; // Includes the standalone startup sweep.
constexpr uint32_t kLabStageMs=12000,kLabStages=11,kLabCycleMs=kLabStageMs*kLabStages;
inline const char* labStageName(uint8_t stage) {
    const char* names[]={"IDLE","SWEEP","WARNING","CRITICAL","RECOVER","WATER ALERT","WATER LOSS","SILENCE","LINK DOWN","LINK RESTORE","VALID ZERO","RPM WARNING ONLY","RPM REDLINE ONLY"};
    if(stage==13)return "STARTUP 0-6500";
    return stage<13?names[stage]:"STARTING";
}
inline LabSample labSample(uint8_t stage,uint32_t phaseMs) {
    LabSample s;float k=float(phaseMs%kLabStageMs)/kLabStageMs;
    switch(stage) {
    case 1:s.rpm=850+k*4600;s.speed=k*100;s.water=40+k*55;s.tps=4+k*75;s.oil=35+k*70;s.pressure=1.3f+k*3.3f;s.afr=14.7f-k*2.2f;break;
    case 2:s.rpm=5800;s.speed=125;s.water=103;s.oil=110;s.pressure=4;s.afr=12.8f;s.tps=70;break;
    case 3:s.rpm=6400;s.speed=145;s.water=112;s.volts=10.8f;s.oil=134;s.pressure=4.6f;s.afr=12.2f;s.tps=95;break;
    case 4:case 9:s.rpm=2400;s.speed=70;s.water=90;s.oil=100;s.pressure=3;s.afr=14.7f;s.tps=18;break;
    case 5:s.rpm=2500;s.speed=70;s.water=112;s.oil=120;s.pressure=3;s.tps=20;break;
    case 6:s.rpm=2500;s.speed=70;s.water=112;s.oil=120;s.pressure=3;s.tps=20;s.waterMissing=true;break;
    case 7:s.silent=true;break;
    case 8:s.connected=false;break;
    case 10:s.rpm=0;s.speed=0;s.water=-40;s.volts=16;s.tps=100;s.oil=-40;s.pressure=0;s.afr=25;break;
    case 11:s.rpm=5800;s.water=85;s.speed=0;s.volts=14.2f;s.tps=60;break;
    case 12:s.rpm=6400;s.water=85;s.speed=0;s.volts=14.2f;s.tps=80;break;
    case 13:{const float ramp=phaseMs>=10000?1.0f:float(phaseMs)/10000.0f;
        s.rpm=ramp*6500;s.speed=ramp*100;s.water=85;s.volts=14.2f;s.tps=ramp*95;
        s.intake=27+ramp*15;s.maf=ramp*130;s.load=ramp*98;break;}
    default:break;
    }
    // Explicit synthetic desktop scenarios, never derived from a physical log.
    if(stage==1){s.intake=27+k*15;s.maf=3+k*87;s.load=22+k*66;}
    else if(stage==2||stage==11){s.intake=42;s.maf=100;s.load=82;}
    else if(stage==3||stage==12){s.intake=48;s.maf=130;s.load=98;}
    else if(stage==4||stage==9){s.intake=32;s.maf=20;s.load=35;}
    else if(stage==5||stage==6){s.intake=45;s.maf=22;s.load=38;}
    else if(stage==10){s.intake=-40;s.maf=0;s.load=0;}
    return s;
}
}
