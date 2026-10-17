#pragma once
#include "vehicle_state.h"
#include <math.h>
namespace obd {
struct DriveRecord {
    uint32_t seq=0,boot=0,uptimeMs=0,heap=0,ok=0,fail=0;
    float values[8]{};
    uint8_t alarms=0,fresh=0,quality=0,state=0,adapter=0,reason=0;
    uint16_t crc=0;
    uint16_t hash() const {uint16_t h=0xffff;const auto* bytes=reinterpret_cast<const uint8_t*>(this);for(size_t i=0;i<offsetof(DriveRecord,crc);++i){h^=uint16_t(bytes[i])<<8;for(int bit=0;bit<8;++bit)h=(h&0x8000)?uint16_t((h<<1)^0x1021):uint16_t(h<<1);}return h;}
    void seal(){crc=hash();}
    bool valid() const {return seq&&crc==hash();}
};
static_assert(sizeof(DriveRecord)==64,"Journal records must remain fixed and versioned");
// New files only. The original 64-byte record remains readable and untouched.
struct DriveRecordV2:DriveRecord {
    float candidates[3]{}; // IAT / MAF / calculated load
    uint8_t candidateMask=0,version=2;uint16_t extendedCrc=0;
    uint16_t hashExtended()const {
        uint16_t h=0xffff;const auto* p=reinterpret_cast<const uint8_t*>(this);
        for(size_t i=0;i<sizeof(*this)-2;++i){h^=uint16_t(p[i])<<8;for(int b=0;b<8;++b)h=(h&0x8000)?uint16_t((h<<1)^0x1021):uint16_t(h<<1);}
        return h;
    }
    void sealV2(){seal();extendedCrc=hashExtended();}
    bool validV2()const{return version==2&&candidateMask<=7&&valid()&&extendedCrc==hashExtended();}
};
static_assert(sizeof(DriveRecordV2)==80,"V2 log layout");
inline uint8_t driveAdapterState(const char* text){
    if(strstr(text,"UNAVAILABLE"))return 10;
    if(strstr(text,"NO_MEMORY"))return 9;if(strstr(text,"OVERFLOW"))return 7;if(strstr(text,"NO_ELM"))return 8;
    if(strstr(text,"NEEDS"))return 6;if(strstr(text,"NOT_FOUND")||strstr(text,"FAILED"))return 5;
    if(strstr(text,"SCANNING"))return 1;if(strstr(text,"CONNECTING")||strstr(text,"JOINING"))return 2;
    if(strstr(text,"CONNECTED"))return 3;if(strstr(text,"LOST"))return 4;if(strstr(text,"IDLE"))return 0;return 255;
}
inline DriveRecord driveSample(const VehicleSnapshot& s,uint32_t boot,uint32_t now,uint32_t heap,uint8_t mode,uint8_t adapter){
    DriveRecord r;r.boot=boot;r.uptimeMs=now;r.heap=heap;r.ok=s.totalOk;r.fail=s.totalFail;r.alarms=s.alarmFlags;r.quality=s.linkQuality;r.adapter=adapter;
    r.state=(mode&3)|((s.elmState&7)<<2)|(s.transportState?32:0)|(s.linkUp?64:0)|(kSimulatedSource?128:0);
    const float values[]={s.rpm,s.oilTempC,s.coolantC,s.oilPressureBar,s.afr,s.throttle,s.speed,s.volts};
    for(uint8_t i=0;i<8;++i)if(valueFresh(s,ValueField(i),now)&&isfinite(values[i])){r.fresh|=1u<<i;r.values[i]=values[i];}
    return r;
}
inline uint8_t driveReason(const DriveRecord& before,const DriveRecord& after){return (before.state!=after.state||before.adapter!=after.adapter?2:0)|(before.fresh!=after.fresh?4:0)|(before.alarms!=after.alarms?8:0);}
}
