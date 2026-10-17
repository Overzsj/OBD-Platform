#include <cstdio>
#include <cstdlib>
#include <limits>
#include "Arduino.h"
#include "ui/ui_ili9488.h"
uint32_t testMillis=0;static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
using namespace obd;
void save(const char* name){FILE* f=fopen(name,"wb");CHECK(f);fprintf(f,"P6\n480 320\n255\n");for(auto p:testPanel){uint8_t rgb[]={uint8_t((p>>11)*255/31),uint8_t(((p>>5)&63)*255/63),uint8_t((p&31)*255/31)};fwrite(rgb,1,3,f);}fclose(f);}
uint32_t faceHash(){uint32_t h=0;for(int y=117;y<166;++y)h^=recordHash(testPanel+y*480+336,200)+y;return h;}
int main(){
 CHECK(TFT_CHARACTER_ANIMATION==1);
 CHECK(gaugeCharacterPose(1000,850,true,6200,false).effort==0);
 CHECK(gaugeCharacterPose(1000,5600,true,6200,false).effort==0);
 auto mid=gaugeCharacterPose(1000,5900,true,6200,false).effort;CHECK(mid>0 && mid<255);
 CHECK(gaugeCharacterPose(1000,6200,true,6200,false).effort==255);
 CHECK(gaugeCharacterPose(1000,6800,true,6200,true).effort==255);
 CHECK(gaugeCharacterPose(1000,5900,true,6200,true).effort==255);
 CHECK(gaugeCharacterPose(1000,6800,false,6200,true).effort==0);
 CHECK(gaugeCharacterPose(1000,std::numeric_limits<float>::quiet_NaN(),true,6200,true).effort==0);
 CHECK(gaugeCharacterPose(1000,5000,true,5000,false).effort==255);
 CHECK(gaugeCharacterPose(1000,5000,true,6200,false).effort==0);
 auto a=gaugeCharacterPose(1000,850,true,6200,false),b=gaugeCharacterPose(1300,850,true,6200,false);CHECK(a.sway!=b.sway || a.nod!=b.nod || a.hair!=b.hair);
 Ili9488 display;display.begin();UiTft ui;ui.attach(&display);VehicleSnapshot s{};s.settings.bootAnimation=0;ui.settings(s.settings);s.linkUp=true;s.validMask=255;
 testMillis=10000;for(auto& t:s.valueMs)t=testMillis;s.rpm=850;ui.render(s,850,0,testMillis);auto calm=faceHash();save("tests/artifacts/character-calm.ppm");
 s.rpm=6800;s.alarmFlags=(1u<<ALARM_RPM);ui.render(s,6800,0,testMillis);CHECK(faceHash()!=calm);save("tests/artifacts/character-alert.ppm");
 s.rpm=850;s.alarmFlags=0;ui.render(s,850,0,testMillis);CHECK(faceHash()==calm);
 s.rpm=6800;s.validMask=0;s.alarmFlags=(1u<<ALARM_RPM);ui.render(s,6800,0,testMillis);CHECK(faceHash()==calm);
 printf("PASS %u character assertions\n",checks);
}
