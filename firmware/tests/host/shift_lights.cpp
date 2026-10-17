#include <cstdio>
#include <cstdlib>
#include <limits>
#include "Arduino.h"
#include "ui/ui_ili9488.h"
uint32_t testMillis=0;static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
using namespace obd;
uint32_t bodyHash(){return recordHash(testPanel+480*32,480*288*sizeof(uint16_t));}
int main() {
    CHECK(ShiftLights::frequency(6199,6200,6600)==0);
    CHECK(ShiftLights::frequency(6200,6200,6600)==2);
    CHECK(ShiftLights::frequency(6400,6200,6600)==3);
    CHECK(ShiftLights::frequency(6600,6200,6600)==4);
    CHECK(ShiftLights::frequency(6800,6200,6600)==4.5f);
    CHECK(ShiftLights::frequency(7400,6200,6600)==6);
    CHECK(ShiftLights::frequency(9999,6200,6600)==6);
    CHECK(ShiftLights::frequency(std::numeric_limits<float>::quiet_NaN(),6200,6600)==0);
    ShiftLights clock;clock.tick(6200,0,6200,6600);
    CHECK(clock.alpha(0,6200,true,6200)==255);
    clock.tick(6200,16,6200,6600);const auto before=clock.alpha(0,6200,true,6200);
    clock.tick(6800,16,6200,6600);CHECK(clock.alpha(0,6800,true,6200)==before); // No phase jump on rate change.
    CHECK(clock.alpha(0,6800,false,6200)==38);
    CHECK(clock.alpha(0,std::numeric_limits<float>::infinity(),true,6200)==38);
    ShiftLights wrap;wrap.tick(6800,0xfffffff0u,6200,6600);wrap.tick(6800,16,6200,6600);
    ShiftLights normal;normal.tick(6800,0,6200,6600);normal.tick(6800,32,6200,6600);
    CHECK(wrap.alpha(0,6800,true,6200)==normal.alpha(0,6800,true,6200));
    unsigned counts[3];float rpms[]={6200,6400,6800};
    for(int rate=0;rate<3;++rate){ShiftLights lights;for(uint32_t t=0;t<=2000;t+=4)lights.tick(rpms[rate],t,6200,6600);counts[rate]=lights.transitions();}
    CHECK(counts[0]==8);CHECK(counts[1]==12);CHECK(counts[2]==18);
    ShiftLights ramp;ramp.tick(2000,0,6200,6600);
    CHECK(ramp.alpha(0,920,true,6200)==38);CHECK(ramp.alpha(0,1010,true,6200)>38);CHECK(ramp.alpha(0,1010,true,6200)<255);CHECK(ramp.alpha(0,1100,true,6200)==255);
    Ili9488 panel;UiTft ui;panel.begin();ui.attach(&panel);VehicleSnapshot s{};
    s.settings.bootAnimation=0;s.settings.muted=1;ui.settings(s.settings);s.linkUp=true;s.validMask=255;s.rpm=6800;
    testMillis=10000;for(auto& t:s.valueMs)t=testMillis;
    ui.tickFlash(s.rpm,testMillis);ui.render(s,s.rpm,0,testMillis);const auto body=bodyHash(),initialFrames=ui.frames(DisplayPage::Main),bytes=panel.bytesWritten();
    uint32_t hashes[2]{};
    for(int i=0;i<125;++i){testMillis=10000+i*16;s.valueMs[VF_RPM]=testMillis;ui.tickFlash(s.rpm,testMillis);ui.renderShiftLights(s,s.rpm,testMillis);CHECK(bodyHash()==body);if(i<2)hashes[i]=recordHash(testPanel,480*32*sizeof(uint16_t));}
    CHECK(hashes[0]!=hashes[1]);CHECK(ui.lightFrames()>110);CHECK(ui.frames(DisplayPage::Main)==initialFrames);
    CHECK(panel.bytesWritten()>bytes);
    CHECK(panel.bytesWritten()-bytes<=ui.lightFrames()*15*16*16*3); // Changed lamps only; no unchanged gaps.
    auto frames=ui.lightFrames();ui.renderShiftLights(s,s.rpm,testMillis+1);CHECK(ui.lightFrames()==frames);
    s.linkUp=false;s.validMask=0;testMillis+=16;ui.tickFlash(0,testMillis);ui.renderShiftLights(s,0,testMillis);
    const auto dark=recordHash(testPanel,480*32*sizeof(uint16_t));frames=ui.lightFrames();
    testMillis+=100;ui.renderShiftLights(s,6800,testMillis);CHECK(ui.lightFrames()==frames);CHECK(recordHash(testPanel,480*32*sizeof(uint16_t))==dark);
    ui.page(DisplayPage::Data);testMillis+=100;ui.renderShiftLights(s,6800,testMillis);CHECK(ui.lightFrames()==frames);
    ui.page(DisplayPage::Main);ui.renderShiftLights(s,6800,testMillis);CHECK(ui.lightFrames()==frames); // Must wait for a full main page.
    printf("PASS %u shift-light assertions: independent strip rendering, 2/3/4.5Hz, rollover, stale-data guard\n",checks);
}
