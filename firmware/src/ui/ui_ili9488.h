#pragma once
#include <Arduino.h>
#include <cmath>
#include <cstdio>
#include <stdlib.h>
#include "tft_ili9488.h"
#include "tft_canvas.h"
#include "tft_transfer_queue.h"
#include "boot_animation.h"
#include "suki_background.h"
#include "../app/vehicle_state.h"
#include "../app/alarms.h"
#include "data_lab_theme.h"
#include "diagnostics_theme.h"
#include "shift_lights.h"
#include "rpm_curve.h"
#include "bar_motion.h"
#include "../app/telemetry_signals.h"

#pragma GCC push_options
#pragma GCC optimize ("O3")
// Keep 25.6 KB available to the classic Bluetooth stack. The compositor's
// existing fenced strip buffers already support identical uncached rendering.
#ifndef TFT_HERO_CACHE
#define TFT_HERO_CACHE 0
#endif
namespace obd {
class UiTft {
public:
    UiTft()=default;
    UiTft(const UiTft&)=delete;
    UiTft& operator=(const UiTft&)=delete;
    ~UiTft(){free(heroPixels_);}
    using Service=void (*)(const VehicleSnapshot&,uint32_t);
    void attach(Ili9488* display) { display_=display;if(TFT_HERO_CACHE && !heroPixels_)heroPixels_=static_cast<uint16_t*>(malloc(kHeroWidth*kHeroHeight*sizeof(uint16_t)));transfers_.begin(display); for (auto& v:history_) v=-1; bootStarted_=false;bootDone_=!TFT_BOOT_ANIMATION;invalidate(); }
    bool heroCached() const {return heroPixels_!=nullptr;}
    bool active() const { return display_ && display_->initialized(); }
    void setService(Service service) { service_=service; }
    void invalidate() { memset(validCells_,0,sizeof(validCells_)); visualValid_=false;ledValid_=false; }
    static float flashWarnRpm() { return kDefaultAlarms[ALARM_RPM].warn; }
    static float flashFastRpm() { return kDefaultAlarms[ALARM_RPM].crit; }
    void settings(const DeviceSettings& s) { warnRpm_=s.rpmWarn;critRpm_=s.rpmCrit;if(!s.bootAnimation)bootDone_=true; }
    void page(DisplayPage value) {
        if(uint8_t(value)>=kDisplayPages || value==page_)return;
        transfers_.finish();page_=value;invalidate();diagMs_=labMs_=0;
        // Navigation should return directly to the selected page, not replay an intro.
        bootDone_=true;
    }
    DisplayPage page() const {return page_;}
    uint32_t frames(DisplayPage value) const {return rendered_[uint8_t(value)];}
    void renderDiagnostics(const VehicleSnapshot& s,uint32_t now) {
        if(diagMs_ && uint32_t(now-diagMs_)<500)return;
        diagMs_=now;
        transfers_.finish();
        for(uint8_t tile=0;tile<kTiles;++tile) {
            pixels_=pixelBuffers_[0];canvas_.attach(pixels_,tile*TftCanvas::kRows);
            drawDiagnostics(canvas_,s,now,ESP.getFreeHeap());
            display_->writeRows(tile*TftCanvas::kRows,TftCanvas::kRows,pixels_);
            if(service_)service_(s,millis());yield();
        }
        ++rendered_[uint8_t(DisplayPage::Diagnostics)];
    }
    uint32_t flashCount() const { return shiftLights_.transitions(); }
    uint32_t lightFrames() const {return lightFrames_;}
    bool bootAnimating() const{return !bootDone_;}
    void replayBoot(){transfers_.finish();page_=DisplayPage::Main;bootStarted_=false;bootDone_=false;bootVisualValid_=false;invalidate();}
    uint32_t bootFrames() const{return bootFrames_;}
    uint32_t bootRenderUs() const{return bootRenderUs_;}
    uint32_t bootBytes() const{return bootBytes_;}
    uint32_t bootLastMs() const{return bootLastMs_;}
    float lightFrequency() const {return shiftLights_.frequency();}
    void tickFlash(float rpm,uint32_t now) {
        shiftLights_.tick(rpm,now,warnRpm_,critRpm_);
    }
    // Independently refresh only the 448x16 light strip, reusing fenced buffers.
    // The character, large digits and the other pages keep their own cadence.
    void renderShiftLights(const VehicleSnapshot& s,float rpmShown,uint32_t now) {
        if(!active() || page_!=DisplayPage::Main || !visualValid_ || !bootDone_)return;
        if(ledValid_ && uint32_t(now-lastLightMs_)<ShiftLights::kFrameMs)return;
        lastLightMs_=now;
        tickFlash(fresh(s,VF_RPM,now)?s.rpm:0,now);
        const bool good=fresh(s,VF_RPM,now) && std::isfinite(rpmShown);
        const uint8_t mode=good?(s.rpm>=critRpm_?2:s.rpm>=warnRpm_?1:0):0;
        uint8_t alpha[15];bool lampDirty[15];bool changed=false;
        for(uint8_t i=0;i<15;++i){alpha[i]=shiftLights_.alpha(i,rpmShown,good,warnRpm_,s.display.maximum[5]);lampDirty[i]=!ledValid_||mode!=lastLightMode_||alpha[i]!=lastLightAlpha_[i];changed=changed||lampDirty[i];}
        if(!changed)return;
        for(uint8_t tile=0;tile<2;++tile) {
            transfers_.acquire(tile);pixels_=pixelBuffers_[tile];const int y=tile*16;
            memcpy_P(pixels_,suki_assets::kBackground+y*480,480*16*sizeof(uint16_t));
            canvas_.attach(pixels_,y);composeShiftLights(s,rpmShown,now);
            const int first=tile?16:6,rows=tile?6:10;
            // Lamps are 16px wide, separated by 14px of untouched background.
            // Keep the same animation, send only changed lamps, not their gaps.
            for(uint8_t i=0;i<15;++i)if(lampDirty[i]){
                const int x=20+i*30;
                transfers_.writeRect(x,first,16,rows,pixels_+(first-y)*480+x,480);
            }
            transfers_.release(tile);
        }
        transfers_.finish();memcpy(lastLightAlpha_,alpha,sizeof(alpha));lastLightMode_=mode;ledValid_=true;++lightFrames_;
    }
    static bool fresh(const VehicleSnapshot& s,ValueField field,uint32_t now) {
        return valueFresh(s,field,now);
    }
    void render(const VehicleSnapshot& s,float rpmShown,float speedShown,uint32_t now) {
        const uint32_t rangeHash=recordHash(&s.display,sizeof(s.display));
        if(lastRangeHash_!=rangeHash){lastRangeHash_=rangeHash;invalidate();}
        if(lastLayout_!=s.presentation.layout){lastLayout_=s.presentation.layout;invalidate();}
        if (!active()) return;
        if(page_==DisplayPage::Diagnostics) {renderDiagnostics(s,now);return;}
        if(page_==DisplayPage::Data) {
        if(labMs_ && uint32_t(now-labMs_)<200)return;
        labMs_=now;
        transfers_.finish();
        for(uint8_t tile=0;tile<kTiles;++tile) {
            pixels_=pixelBuffers_[0];canvas_.attach(pixels_,tile*TftCanvas::kRows);
            drawDataLab(canvas_,s,now);display_->writeRows(tile*TftCanvas::kRows,TftCanvas::kRows,pixels_);
            if(service_)service_(s,millis());yield();
        }
        ++rendered_[uint8_t(DisplayPage::Data)];
        return;
        }
        const uint32_t frameStart=micros();
        uint32_t composeUs=0,hashUs=0,sendUs=0,phaseStart=0,charUs=0;
        const uint32_t bytesBefore=display_->bytesWritten();
        if (!bootStarted_) { bootStarted_=true;bootStartMs_=now; }
        const uint32_t bootElapsed=uint32_t(now-bootStartMs_);
        if (bootElapsed>=BootAnimation::kDuration)bootDone_=true;
        const bool intro=!bootDone_ && bootElapsed<3400;
        const bool foreground=!bootDone_;
        bool historyChanged=false;
        if (now-lastHistoryMs_>=100) {
            historyChanged=true;
            lastHistoryMs_=now;
            memmove(history_,history_+1,(kHistory-1)*sizeof(history_[0]));
            history_[kHistory-1]=fresh(s,VF_RPM,now) && std::isfinite(s.rpm)?s.rpm:-1;
        }
        const auto pose=gaugeCharacterPose(now,s.rpm,fresh(s,VF_RPM,now),warnRpm_,(s.alarmFlags & (1u<<ALARM_RPM))!=0);
#if TFT_CHARACTER_ANIMATION
        if (!intro) canvas_.prepareCharacter(pose,suki_assets::kBackground);
#endif
        bool candidates[kTiles*kColumns]{};
        auto mark=[&](int x,int y,int w,int h) {
            for (int row=y/TftCanvas::kRows;row<=(y+h-1)/TftCanvas::kRows;++row)
                for (int col=x/kCellWidth;col<=(x+w-1)/kCellWidth;++col)
                    candidates[row*kColumns+col]=true;
        };
        const bool full=intro?!bootVisualValid_:!visualValid_ || !bootDone_;
        if (full) memset(candidates,1,sizeof(candidates));
        else if(intro)BootAnimation::changes(mark,lastBootElapsed_,bootElapsed);
        const bool rpmGood=fresh(s,VF_RPM,now) && std::isfinite(rpmShown);
        const int heroKey=rpmGood?int(bounded(rpmShown,9999)+.5f):-1;
        const uint32_t statusKey=uint32_t(s.linkUp)|(uint32_t(s.linkQuality)<<1)|(uint32_t(s.validMask)<<9)|
            (uint32_t(s.alarmFlags)<<17)|(uint32_t(rpmGood)<<25)|
            (uint32_t(fresh(s,VF_COOLANT,now))<<26)|(uint32_t(s.estimatedGear)<<27);
        if(!intro){
        heroTouched_=full || heroKey!=lastHeroKey_;
        if (heroTouched_)mark(kHeroX,kHeroY,kHeroWidth,kHeroHeight);
        if (full || statusKey!=lastStatusKey_) { mark(416,32,64,32);mark(0,288,480,32); }
        if (historyChanged)mark(0,176,256,48);

        for (int i=0;i<5;++i) {
            const auto sample=signalValue(s,kThemeSlots[i],now);
            const auto& def=kSignals[uint8_t(kThemeSlots[i])];
            const bool good=sample.fresh();
            const float oldBar=barMotion_[i].height();
            const float bar=barMotion_[i].update(sample.value/s.display.maximum[uint8_t(kThemeSlots[i])]*49,good,now);
            const int barKey=int(lroundf(bar*8));
            if(full||barKey!=lastBarKeys_[i]){
                const int height=int(ceilf(oldBar>bar?oldBar:bar));
                if(height)mark(19+i*92,297-height,74,height);
            }
            lastBarKeys_[i]=barKey;
            const auto state=sample.status;
            const int value=good?int(lroundf(sample.value*100.0f)):-99999-int(state);
            // Labels and units are static while the field remains valid.
            // Only validity/status transitions need to repaint their row.
            const int statusKey=good?0:1+int(state);
            if(full||statusKey!=lastFieldStatus_[i])mark(16+i*92,208,80,96);
            else if(value!=lastValues_[i])mark(16+i*92,264,80,32);
            lastFieldStatus_[i]=statusKey;
            lastValues_[i]=value;
        }
        const bool characterChanged=full || pose.effort!=lastPose_.effort || pose.sway!=lastPose_.sway ||
            pose.nod!=lastPose_.nod || pose.hair!=lastPose_.hair || pose.steer!=lastPose_.steer;
        if (characterChanged)mark(256,64,224,160);
        }
        const HeroRect heroRect=heroUpdateRect(lastHeroKey_,heroKey,full);
        bool heroDirty=false;
        for (uint8_t tile=0;tile<kTiles;++tile) {
            bool any=false;
            for (int col=0;col<kColumns;++col)any=any || candidates[tile*kColumns+col];
            if (!any) continue;
            const uint8_t slot=tile&1;
            transfers_.acquire(slot);
            pixels_=pixelBuffers_[slot];
            const uint16_t y=tile*TftCanvas::kRows;
            phaseStart=micros();
            canvas_.attach(pixels_,y);
            if (intro)BootAnimation::draw(canvas_,bootElapsed);
            else {
            for (uint8_t col=0;col<kColumns;) {
                if (!candidates[tile*kColumns+col]) { ++col;continue; }
                const uint8_t first=col;
                while (col<kColumns && candidates[tile*kColumns+col])++col;
                for (uint16_t row=0;row<TftCanvas::kRows;++row)
                    memcpy_P(pixels_+row*480+first*kCellWidth,
                        suki_assets::kBackground+(y+row)*480+first*kCellWidth,
                        (col-first)*kCellWidth*sizeof(uint16_t));
            }
#if TFT_CHARACTER_ANIMATION
            const uint32_t charStart=micros();
            bool charCell=false;
            for (uint8_t c=8;c<kColumns;++c)charCell=charCell || candidates[tile*kColumns+c];
            if (charCell)canvas_.character(pose,suki_assets::kBackground);
            charUs+=micros()-charStart;
#endif
            compose(s,rpmShown,speedShown,now,candidates+tile*kColumns);
            if (!bootDone_)canvas_.fillRect(0,0,480,320,kBg,255-BootAnimation::ease(bootElapsed-3400,400));
            }
            composeUs+=micros()-phaseStart; phaseStart=micros();
            bool dirty[kColumns]{};
            for (uint8_t col=0;col<kColumns;++col) {
                if (!candidates[tile*kColumns+col])continue;
                uint32_t hash=2166136261u;
                for (uint16_t row=0;row<TftCanvas::kRows;++row)
                    for (uint16_t x=col*kCellWidth;x<(col+1)*kCellWidth;x+=2) {
                        uint32_t pair;memcpy(&pair,pixels_+row*480+x,sizeof(pair));
                        hash=(hash^pair)*16777619u;
                        // Mix high RGB565 bits back down; word-only FNV can
                        // otherwise miss subtle dark pixels during fades.
                        hash=(hash<<7)|(hash>>25);
                    }
                const size_t cell=tile*kColumns+col;
                dirty[col]=!validCells_[cell] || cellHash_[cell]!=hash;
                cellHash_[cell]=hash; validCells_[cell]=true;
            }
            // Stage all large-number strips, then send one contiguous rectangle.
            // This shortens the visible old/new digit transition on the panel.
            const bool heroTile=heroPixels_ && bootDone_ && heroTouched_ && y>=kHeroY && y<kHeroY+kHeroHeight;
            if (heroTile) {
                for (uint16_t row=0;row<TftCanvas::kRows;++row)
                    memcpy(heroPixels_+(y-kHeroY+row)*kHeroWidth,
                           pixels_+row*480+kHeroX,kHeroWidth*sizeof(uint16_t));
                for (uint8_t col=kHeroX/kCellWidth;col<(kHeroX+kHeroWidth)/kCellWidth;++col) {
                    heroDirty=heroDirty || dirty[col];
                    dirty[col]=false;
                }
            }
            hashUs+=micros()-phaseStart; phaseStart=micros();
            for (uint8_t col=0;col<kColumns;) {
                if (!dirty[col]) { ++col; continue; }
                uint8_t first=col; while (col<kColumns && dirty[col]) ++col;
                const uint16_t x=first*kCellWidth,w=(col-first)*kCellWidth;
                transfers_.writeRect(x,y,w,TftCanvas::kRows,pixels_+x,480);
                if (service_) service_(s,millis()); yield();
            }
            if (heroTile && y+TftCanvas::kRows==kHeroY+kHeroHeight && heroDirty)
                transfers_.writeRect(heroRect.x,heroRect.y,heroRect.w,heroRect.h,
                    heroPixels_+(heroRect.y-kHeroY)*kHeroWidth+heroRect.x-kHeroX,kHeroWidth);
            transfers_.release(slot);
            if (service_) service_(s,millis()); yield();
            sendUs+=micros()-phaseStart;
            // Service the light deadline between body strips as well. Waiting
            // until a large-number/chart frame ends can reduce it below 30fps.
            renderShiftLights(s,rpmShown,millis());
        }
        phaseStart=micros();transfers_.finish();sendUs+=micros()-phaseStart;
        bootVisualValid_=intro;lastBootElapsed_=bootElapsed;
        lastHeroKey_=heroKey;lastStatusKey_=statusKey;lastPose_=pose;
        if(full)ledValid_=false;
        visualValid_=bootDone_;
        ++rendered_[uint8_t(DisplayPage::Main)];
        const uint32_t frameUs=micros()-frameStart;
        if(foreground){++bootFrames_;bootRenderUs_+=frameUs;bootBytes_+=display_->bytesWritten()-bytesBefore;bootLastMs_=bootElapsed;}
        ++perfFrames_;perfTotalUs_+=frameUs;perfComposeUs_+=composeUs;
        perfCharUs_+=charUs;perfHashUs_+=hashUs;perfSendUs_+=sendUs;perfBytes_+=display_->bytesWritten()-bytesBefore;
        if (frameUs>perfMaxUs_)perfMaxUs_=frameUs;
        const uint32_t perfNow=millis(),elapsed=perfNow-perfStartMs_;
        if (elapsed>=2000) {
            Serial.printf("UI_PERF|frames=%u|window_ms=%u|avg_us=%u|max_us=%u|compose_us=%u|char_us=%u|hash_us=%u|send_us=%u|bytes_frame=%u|heap=%u\n",
                perfFrames_,elapsed,perfTotalUs_/perfFrames_,perfMaxUs_,perfComposeUs_/perfFrames_,perfCharUs_/perfFrames_,
                perfHashUs_/perfFrames_,perfSendUs_/perfFrames_,perfBytes_/perfFrames_,ESP.getFreeHeap());
            perfStartMs_=perfNow;perfFrames_=perfTotalUs_=perfComposeUs_=perfCharUs_=perfHashUs_=perfSendUs_=perfBytes_=perfMaxUs_=0;
        }
    }
    void testPattern(const VehicleSnapshot& s) {
        if (!active()) return;
        const uint16_t colors[]={rgb565(255,0,0),rgb565(0,255,0),rgb565(0,0,255),rgb565(255,255,255),rgb565(0,0,0),rgb565(0,255,255),rgb565(255,0,255),rgb565(255,255,0)};
        for (uint8_t tile=0;tile<kTiles;++tile) {
            for (uint16_t row=0;row<TftCanvas::kRows;++row) for (uint16_t x=0;x<480;++x) pixels_[row*480+x]=colors[x/60];
            canvas_.attach(pixels_,tile*TftCanvas::kRows); canvas_.rect(0,0,480,320,rgb565(255,255,255));
            canvas_.fillRect(12,285,262,24,rgb565(0,0,0)); canvas_.text(20,291,"ILI9488 480X320",2,rgb565(255,255,255));
            display_->writeRows(tile*TftCanvas::kRows,TftCanvas::kRows,pixels_);
            if (service_) service_(s,millis()); yield();
        }
        invalidate();
    }
private:
    float warnRpm_=5000,critRpm_=6000;
    DisplayPage page_=DisplayPage::Main;
    uint32_t rendered_[kDisplayPages]{};
    uint32_t diagMs_=0;
    uint32_t labMs_=0;
    static constexpr uint8_t kTiles=20,kHistory=48,kColumns=15;
    static constexpr uint16_t kCellWidth=32;
    // Four-digit hero ink fits x=36..187, y=98..162. Include old ink on shrink.
    static constexpr uint16_t kHeroX=32,kHeroY=96,kHeroWidth=160,kHeroHeight=80;
    static_assert(kHeroX%kCellWidth==0 && kHeroWidth%kCellWidth==0 &&
                  kHeroY%TftCanvas::kRows==0 && kHeroHeight%TftCanvas::kRows==0,
                  "Hero region must cover complete compositor cells");
    const uint16_t &kBg=theme::kBg,&kWhite=theme::kWhite,&kYellow=theme::kYellow,&kMuted=theme::kMuted,&kCyan=theme::kCyan,&kPink=theme::kPink;
    struct HeroRect { uint16_t x,y,w,h; };
    // Compare glyphs at their actual screen positions, including erased old ink.
    // Unchanged leading digits need no SPI transfer; width changes remain safe.
    static HeroRect heroUpdateRect(int previous,int current,bool full) {
        if (full) return {kHeroX,kHeroY,kHeroWidth,kHeroHeight};
        struct Ink { uint16_t code; int x,y,w,h; } oldInk[4]{},newInk[4]{};
        auto layout=[](int value,Ink* ink) {
            char text[16];
            if (value<0) snprintf(text,sizeof(text),"----");
            else snprintf(text,sizeof(text),"%d",value);
            int x=188-TftCanvas::bitmapWidth(text,UiFont::Hero),n=0;
            for (const char* p=text;*p && n<4;++p) {
                const auto* g=TftCanvas::findGlyph(kUiFonts[uint8_t(UiFont::Hero)],uint8_t(*p));
                if (!g) continue;
                ink[n++]={uint16_t(uint8_t(*p)),x+g->left,92+g->top,g->width,g->height};
                x+=g->advance;
            }
            return n;
        };
        const int a=layout(previous,oldInk),b=layout(current,newInk);
        int left=192,top=176,right=32,bottom=96;
        auto include=[&](const Ink& ink,const Ink* other,int count) {
            for (int i=0;i<count;++i)
                if (ink.code==other[i].code && ink.x==other[i].x && ink.y==other[i].y) return;
            left=min(left,ink.x);top=min(top,ink.y);
            right=max(right,ink.x+ink.w);bottom=max(bottom,ink.y+ink.h);
        };
        for (int i=0;i<a;++i)include(oldInk[i],newInk,b);
        for (int i=0;i<b;++i)include(newInk[i],oldInk,a);
        if (right<=left || bottom<=top) return {kHeroX,kHeroY,0,0};
        return {uint16_t(left),uint16_t(top),uint16_t(right-left),uint16_t(bottom-top)};
    }
    static float bounded(float v,float max) { return std::isfinite(v)?(v<0?0:v>max?max:v):0; }
    static const char* alarmWord(uint8_t f) {
        if (f&0x10) return "HOT!"; if (f&0x20) return "OVER!";
        if (f&0x01) return "HOT"; if (f&0x02) return "OVER"; return nullptr;
    }
    static const char* sourceName() {
        return OBD_DRIVE_REPLAY?"REPLAY":kSimulatedSource?"SIM":"BT";
    }
    void reading(int16_t x,const char* label,const char* unit,float value,float maximum,bool good,uint8_t decimals,uint16_t accent,const char* chinese=nullptr,ReadingStatus state=ReadingStatus::Missing,float barHeight=-1) {
        // Do not format numbers, measure strings or draw labels for other bands.
        const int top=canvas_.top(),bottom=top+TftCanvas::kRows;
        if(bottom<=208||top>=298)return;
        if(bottom<=219)return;
        if(top<243){
        if(chinese)zhText(canvas_,x+40-zhWidth(chinese,ZhSize::Small)/2,219,chinese,kMuted,ZhSize::Small);
        else canvas_.bitmapCenter(x+40,219,label,UiFont::Label,kMuted);
        }
        if(bottom>233&&top<258){
        if(chinese&&!good){
            const char* status=state==ReadingStatus::Stale?"已过期":state==ReadingStatus::Disconnected?"已断开":"无数据";
            zhText(canvas_,x+40-zhWidth(status,ZhSize::Small)/2,234,status,kMuted,ZhSize::Small);
        }else canvas_.bitmapCenter(x+40,233,unit,UiFont::Small,kMuted);
        }
        const float extent=good?(barHeight>=0?barHeight:bounded(value,maximum)/maximum*49):0;
        const int16_t height=int16_t(ceilf(extent));
        const uint8_t edgeAlpha=uint8_t((extent-floorf(extent)>0?extent-floorf(extent):1)*255);
        const int16_t barY=297-height;
        const int16_t first=canvas_.top()>barY?canvas_.top()-barY:0;
        const int16_t last=canvas_.top()+TftCanvas::kRows<297?canvas_.top()+TftCanvas::kRows-barY:height;
        for (int16_t row=first;row<last;++row) {
            const uint8_t a=height<=1?255:uint16_t(height-1-row)*255/(height-1);
            canvas_.fillRect(x+3,297-height+row,74,1,mix565(rgb565(72,38,120),rgb565(223,148,243),a),row==0?edgeAlpha:255);
        }
        if (height) canvas_.fillRect(x+3,297-height,74,1,mix565(accent,kWhite,80),edgeAlpha);
        if(bottom<=264||top>=296)return;
        char text[16];
        if (!good || !std::isfinite(value)) snprintf(text,sizeof(text),"--");
        else if (decimals) snprintf(text,sizeof(text),"%.1f",value);
        else snprintf(text,sizeof(text),"%d",int(value+(value>=0?.5f:-.5f)));
        canvas_.bitmapCenter(x+40,264,text,UiFont::Value,good?kYellow:kMuted);
    }
    void composeShiftLights(const VehicleSnapshot& s,float rpmShown,uint32_t now) {
        const bool good=fresh(s,VF_RPM,now) && std::isfinite(rpmShown);
        for (uint8_t i=0;i<15;++i) {
            uint16_t color=i<5?kPink:i<11?kCyan:i==11?kYellow:rgb565(240,69,98);
            if (good && s.rpm>=critRpm_) color=rgb565(240,69,98);
            else if (good && s.rpm>=warnRpm_) color=kYellow;
            const uint8_t alpha=shiftLights_.alpha(i,rpmShown,good,warnRpm_,s.display.maximum[5]);
            canvas_.mask(20+i*30,6,16,16,kUiLedMask,0,color,alpha);
            if (alpha>38) canvas_.fillRect(24+i*30,9,2,1,mix565(color,kWhite,80),alpha);
        }
    }
    void compose(const VehicleSnapshot& s,float rpmShown,float speedShown,uint32_t now,const bool* columns=nullptr) {
        auto overlaps=[&](int x,int w){if(!columns)return true;
            for(int col=x/kCellWidth;col<=(x+w-1)/kCellWidth;++col)if(columns[col])return true;
            return false;};
        const bool good=fresh(s,VF_RPM,now) && std::isfinite(rpmShown);
        const float rpm=bounded(rpmShown,9999);
        const int16_t top=canvas_.top();
        if(top<32)composeShiftLights(s,rpmShown,now);
        const char* source=sourceName();
        if (top<64 && top+TftCanvas::kRows>40) {
        canvas_.bitmapText(114,44,!strcmp(source,"SIM")?"SIMULATION":source,UiFont::Small,kCyan);
        const uint8_t quality=s.linkQuality>100?100:s.linkQuality;
        for (uint8_t i=0;i<5;++i) canvas_.fillRect(425+i*8,53-i*2,4,3+i*2,s.linkUp && quality>=20*(i+1)?kCyan:rgb565(49,29,64));
        }
        if ((heroTouched_ || top<96) && top<176 && top+TftCanvas::kRows>80) {
        canvas_.bitmapText(28,82,"ENGINE RPM",UiFont::Label,kMuted);
        char text[24]; if (good) snprintf(text,sizeof(text),"%d",int(rpm+.5f)); else snprintf(text,sizeof(text),"----");
        canvas_.bitmapRight(187,92,text,UiFont::Hero,good?kYellow:kMuted);
        }
        canvas_.bitmapText(203,151,"RPM",UiFont::Label,kWhite);
        if (top<209 && top+TftCanvas::kRows>176) {
        const auto scale=RpmCurveScale::from(history_,kHistory);
        char range[32];snprintf(range,sizeof(range),"%.0f-%.0f RPM",scale.low,scale.high);
        canvas_.text(22,177,range,1,kMuted);
        for (int16_t gy=187;gy<=207;gy+=10) canvas_.fillRect(22,gy,226,1,rgb565(28,24,40));
        if (good) for (uint8_t i=1;i<kHistory;++i) {
            if (!RpmCurveScale::valid(history_[i-1]) || !RpmCurveScale::valid(history_[i])) continue;
            int16_t x0=22+(i-1)*225/(kHistory-1),x1=22+i*225/(kHistory-1);
            int16_t y0=scale.y(history_[i-1]),y1=scale.y(history_[i]);
            const uint16_t color=history_[i]>=critRpm_?kPink:history_[i]>=warnRpm_?kYellow:kCyan;
            canvas_.line(x0,y0,x1,y1,color);
            canvas_.line(x0,y0<207?y0+1:y0,x1,y1<207?y1+1:y1,color);
        }
        }
        const bool cn=s.presentation.layout==1;
        for(uint8_t i=0;i<5;++i){
            if(!overlaps(16+i*92,80))continue;
            const auto id=kThemeSlots[i];const auto& def=kSignals[uint8_t(id)];const auto sample=signalValue(s,id,now);
            reading(16+i*92,def.label,def.unit,sample.value,s.display.maximum[uint8_t(id)],sample.fresh(),def.decimals,
                i%2?kCyan:kPink,cn?def.chinese:nullptr,sample.status,barMotion_[i].height());
        }
        if (top+TftCanvas::kRows<=302) return;
        const uint8_t activeFlags=s.alarmFlags&((fresh(s,VF_COOLANT,now)?0x11:0)|(fresh(s,VF_RPM,now)?0x22:0));
        const char* alarm=alarmWord(activeFlags);
        if (alarm) canvas_.fillRect(0,302,480,18,rgb565(112,23,62));
        canvas_.bitmapText(18,307,!strcmp(source,"SIM")?"SIM DATA":source,UiFont::Small,kCyan);
        if (alarm) canvas_.bitmapCenter(240,305,alarm,UiFont::Label,kYellow);
        else {
            char gearText[20];
            if (!kVehicleProfile.gears.calibrated) snprintf(gearText,sizeof(gearText),"GEAR: CAL");
            else if (s.estimatedGear) snprintf(gearText,sizeof(gearText),"EST GEAR: %u",s.estimatedGear);
            else snprintf(gearText,sizeof(gearText),"EST GEAR: --");
            canvas_.bitmapCenter(240,307,gearText,UiFont::Small,kMuted);
        }
        const char* state=!s.linkUp?"NO LINK":!s.validMask?"READING":good?"READY":"STALE";
        canvas_.bitmapRight(463,307,state,UiFont::Small,s.linkUp && good?kWhite:kPink);
        canvas_.fillRect(328,304,37,13,kBg);canvas_.text(332,307,"01/03",1,kMuted);
    }
    bool visualValid_=false,heroTouched_=true;
    BarMotion barMotion_[5];int lastBarKeys_[5]{},lastFieldStatus_[5]{};
    int lastHeroKey_=-99999,lastValues_[5]{};
    uint32_t lastRangeHash_=0;
    uint8_t lastLayout_=255;
    uint32_t lastStatusKey_=0;
    ShiftLights shiftLights_;
    uint32_t lastLightMs_=0,lightFrames_=0;
    uint8_t lastLightAlpha_[15]{},lastLightMode_=0;
    bool ledValid_=false;
    CharacterPose lastPose_{};
    Ili9488* display_=nullptr;
    Service service_=nullptr;
    TftCanvas canvas_;
    TftTransferQueue transfers_;
    uint16_t pixelBuffers_[2][480*TftCanvas::kRows]{};
    uint16_t* pixels_=pixelBuffers_[0];
    // Allocate in the ESP32's general heap rather than exhausting its smaller
    // statically linked DRAM region when Wi-Fi and BLE are compiled together.
    // If allocation fails, normal dirty-strip rendering remains available.
    uint16_t* heroPixels_=nullptr;
    uint32_t cellHash_[kTiles*kColumns]{};
    bool validCells_[kTiles*kColumns]{};
    float history_[kHistory]{};
    uint32_t perfStartMs_=0,perfFrames_=0,perfTotalUs_=0,perfComposeUs_=0,perfCharUs_=0,perfHashUs_=0,perfSendUs_=0,perfBytes_=0,perfMaxUs_=0;
    uint32_t lastHistoryMs_=0;
    uint32_t bootStartMs_=0;
    uint32_t lastBootElapsed_=0,bootFrames_=0,bootRenderUs_=0,bootBytes_=0,bootLastMs_=0;
    bool bootVisualValid_=false;
    bool bootStarted_=false,bootDone_=!TFT_BOOT_ANIMATION;
};
} // namespace obd
#pragma GCC pop_options
