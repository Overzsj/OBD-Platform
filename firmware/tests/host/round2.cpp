#include <cstdio>
#include <cstdlib>
#include "Arduino.h"
#include "app/device_storage.h"
uint32_t testMillis=0;static int checks=0;
#define CHECK(x) do{++checks;if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
using namespace obd;
int main(){
    DeviceSettings s;CHECK(validSettings(s));
    for(const char* bad:{"100 6600 100 108 0 1 1","6200 6200 100 108 0 1 1","6200 6600 108 100 0 1 1","6200 6600 100 108 2 1 1","6200 6600 100 108 0 1 4","6200 6600 100 108 0 1","6200 6600 100 108 0 1 1 extra","-1 6600 100 108 0 1 1","65536 6600 100 108 0 1 1"}){
        DeviceSettings before=s;CHECK(!parseSettings(bad,s));CHECK(!memcmp(&before,&s,sizeof(s)));
    }
    CHECK(parseSettings("6000 6500 99 107 1 0 3",s));
    CHECK(!parseSettings("4294973496 6600 100 108 0 1 1",s));
    CHECK(!parseSettings("999999999999999999999999 6600 100 108 0 1 1",s));
    CHECK(configuredAlarm(ALARM_RPM,s).warn==6000);CHECK(configuredAlarm(ALARM_COOLANT,s).crit==107);
    SettingsRecord record;record.value=s;record.seal();CHECK(record.valid());record.value.muted^=1;CHECK(!record.valid());
    DeviceStorage store;store.begin();CHECK(store.boot==1);CHECK(store.save(s));
    unsigned writes=testWrites;CHECK(store.save(s));CHECK(testWrites==writes);
    DeviceStorage reboot;reboot.begin();CHECK(reboot.boot==2);CHECK(reboot.settings.rpmWarn==6000);CHECK(reboot.settings.muted==1);
    auto next=s;next.tempWarn=98;testWriteFailure=true;CHECK(!reboot.save(next));CHECK(reboot.settings.tempWarn==99);CHECK(reboot.writeFailed);testWriteFailure=false;
    testNvs["settings"][0]^=1;DeviceStorage corrupt;corrupt.begin();CHECK(corrupt.settingsRecovered);CHECK(corrupt.settings.rpmWarn==5000);
    CHECK(corrupt.save(DeviceSettings{}));CHECK(!corrupt.settingsRecovered);
    DeviceStorage repaired;repaired.begin();CHECK(!repaired.settingsRecovered);CHECK(repaired.settings.rpmWarn==5000);
    CHECK(repaired.presentation.layout==0&&repaired.presentation.sounds==7);
    PresentationOptions options;options.layout=1;options.sounds=2;CHECK(repaired.savePresentation(options));
    DeviceStorage layoutReboot;layoutReboot.begin();CHECK(layoutReboot.presentation.layout==1&&layoutReboot.presentation.sounds==2);
    options.layout=2;CHECK(!layoutReboot.savePresentation(options));CHECK(layoutReboot.presentation.layout==1);
    options.layout=0;options.sounds=7;CHECK(layoutReboot.savePresentation(options));
    // Actual alarm hysteresis/debounce drives the event recorder.
    AlarmState alarm{};auto cfg=configuredAlarm(ALARM_COOLANT,s);
    for(unsigned t=100;t<=2500;t+=500){auto sev=evaluateAlarmAuto(cfg,alarm,110,t);store.events.sample(0,sev,110,2500,true,95,store.boot,t);}
    CHECK(store.events.running[0]);CHECK(store.events.active[0].severity==2);
    store.events.sample(0,Sev::Critical,118,3000,true,90,store.boot,3000);
    auto sev=evaluateAlarmAuto(cfg,alarm,90,3500);store.events.sample(0,sev,90,1500,true,100,store.boot,3500);
    CHECK(!store.events.running[0]);CHECK(store.events.history.count==1);CHECK(store.events.history.rows[0].peak==118);CHECK(store.events.history.rows[0].trigger==110);CHECK(store.events.dirty);
    writes=testWrites;store.tick(59999);CHECK(testWrites==writes);store.tick(60000);CHECK(!store.events.dirty);CHECK(testWrites==writes+1);
    DeviceStorage persisted;persisted.begin();CHECK(persisted.events.history.count==1);CHECK(persisted.events.history.rows[0].peak==118);
    store.events.sample(1,Sev::Warn,6300,6300,true,100,store.boot,0xffffff00u);
    store.events.sample(1,Sev::None,0,0,false,0,store.boot,0x100u);
    CHECK(store.events.history.rows[1].reason==uint8_t(EventEnd::DataLost));CHECK(store.events.history.rows[1].durationMs==512);
    for(unsigned i=0;i<16;++i){store.events.sample(0,Sev::Warn,101+i,1000,true,100,store.boot,70000+i*1000);store.events.finish(0,70500+i*1000,EventEnd::SettingsChanged);}
    CHECK(store.events.history.count==12);CHECK(store.events.history.rows[11].peak==116);CHECK(store.events.history.rows[11].reason==2);
    store.events.history.seal();CHECK(store.events.history.valid());store.events.history.count=13;store.events.history.seal();CHECK(!store.events.history.valid());store.events.history.count=12;
    testWriteFailure=true;store.tick(120000);CHECK(store.events.dirty);CHECK(store.writeFailed);testWriteFailure=false;store.tick(180000);CHECK(!store.events.dirty);
    printf("PASS %d round2 assertions\n",checks);
}
