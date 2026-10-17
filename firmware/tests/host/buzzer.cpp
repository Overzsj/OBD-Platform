#include <cstdio>
#include <cstdlib>
#include "Arduino.h"
inline void ledcSetup(int,int,int){}
inline void ledcAttachPin(int,int){}
inline void ledcDetachPin(int){}
inline void ledcWriteTone(int,int){}
#include "ui/buzzer.h"
#include "app/telemetry_pipeline.h"
#include "test/data_script.h"
uint32_t testMillis=0;static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
using namespace obd;
int main(){
 VehicleSnapshot soundOptions{};soundOptions.alarmFlags=0x33;soundOptions.presentation.sounds=7;CHECK(audibleSnapshotFlags(soundOptions)==0x33);
 soundOptions.presentation.sounds=2;CHECK(audibleSnapshotFlags(soundOptions)==0x22);soundOptions.presentation.sounds=4;CHECK(audibleSnapshotFlags(soundOptions)==0x11);
 soundOptions.presentation.sounds=0;CHECK(audibleSnapshotFlags(soundOptions)==0);CHECK(soundOptions.alarmFlags==0x33);
 CHECK(DeviceSettings{}.rpmWarn==5000);CHECK(DeviceSettings{}.rpmCrit==6000);
 for(uint8_t stage=11;stage<=12;++stage){const auto sample=labSample(stage,0);AlarmState st[ALARM_COUNT]{};uint8_t flags=0;
  for(uint32_t now=100;now<=1000;now+=100){evaluateAlarmAuto(kDefaultAlarms[ALARM_RPM],st[ALARM_RPM],sample.rpm,now);evaluateAlarmAuto(kDefaultAlarms[ALARM_COOLANT],st[ALARM_COOLANT],sample.water,now);flags=packAlarmFlags(st);}
  CHECK(flags==(stage==11?2:34));
 }
 AlarmState boundary{};for(uint32_t t=100;t<=600;t+=100)evaluateAlarmAuto(kDefaultAlarms[ALARM_RPM],boundary,4999,t);CHECK(!boundary.active);
 for(uint32_t t=700;t<=1200;t+=100)evaluateAlarmAuto(kDefaultAlarms[ALARM_RPM],boundary,5000,t);CHECK(boundary.active&&boundary.sev==Sev::Warn);
 Buzzer b;b.begin();CHECK(!b.passive()&&b.kPin==25&&!b.sounding());
 testMillis=100;b.selfTest();CHECK(testMillis==100); // No delay in startup path.
 for(uint32_t t=0;t<600;++t){b.tick(0,false,100+t);const bool expected=t<60||(t>=140&&t<200)||(t>=280&&t<460);CHECK(b.sounding()==expected);}
 CHECK(b.previewMode()==0);
 b.preview(2,1000);
 for(uint32_t t=0;t<2160;++t){b.tick(0,false,1000+t);const uint32_t phase=t%180;CHECK(b.sounding()==(phase<70));}
 b.tick(0,false,3160);CHECK(!b.sounding()&&b.previewMode()==0);
 b.setCritLevel(1);b.preview(3,5000);
 for(uint32_t t=0;t<1500;++t){b.tick(0,false,5000+t);CHECK(b.sounding()==(t%80<50));}
 b.tick(0,false,6500);CHECK(!b.sounding());
 b.preview(3,7000);b.tick(0,false,7000);CHECK(b.sounding());b.preview(0,7001);b.tick(0,false,7001);CHECK(!b.sounding());
 // Actual alarms override previews and restart their first pulse immediately.
 b.preview(1,8000);b.tick(1<<ALARM_RPM,true,8123);CHECK(b.sounding()&&b.previewMode()==0);
 b.tick(1<<ALARM_RPM,true,8203);CHECK(!b.sounding());
 b.tick((1<<ALARM_RPM)|(1<<(ALARM_RPM+4)),true,8230);CHECK(b.sounding());
 b.tick(1<<(ALARM_RPM+4),true,8280);CHECK(!b.sounding()); // critical bit alone also works
 b.tick(0,false,8320);CHECK(!b.sounding());
 b.preview(1,9000);b.setMuted(true);b.tick(0,false,9000);CHECK(!b.sounding()&&b.previewMode()==0);
 b.setMuted(false);b.tick(0,false,9001);CHECK(!b.sounding());
 b.preview(1,0xfffffff0u);b.tick(0,false,0x10);CHECK(b.sounding());b.tick(0,false,0x230);CHECK(!b.sounding());
 // Real sample -> configured alarm debounce -> buzzer; no synthetic snapshot writes.
 VehicleSnapshot s{};s.linkUp=true;DeviceSettings settings;AlarmState alarms[ALARM_COUNT]{};ParsedPid rpm{};rpm.pid=0x0C;rpm.value=6300;
 for(uint32_t now=10000;now<=10300;now+=100){uint8_t fresh=0;acceptParsed(&rpm,1,s,fresh,now);evaluateSnapshot(s,alarms,settings,fresh,now);b.tick(s.alarmFlags,s.linkUp,now);}
 CHECK((s.alarmFlags&(1<<ALARM_RPM))!=0);
 rpm.value=6700;uint8_t fresh=0;acceptParsed(&rpm,1,s,fresh,10400);evaluateSnapshot(s,alarms,settings,fresh,10400);b.tick(s.alarmFlags,true,10400);CHECK(b.sounding()&&(s.alarmFlags&(1<<(ALARM_RPM+4))));
 const auto saved=s;b.preview(2,11000);b.tick(0,false,11000);CHECK(!memcmp(&s,&saved,sizeof(s)));
 printf("PASS %u buzzer assertions: startup/RPM/redline timing, nonblocking, mute, priority, rollover and real alarm pipeline\n",checks);
}
