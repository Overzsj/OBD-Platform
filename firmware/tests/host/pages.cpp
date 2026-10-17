#include <cstdio>
#include <cstdlib>
#include "Arduino.h"
#include "ui/ui_ili9488.h"
#include "app/telemetry_pipeline.h"
uint32_t testMillis=0;static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
using namespace obd;
void save(DisplayPage page,const char* suffix="") {
    char name[120];snprintf(name,sizeof(name),"tests/artifacts/pages-%s-%u%s.ppm",kSimulatedSource?"sim":"vehicle",unsigned(page),suffix);
    FILE* out=fopen(name,"wb");CHECK(out);fprintf(out,"P6\n480 320\n255\n");
    for(auto pixel:testPanel){uint8_t rgb[]={uint8_t(((pixel>>11)&31)*255/31),uint8_t(((pixel>>5)&63)*255/63),uint8_t((pixel&31)*255/31)};fwrite(rgb,1,3,out);}fclose(out);
}
int main() {
    // Exercise UTF-8 and the actual flash glyphs across 16-row transfer bands.
    const char* utf8="水A℃";CHECK(zhNext(utf8)==0x6c34);CHECK(zhNext(utf8)=='A');CHECK(zhNext(utf8)==0x2103);CHECK(!*utf8);
    const char* broken="\xe6";CHECK(zhNext(broken)=='?');CHECK(!*broken);
    CHECK(zhWidth("发动机转速")==70);CHECK(zhWidth("距更新 4294967295毫秒")==147);
    CHECK(zhWidth("链路：未连接  适配器：能力探测  数据：等待",ZhSize::Small)<=452);
    CHECK(zhWidth("电脑切页：主仪表 / 数据总览 / 设备诊断",ZhSize::Small)<400);
    uint16_t tile[480*16];TftCanvas canvas;
    for(uint8_t size=0;size<3;++size)for(uint16_t index=0;index<kZhGlyphCount;++index) {
        uint16_t code=pgm_read_word(kZhCodes+index);char glyph[4]{};
        if(code<128)glyph[0]=code;
        else if(code<2048){glyph[0]=0xc0|(code>>6);glyph[1]=0x80|(code&63);}
        else{glyph[0]=0xe0|(code>>12);glyph[1]=0x80|((code>>6)&63);glyph[2]=0x80|(code&63);}
        const auto& font=kZhFonts[size];bool matches=true;
        for(int band=0;band<48;band+=16) {
            memset(tile,0,sizeof(tile));canvas.attach(tile,band);zhText(canvas,3,13,glyph,0xffff,ZhSize(size));
            for(int y=0;y<16;++y)for(int x=0;x<font.cell;++x) {
                int row=band+y-13;bool expected=row>=0&&row<font.height&&x<(code<128?font.cell/2:font.cell)&&
                    (pgm_read_byte(font.bits+index*font.height*font.stride+row*font.stride+x/8)&(0x80>>(x%8)));
                if(bool(tile[y*480+3+x])!=expected)matches=false;
            }
        }
        CHECK(matches);
    }
    DisplayPage requested=DisplayPage::Main;
    CHECK(parsePageRequest("VIEW PREV",requested,requested));CHECK(requested==DisplayPage::Diagnostics);
    CHECK(parsePageRequest("VIEW NEXT",requested,requested));CHECK(requested==DisplayPage::Main);
    CHECK(parsePageRequest("VIEW DATA",requested,requested));CHECK(requested==DisplayPage::Data);
    auto previous=requested;CHECK(!parsePageRequest("VIEW DATA EXTRA",requested,requested));CHECK(requested==previous);
    CHECK(!parsePageRequest("VIEW 999",requested,requested));CHECK(!parsePageRequest("VIEW GET",requested,requested));
    CHECK(parsePageRequest("PAGE 1",requested,requested));CHECK(requested==DisplayPage::Diagnostics);
    CHECK(parsePageRequest("PAGE 0",requested,requested));CHECK(requested==DisplayPage::Main);
    // A parser can finish after the loop's original clock. A single observation
    // timestamp must survive expiry checks, rather than resetting debounce.
    VehicleSnapshot delayed{};delayed.linkUp=true;AlarmState delayedAlarms[ALARM_COUNT]{};DeviceSettings config;
    ParsedPid high[2]{};high[0].pid=0x0C;high[0].value=6800;high[1].pid=0x05;high[1].value=112;
    for(uint32_t observed=1000;observed<=4000;observed+=500) {
        testMillis=observed+3;uint8_t fresh=0;
        acceptParsed(high,2,delayed,fresh,observed);
        evaluateSnapshot(delayed,delayedAlarms,config,fresh,observed);
        expireAlarms(delayed,delayedAlarms,testMillis);
        CHECK(valueFresh(delayed,VF_RPM,testMillis));CHECK(valueFresh(delayed,VF_COOLANT,testMillis));
    }
    CHECK((delayed.alarmFlags&0x33)==0x33);
    Ili9488 panel;UiTft ui;panel.begin();ui.attach(&panel);
    VehicleSnapshot s{};s.settings.bootAnimation=0;s.settings.muted=1;ui.settings(s.settings);
    testMillis=10000;s.linkUp=true;s.linkQuality=100;s.transportState=1;s.elmState=4;
    s.rpm=3450;s.speed=87;s.coolantC=88;s.throttle=26;s.oilTempC=99;s.oilPressureBar=3.2f;s.afr=14.7f;s.volts=14.2f;
    s.validMask=kSimulatedSource?255:((1u<<VF_RPM)|(1u<<VF_SPEED)|(1u<<VF_COOLANT)|(1u<<VF_THROTTLE));
    for(auto& t:s.valueMs)t=testMillis;
    s.testStage=1;s.testPhaseMs=9000;s.testCycle=1;s.eventCount=3;
    uint32_t hashes[3]{};
    for(unsigned loop=0;loop<20;++loop)for(uint8_t p=0;p<kDisplayPages;++p) {
        const auto target=DisplayPage(p);ui.page(target);auto before=ui.frames(target);auto bytes=panel.bytesWritten();
        ui.render(s,s.rpm,s.speed,testMillis);CHECK(ui.page()==target);CHECK(ui.frames(target)==before+1);CHECK(panel.bytesWritten()>bytes);
        auto hash=recordHash(testPanel,sizeof(testPanel));
        if(!loop){hashes[p]=hash;save(target);}else CHECK(hashes[p]==hash);
    }
    CHECK(hashes[0]!=hashes[1]);CHECK(hashes[1]!=hashes[2]);
    ui.page(DisplayPage(255));CHECK(ui.page()==DisplayPage::Diagnostics);
    s.linkUp=false;s.validMask=0;ui.page(DisplayPage::Data);ui.render(s,s.rpm,s.speed,testMillis);
    CHECK(recordHash(testPanel,sizeof(testPanel))!=hashes[1]);
    save(DisplayPage::Data,"-offline");ui.page(DisplayPage::Diagnostics);ui.render(s,s.rpm,s.speed,testMillis);save(DisplayPage::Diagnostics,"-offline");
    s.linkUp=true;s.validMask=255;s.alarmFlags=255;s.testStage=3;s.coolantC=112;s.rpm=6800;s.volts=10.8;
    ui.page(DisplayPage::Data);ui.render(s,s.rpm,s.speed,testMillis);save(DisplayPage::Data,"-critical");
    // Explicitly simulated preview inputs, never a vehicle acquisition claim.
    testMillis+=200;
    s.linkUp=true;s.validMask=(1<<VF_RPM)|(1<<VF_SPEED)|(1<<VF_COOLANT)|(1<<VF_THROTTLE)|(1<<VF_VOLTS);s.alarmFlags=0;
    s.coolantC=48;s.rpm=965;s.speed=0;s.throttle=11;s.volts=14.575;s.oilReported=0;s.probeStates[0]=uint8_t(ProbeState::NoData);
    const float candidateDemo[]={1.0f,35.0f,22.0f,3.25f};
    for(uint8_t i=0;i<4;++i){s.candidateValues[i]=candidateDemo[i];s.candidateMs[i]=testMillis;s.candidateStates[i]=uint8_t(ProbeState::Valid);}
    ui.page(DisplayPage::Main);ui.render(s,s.rpm,s.speed,testMillis);save(DisplayPage::Main,"-intake-maf-load");
    const auto newMain=recordHash(testPanel,sizeof(testPanel));
    s.oilTempC=140;s.oilPressureBar=9;s.afr=20;ui.render(s,s.rpm,s.speed,testMillis);
    CHECK(recordHash(testPanel,sizeof(testPanel))==newMain); // Removed fields cannot dirty or change the main cards.
    s.candidateValues[1]+=5;ui.render(s,s.rpm,s.speed,testMillis);
    CHECK(recordHash(testPanel,sizeof(testPanel))!=newMain); // Fresh IAT must repaint without another main PID changing.
    for(uint8_t i=0;i<4;++i)s.candidateStates[i]=uint8_t(ProbeState::NotRun);
    ui.render(s,s.rpm,s.speed,testMillis);save(DisplayPage::Main,"-intake-maf-load-missing");
    const auto missingMain=recordHash(testPanel,sizeof(testPanel));
    s.candidateValues[1]=99;s.candidateValues[2]=99;s.candidateValues[3]=99;
    ui.render(s,s.rpm,s.speed,testMillis);CHECK(recordHash(testPanel,sizeof(testPanel))==missingMain);
    for(uint8_t i=0;i<4;++i){s.candidateValues[i]=candidateDemo[i];s.candidateStates[i]=uint8_t(ProbeState::Valid);}
    ui.render(s,s.rpm,s.speed,testMillis);const auto stableLayout=recordHash(testPanel,sizeof(testPanel));
    s.presentation.layout=1;ui.render(s,s.rpm,s.speed,testMillis);save(DisplayPage::Main,"-polish-chinese");
    CHECK(recordHash(testPanel,sizeof(testPanel))!=stableLayout);
    CHECK(candidateStatus(s,1,testMillis)==ReadingStatus::Fresh);
    CHECK(candidateStatus(s,1,testMillis+15001)==ReadingStatus::Stale);
    s.transportState=0;CHECK(candidateStatus(s,1,testMillis)==ReadingStatus::Disconnected);s.transportState=1;
    s.presentation.layout=0;ui.render(s,s.rpm,s.speed,testMillis);CHECK(recordHash(testPanel,sizeof(testPanel))==stableLayout);
    ui.page(DisplayPage::Data);ui.render(s,s.rpm,s.speed,testMillis);save(DisplayPage::Data,"-candidates-demo");
    CHECK(recordHash(testPanel,sizeof(testPanel))!=hashes[1]);
    for(uint8_t i=0;i<4;++i)CHECK(candidateFresh(s,i,testMillis));
    CHECK(zhMissingGlyphs==0);
    printf("PASS %u page assertions (%s): 60 page renders, repeat hashes equal, offline view changes\n",checks,kSimulatedSource?"sim":"vehicle");
}
