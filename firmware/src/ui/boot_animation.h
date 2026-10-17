#pragma once
#include "tft_canvas.h"
#include "suki_boot_assets.h"

#ifndef TFT_BOOT_ANIMATION
#define TFT_BOOT_ANIMATION 1
#endif

namespace obd {
class BootAnimation {
public:
    static constexpr uint16_t kDuration=theme::kBootDuration;
    static uint8_t ease(int32_t time,uint16_t length) { return characterEase(characterClamp(time*255/length,0,255)); }
    static void scaledMask(TftCanvas& c,const uint8_t* mask,int16_t x,int16_t y,uint16_t w,uint16_t h,uint16_t color,uint8_t opacity,uint8_t reveal) {
        const int16_t begin=max(int16_t(0),int16_t(c.top()-y)),end=min(int16_t(h),int16_t(c.top()+TftCanvas::kRows-y));
        if(begin>=end||!opacity||!reveal)return;
        const uint16_t columns=uint32_t(w)*reveal/255;
        // Both masks and all strips share the same horizontal resampling map.
        static uint16_t sourceX[240],cachedWidth=0;
        if(cachedWidth!=w){for(uint16_t col=0;col<w;++col)sourceX[col]=uint32_t(col)*240/w;cachedWidth=w;}
        for (int16_t row=begin;row<end;++row) for (uint16_t col=0;col<columns;++col) {
            const uint8_t a=(uint16_t(pgm_read_byte(mask+uint32_t(row)*84/h*240+sourceX[col]))*opacity+127)/255;
            c.blendPixel(x+col,y+row,color,a);
        }
    }
    template<class Mark> static void changes(Mark mark,uint32_t previous,uint32_t t){
        if(previous<260||t<260||t>=3100){mark(0,0,480,320);return;}
        if(t<1350)mark(48,96,384,144);
        else if(previous<1850||t<1850)mark(32,32,448,256);
        else if(ease(int32_t(previous)-1700,900)!=ease(int32_t(t)-1700,900)||characterWave(previous-1350,3400)*2/127!=characterWave(t-1350,3400)*2/127)mark(192,32,288,256);
        if(min(uint32_t(15),previous*15/3100)!=min(uint32_t(15),t*15/3100))mark(0,272,480,32);
    }
    static void draw(TftCanvas& c,uint32_t t) {
        const uint16_t bg=theme::kBg,pink=theme::kPink,cyan=theme::kCyan,white=theme::kWhite,yellow=theme::kYellow,muted=theme::kMuted;
        const uint16_t line=rgb565(30,24,42),faint=rgb565(12,11,21);
        c.fillRect(0,0,480,320,bg);
        for (int16_t y=72;y<272;y+=25)c.fillRect(28,y,424,1,faint);
        const int16_t corners[4][4]={{20,20,1,1},{460,20,-1,1},{20,300,1,-1},{460,300,-1,-1}};
        for (const auto& p:corners) { c.line(p[0],p[1],p[0]+18*p[2],p[1],line);c.line(p[0],p[1],p[0],p[1]+18*p[3],line); }
        c.bitmapText(28,27,theme::kBootTop,UiFont::Small,muted);
        c.bitmapRight(451,27,theme::kBootRight,UiFont::Small,muted);
        if (t<450) {
            const int16_t w=24+uint32_t(322)*ease(t,450)/255,x=(480-w)/2;
            c.fillRect(x,157,w,7,pink,28);c.fillRect(x,160,w,1,white);
            c.bitmapCenter(240,194,"IGNITION",UiFont::Label,yellow);
        } else {
            const uint8_t shift=ease(int32_t(t)-1350,500);
            const uint16_t width=240-uint32_t(50)*shift/255,height=uint32_t(width)*84/240;
            const int16_t x=120-uint32_t(86)*shift/255,y=102+uint32_t(16)*shift/255;
            if (t>=1350) {
                const uint8_t appear=ease(t-1350,500),effort=ease(int32_t(t)-1700,900);
                const int16_t px=220+uint32_t(72)*(255-appear)/255,py=44+characterWave(t-1350,3400)*2/127;
                c.line(330,48,258,267,rgb565(62,31,68));
                const int16_t begin=max(int16_t(0),int16_t(c.top()-py)),end=min(int16_t(230),int16_t(c.top()+TftCanvas::kRows-py));
                for (int16_t sy=begin;sy<end;++sy) for (uint16_t sx=0;sx<260 && px+sx<480;++sx) {
                    const uint32_t i=uint32_t(sy)*260+sx;
                    const uint16_t p=effort==0?pgm_read_word(suki_assets::kBootCalm+i):effort==255?pgm_read_word(suki_assets::kBootEffort+i):mix565(pgm_read_word(suki_assets::kBootCalm+i),pgm_read_word(suki_assets::kBootEffort+i),effort);
                    c.blendPixel(px+sx,py+sy,p,appear);
                }
                c.fillRect(34,197,26,2,cyan);c.bitmapText(34,209,theme::kBootRight,UiFont::Label,cyan);
                c.bitmapText(34,230,theme::kBootTagline,UiFont::Small,yellow);
            } else c.bitmapCenter(240,207,theme::kBootTagline,UiFont::Label,yellow);
            const uint8_t reveal=ease(int32_t(t)-450,850);
            scaledMask(c,suki_assets::kBootGlow,x,y,width,height,pink,130,reveal);
            scaledMask(c,suki_assets::kBootLogo,x,y,width,height,white,255,reveal);
        }
        const uint8_t progress=min(uint32_t(15),t*15/3100);
        for (uint8_t i=0;i<15;++i)c.mask(20+i*30,279,16,16,kUiLedMask,0,i<5?pink:i<11?cyan:yellow,i<progress?210:24);
        c.bitmapText(28,306,theme::kBootFooter,UiFont::Small,muted);
        const uint8_t fadeIn=ease(t,240),fadeOut=t>3100?255-ease(t-3100,300):255;
        c.fillRect(0,0,480,320,bg,255-min(fadeIn,fadeOut));
    }
};
} // namespace obd
