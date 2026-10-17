#include <cstdio>
#include "Arduino.h"
#include "ui/ui_ili9488.h"
uint32_t testMillis=0;using namespace obd;
int main(){Ili9488 panel;panel.begin();UiTft ui;ui.attach(&panel);VehicleSnapshot s{};s.settings.bootAnimation=1;ui.settings(s.settings);s.rpm=800;s.coolantC=85;s.linkUp=true;s.validMask=255;
for(unsigned t=0;t<=4000;t+=16){testMillis=10000+t;for(auto& ms:s.valueMs)ms=testMillis;ui.render(s,800,0,testMillis);uint32_t h=2166136261u;for(auto p:testPanel)h=(h^p)*16777619u;printf("%u %08x\n",t,h);}
}
