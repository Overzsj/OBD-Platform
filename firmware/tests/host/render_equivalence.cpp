#include <cstdio>
#include "Arduino.h"
#include "ui/ui_ili9488.h"
uint32_t testMillis=0;
using namespace obd;
int main(){Ili9488 display;display.begin();UiTft ui;ui.attach(&display);DeviceSettings settings{};settings.bootAnimation=0;ui.settings(settings);VehicleSnapshot s{};s.settings=settings;s.linkUp=true;s.validMask=255;
for(unsigned f=0;f<80;++f){testMillis=1000+f*75;for(auto& ms:s.valueMs)ms=testMillis;for(auto& ms:s.candidateMs)ms=testMillis;
 s.presentation.layout=f<40?0:1;s.rpm=1000+f*90;s.coolantC=85;s.throttle=(f%25)*4;s.candidateValues[1]=20+f%15;s.candidateValues[2]=f%100;s.candidateValues[3]=f*1.5f;
 for(auto& state:s.candidateStates)state=uint8_t(ProbeState::Valid);
 if(f>=30&&f<35)s.candidateStates[1]=uint8_t(ProbeState::Timeout);
 if(f>=50&&f<55)s.linkUp=false;else s.linkUp=true;
 ui.render(s,s.rpm,0,testMillis);ui.renderShiftLights(s,s.rpm,testMillis+20);uint32_t hash=2166136261u;for(auto pixel:testPanel)hash=(hash^pixel)*16777619u;printf("%u %08x\n",f,hash);
} }
