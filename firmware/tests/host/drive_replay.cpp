#include <cstdio>
#include <cstdlib>
#include "Arduino.h"
#include "test/drive_replay.h"
uint32_t testMillis=0;
#define CHECK(x) do{if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
using namespace obd;
int main(){
 DriveReplay replay;VehicleSnapshot s{};AlarmState alarms[ALARM_COUNT]{};
 replay.begin(100);replay.apply(s,alarms,100);CHECK(replay.index()==0);
 replay.pause(600);const auto p=replay.position(600);CHECK(replay.position(5000)==p);
 replay.apply(s,alarms,5000);CHECK(s.alarmFlags==0);
 replay.play(5000);CHECK(replay.position(5100)==p+100);
 replay.play(5100);CHECK(replay.position(5200)==p+200); // Already-playing command is idempotent.
 replay.begin(9000);CHECK(replay.position(9000)==0);
 for(uint32_t i=0;i<kReplayCount;++i){
  const auto& row=kReplayRows[i];replay.apply(s,alarms,9000+row.ms);
  CHECK(replay.index()==i);CHECK(s.validMask==row.fresh);
  CHECK(s.linkUp==bool(row.state&64));
  CHECK(s.rpm==row.values[0]);CHECK(s.volts==row.values[7]);
  CHECK(!candidateFresh(s,0,9000+row.ms));
  for(uint8_t f=0;f<VF_COUNT;++f)if(!(row.fresh&(1u<<f)))CHECK(!valueFresh(s,ValueField(f),9000+row.ms));
 }
 replay.apply(s,alarms,9000+kReplayDuration);CHECK(replay.paused());CHECK(s.alarmFlags==0);
 replay.begin(0xfffffff0u);CHECK(replay.position(0x10)==32);
 printf("PASS replay: %lu records, original masks/timestamps, pause/resume/end, rollover, missing candidate guard\n",(unsigned long)kReplayCount);
}
