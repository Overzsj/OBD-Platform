#include <cstdio>
#include "Arduino.h"
#include "ui/ui_ili9488.h"
#include "bt/memory_budget.h"
uint32_t testMillis=0;
using namespace obd;
int main(){
 if(btConnectionMemoryReady(kBtFreeReserve-1,kBtLargestReserve) || btConnectionMemoryReady(kBtFreeReserve,kBtLargestReserve-1) || !btConnectionMemoryReady(kBtFreeReserve,kBtLargestReserve))return 1;
 Ili9488 panel;panel.begin();UiTft ui;ui.attach(&panel);if(ui.heroCached()!=bool(TFT_HERO_CACHE))return 2;
 VehicleSnapshot s{};s.settings.bootAnimation=0;ui.settings(s.settings);s.linkUp=true;s.validMask=255;
 float values[]={0,9,10,99,100,999,1000,9999,850,6350,6800,2400};
 for(int i=0;i<144;++i){testMillis=10000+i*100;for(auto& t:s.valueMs)t=testMillis;s.rpm=values[i%12];s.validMask=i%19?255:0;s.alarmFlags=s.rpm>=6200?2:0;ui.tickFlash(s.rpm,testMillis);ui.render(s,s.rpm,0,testMillis);printf("%d %08x\n",i,recordHash(testPanel,sizeof(testPanel)));}
}
