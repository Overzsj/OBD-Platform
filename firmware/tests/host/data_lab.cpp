#include <cstdio>
#include <cstdlib>
#include "Arduino.h"
#include "test/fake_elm.h"
#include "app/telemetry_pipeline.h"
#include "app/device_storage.h"
#include "obd/link_recovery.h"
#include "ui/tft_canvas.h"
#include "ui/data_lab_theme.h"
uint32_t testMillis=0;static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){printf("FAIL line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
using namespace obd;
void preview(const VehicleSnapshot& s,uint32_t now,uint8_t stage) {
    char path[100];snprintf(path,sizeof(path),"tests/artifacts/lab-%02u.ppm",stage);
    FILE* f=fopen(path,"wb");CHECK(f);fprintf(f,"P6\n480 320\n255\n");
    uint16_t pixels[480*16];TftCanvas canvas;
    for(int y=0;y<320;y+=16) {
        canvas.attach(pixels,y);drawDataLab(canvas,s,now);
        for(auto p:pixels){unsigned char rgb[]={uint8_t(((p>>11)&31)*255/31),uint8_t(((p>>5)&63)*255/63),uint8_t((p&31)*255/31)};fwrite(rgb,1,3,f);}
    }
    fclose(f);
}
int main() {
    for(char ch: {'Q','Z'}) {bool found=false;for(size_t i=0;i<kFont5x7Count;++i)found|=kFont5x7[i].ch==ch;CHECK(found);}
    FakeElmIo io;ElmClient elm;PidScheduler scheduler;LinkRecovery recovery;DeviceStorage storage;
    VehicleSnapshot s{};AlarmState alarms[ALARM_COUNT]{};ElmState last=ElmState::Idle;
    CHECK(storage.settings.muted==1);CHECK(storage.settings.bootAnimation==0);
    CHECK(profilePidEnabled(0x42));CHECK(profilePidEnabled(0xF0));CHECK(profilePidEnabled(0xF1));
    io.begin();elm.begin();scheduler.begin();storage.begin();
    uint8_t freshSeen[11]{},alarmSeen[11]{};bool rendered[11]{},waterLost=false,silentExpired=false,linkDown=false,zeroValid=false,restored=false;
    uint32_t reinits=0;
    for(testMillis=5;testMillis<kLabCycleMs*2;testMillis+=5) {
        const uint32_t now=testMillis;io.tick(now);elm.tick(io,now);auto cur=elm.state();
        if(cur!=last) {if(cur==ElmState::Ready){scheduler.begin();elm.enableBatchProbe(3);recovery.sessionStarted(now);}last=cur;}
        if(io.isConnected()) {
            while(io.available()){uint8_t b=io.read();elm.feed(&b,1);}
            s.linkUp=elm.state()==ElmState::Ready;if(!s.linkUp)s.validMask=0;
            elm.poll(io,scheduler,now);
            if(elm.replyReady(now)) {
                ParsedPid parsed[8];auto n=elm.consumeReply(now,scheduler,parsed,8);uint8_t fresh=0;
                if(n){s.extraCount=0;acceptParsed(parsed,n,s,fresh,now);s.lastOkMs=now;}
                else ++s.totalFail;
                recovery.observe(n>0,elm.lastReplyHadPrompt(),now);evaluateSnapshot(s,alarms,storage.settings,fresh,now);
            }
            if(recovery.check(now,s.linkUp,elm.awaiting(),false)!=RecoveryAction::None) {
                ++reinits;s.linkUp=false;s.validMask=0;memset(alarms,0,sizeof(alarms));elm.requestReinit();
            }
        } else {s.linkUp=false;s.validMask=0;memset(alarms,0,sizeof(alarms));}
        expireAlarms(s,alarms,now);
        storage.events.sample(0,alarms[0].sev,s.coolantC,s.rpm,valueFresh(s,VF_COOLANT,now),100,storage.boot,now);
        storage.events.sample(1,alarms[1].sev,s.rpm,s.rpm,valueFresh(s,VF_RPM,now),100,storage.boot,now);storage.tick(now);
        s.settings=storage.settings;s.testStage=io.labStage(now);s.testPhaseMs=io.labPhaseMs(now);s.testCycle=io.labCycle(now);s.eventCount=storage.events.history.count;s.linkQuality=s.linkUp?100:0;
        uint8_t fresh=0;for(uint8_t f=0;f<VF_COUNT;++f)if(valueFresh(s,ValueField(f),now))fresh|=1u<<f;
        freshSeen[s.testStage]|=fresh;alarmSeen[s.testStage]|=s.alarmFlags;
        if(s.testPhaseMs>9000) {
            if(s.testStage==6)waterLost|=!valueFresh(s,VF_COOLANT,now)&&valueFresh(s,VF_RPM,now)&&!(s.alarmFlags&1);
            if(s.testStage==7)silentExpired|=fresh==0;
            if(s.testStage==8)linkDown|=!s.linkUp&&s.validMask==0&&s.alarmFlags==0;
            if(s.testStage==9)restored|=fresh==255&&s.alarmFlags==0;
            if(s.testStage==10)zeroValid|=fresh==255&&s.rpm==0&&s.speed==0&&s.oilPressureBar==0&&s.coolantC==-40;
            if(!rendered[s.testStage]){preview(s,now,s.testStage);rendered[s.testStage]=true;}
        }
    }
    for(unsigned i=0;i<kLabStages;++i){CHECK(rendered[i]);printf("STAGE %02u %-12s fresh=%02X alarm=%02X\n",i,labStageName(i),freshSeen[i],alarmSeen[i]);}
    CHECK(freshSeen[0]==255);CHECK((alarmSeen[2]&7)==7);CHECK(alarmSeen[3]==255);
    CHECK(waterLost);CHECK(silentExpired);CHECK(linkDown);CHECK(restored);CHECK(zeroValid);CHECK(reinits>0);
    CHECK(storage.events.history.count>=6);
    bool lossEvent=false;for(unsigned i=0;i<storage.events.history.count;++i)lossEvent|=storage.events.history.rows[i].reason==uint8_t(EventEnd::DataLost);
    CHECK(lossEvent);DeviceStorage reboot;reboot.begin();CHECK(reboot.events.history.count>=3);
    printf("PASS %u Data Lab assertions; two complete protocol cycles (%lu simulated ms), reinits=%lu, events=%lu\n",checks,(unsigned long)kLabCycleMs*2,(unsigned long)reinits,(unsigned long)storage.events.history.count);
}
