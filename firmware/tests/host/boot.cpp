#include <cstdio>
#include <cstdlib>
#include "Arduino.h"
#include "ui/ui_ili9488.h"
uint32_t testMillis=0;static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
using namespace obd;
int main(){
    Ili9488 panel;UiTft ui;panel.begin();ui.attach(&panel);VehicleSnapshot s{};s.settings.bootAnimation=1;ui.settings(s.settings);
    uint16_t pixels[480*16];TftCanvas expected;
    for(uint32_t t=0;t<3400;t+=16){testMillis=10000+t;ui.render(s,0,0,testMillis);
        for(int y=0;y<320;y+=16){expected.attach(pixels,y);BootAnimation::draw(expected,t);if(memcmp(testPanel+y*480,pixels,sizeof(pixels))){for(int i=0;i<480*16;++i)if(testPanel[y*480+i]!=pixels[i]){printf("DIFF t=%u x=%u y=%u actual=%04x expected=%04x\n",t,i%480,y+i/480,testPanel[y*480+i],pixels[i]);break;}}CHECK(!memcmp(testPanel+y*480,pixels,sizeof(pixels)));}
    }
    CHECK(ui.bootAnimating());testMillis=14000;ui.render(s,0,0,testMillis);CHECK(!ui.bootAnimating());CHECK(ui.bootFrames()>0);
    printf("PASS %u boot assertions: incremental animation pixels match full repaint at every 16ms timestamp\n",checks);
}
