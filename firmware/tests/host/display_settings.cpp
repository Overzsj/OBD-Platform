#include <cstdio>
#include <cstdlib>
#include "Arduino.h"
#include "app/device_storage.h"
#include "app/telemetry_signals.h"
#include "ui/shift_lights.h"
uint32_t testMillis=0;unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){printf("FAIL %d %s\n",__LINE__,#x);exit(1);}}while(0)
using namespace obd;
int main(){
 DisplaySettings d;CHECK(d.valid());DisplaySettings next;
 CHECK(parseDisplay("120 120 200 100 100 8000 240 16 150 10 25",next));CHECK(next.maximum[5]==8000);
 for(const char* bad:{"120 120","120 120 200 100 100 8000 240 16 150 10 25 extra","0 120 200 100 100 7000 240 16 150 10 25","120 120 200 101 100 7000 240 16 150 10 25","120 120 200 100 100 99999999 240 16 150 10 25"})CHECK(!parseDisplay(bad,d));
 CHECK(d.maximum[5]==7000);DisplayRecord record;record.value=next;record.seal();CHECK(record.valid());record.value.maximum[5]++;CHECK(!record.valid());
 DeviceStorage store;store.begin();CHECK(store.saveDisplay(next));DeviceStorage reboot;reboot.begin();CHECK(reboot.display.maximum[5]==8000);
 testWriteFailure=true;d.maximum[5]=9000;CHECK(!reboot.saveDisplay(d));CHECK(reboot.display.maximum[5]==8000);testWriteFailure=false;
 VehicleSnapshot s{};s.rpm=5500;s.linkUp=true;s.transportState=1;s.validMask=1;s.valueMs[VF_RPM]=testMillis;s.display=next;
 CHECK(signalValue(s,Signal::Rpm,testMillis).value==5500);CHECK(s.settings.rpmWarn==5000&&s.settings.rpmCrit==6000);
 ShiftLights lights;CHECK(lights.alpha(8,3000,true,5000,8000)<lights.alpha(8,3000,true,5000,4000));
 DeviceSettings alarms;CHECK(parseSettings("7500 8500 100 108 0 1 1",alarms));CHECK(alarms.rpmCrit==8500);
 printf("PASS %u display settings assertions: bounds, persistence, failed write, signal/alarm separation and light scaling\n",checks);
}
