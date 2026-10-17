#pragma once
#include "vehicle_state.h"
namespace obd {
// Stable semantic ABI: themes never know PID numbers or snapshot offsets.
enum class Signal:uint8_t { Iat,Coolant,Maf,Load,Throttle,Rpm,Speed,Voltage,OilTemp,OilPressure,Afr,Count };
struct SignalDef { const char* id;const char* label;const char* chinese;const char* unit;float maximum;uint8_t decimals; };
static const SignalDef kSignals[]={
 {"iat","IAT","进气温度","C",120,0},{"coolant","WATER","水温","C",120,0},
 {"maf","MAF","空气流量","g/s",200,1},{"load","LOAD","发动机负荷","%",100,0},
 {"throttle","TPS","节气门","%",100,0},{"rpm","RPM","转速","rpm",7000,0},
 {"speed","SPEED","车速","km/h",240,0},{"voltage","VOLT","电压","V",16,1},
 {"oil_temp","OIL T","油温","C",150,0},{"oil_pressure","OIL P","油压","bar",10,1},
 {"afr","AFR","空燃比",":1",25,1}
};
struct SignalValue {float value=NAN;ReadingStatus status=ReadingStatus::Missing;bool fresh()const{return status==ReadingStatus::Fresh&&isfinite(value);} };
inline SignalValue signalValue(const VehicleSnapshot& s,Signal id,uint32_t now){
 SignalValue r;int candidate=-1;ValueField field=VF_RPM;
 switch(id){
 case Signal::Iat:candidate=1;break;case Signal::Maf:candidate=3;break;case Signal::Load:candidate=2;break;
 case Signal::Coolant:r.value=s.coolantC;field=VF_COOLANT;break;
 case Signal::Throttle:r.value=s.throttle;field=VF_THROTTLE;break;
 case Signal::Rpm:r.value=s.rpm;break;case Signal::Speed:r.value=s.speed;field=VF_SPEED;break;
 case Signal::Voltage:r.value=s.volts;field=VF_VOLTS;break;
 case Signal::OilTemp:r.value=s.oilTempC;field=VF_OIL_TEMP;break;
 case Signal::OilPressure:r.value=s.oilPressureBar;field=VF_OIL_PRESSURE;break;
 case Signal::Afr:r.value=s.afr;field=VF_AFR;break;default:return r;
 }
 if(candidate>=0){r.value=s.candidateValues[candidate];r.status=candidateStatus(s,candidate,now);}
 else r.status=fieldStatus(s,field,now);
 if(!r.fresh())r.value=NAN;
 return r;
}
// Fixed v1 layout: five bottom slots. Hero/curve/shift lights remain RPM.
static Signal kThemeSlots[5]={Signal::Iat,Signal::Coolant,Signal::Maf,Signal::Load,Signal::Throttle};
}
