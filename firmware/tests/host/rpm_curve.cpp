#include <cstdio>
#include <cstdlib>
#include <limits>
#include "Arduino.h"
#include "ui/ui_ili9488.h"
uint32_t testMillis=0;static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
using namespace obd;
int main(){
 float cruise[]={2000,2200,2600,2400};auto scale=RpmCurveScale::from(cruise,4);
 CHECK(scale.low<=2000 && scale.high>=2600);CHECK(scale.high-scale.low>=1500);CHECK(scale.y(2000)-scale.y(2600)>=6);
 float edge[]={0,850,6800,9999};scale=RpmCurveScale::from(edge,4);CHECK(scale.low==0);CHECK(scale.high>=9999);
 CHECK(scale.y(-500)==207);CHECK(scale.y(20000)==186);
 float missing[]={-1,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()};scale=RpmCurveScale::from(missing,3);CHECK(scale.low==0 && scale.high==2000);
 for(int base=0;base<=8500;base+=100){float v[]={float(base),float(base+500)};scale=RpmCurveScale::from(v,2);CHECK(scale.low<=v[0]);CHECK(scale.high>=v[1]);CHECK(scale.y(v[0])>=scale.y(v[1]));}
 Ili9488 panel;panel.begin();UiTft ui;ui.attach(&panel);VehicleSnapshot s{};s.settings.bootAnimation=0;ui.settings(s.settings);s.linkUp=true;s.validMask=255;
 for(int i=0;i<60;++i){testMillis=10000+i*100;for(auto& t:s.valueMs)t=testMillis;s.rpm=2400+400*std::sin(i*.24);ui.render(s,s.rpm,0,testMillis);}
 FILE* f=fopen("tests/artifacts/rpm-curve.ppm","wb");CHECK(f);fprintf(f,"P6\n480 320\n255\n");for(auto p:testPanel){uint8_t rgb[]={uint8_t((p>>11)*255/31),uint8_t(((p>>5)&63)*255/63),uint8_t((p&31)*255/31)};fwrite(rgb,1,3,f);}fclose(f);
 printf("PASS %u RPM curve assertions\n",checks);
}
