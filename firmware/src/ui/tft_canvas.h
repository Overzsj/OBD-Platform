#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "font5x7.h"
#include "suki_fonts.h"
#include "character_animation.h"

#pragma GCC push_options
#pragma GCC optimize ("O3")

namespace obd {

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Exact division by 255 for the blend numerators (at most 16192).
inline uint16_t blendDiv255(uint32_t v) { return (v+1+(v>>8))>>8; }
inline uint16_t mix565(uint16_t bg, uint16_t fg, uint8_t alpha) {
    if (!alpha || bg==fg) return bg;
    if (alpha==255) return fg;
    const uint16_t inv = 255 - alpha;
    uint16_t r = blendDiv255(((bg >> 11) & 31) * inv + ((fg >> 11) & 31) * alpha + 127);
    uint16_t g = blendDiv255(((bg >> 5) & 63) * inv + ((fg >> 5) & 63) * alpha + 127);
    uint16_t b = blendDiv255((bg & 31) * inv + (fg & 31) * alpha + 127);
    return uint16_t((r << 11) | (g << 5) | b);
}

// Coordinates are screen coordinates; drawing clips to the current 16-row tile.
class TftCanvas {
public:
    static constexpr int16_t kWidth = 480, kRows = 16;
    void attach(uint16_t* buf, int16_t originY) { buf_ = buf; originY_ = originY; }
    int16_t top() const { return originY_; }
    void blendPixel(int16_t x,int16_t y,uint16_t color,uint8_t alpha) {
        y-=originY_;
        if (x<0 || x>=kWidth || y<0 || y>=kRows || !alpha) return;
        uint16_t& p=buf_[y*kWidth+x];p=alpha==255?color:mix565(p,color,alpha);
    }
    void pixel(int16_t x, int16_t y, uint16_t color) {
        y -= originY_;
        if (x >= 0 && x < kWidth && y >= 0 && y < kRows) buf_[y * kWidth + x] = color;
    }
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color, uint8_t alpha = 255) {
        if(!alpha||w<=0||h<=0)return;
        int16_t x0 = x < 0 ? 0 : x, x1 = x + w > kWidth ? kWidth : x + w;
        int16_t y0 = y < originY_ ? originY_ : y;
        int16_t y1 = y + h > originY_ + kRows ? originY_ + kRows : y + h;
        for (int16_t py = y0; py < y1; ++py) {
            uint16_t* row = buf_ + (py - originY_) * kWidth;
            for (int16_t px = x0; px < x1; ++px) row[px] = alpha == 255 ? color : mix565(row[px], color, alpha);
        }
    }
    void rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
        fillRect(x, y, w, 1, color); fillRect(x, y + h - 1, w, 1, color);
        fillRect(x, y, 1, h, color); fillRect(x + w - 1, y, 1, h, color);
    }
    void circle(int16_t x, int16_t y, int16_t radius, uint16_t color) {
        for (int16_t dy = -radius; dy <= radius; ++dy) {
            if (y + dy < originY_ || y + dy >= originY_ + kRows) continue;
            for (int16_t dx = -radius; dx <= radius; ++dx) if (dx * dx + dy * dy <= radius * radius) pixel(x + dx, y + dy, color);
        }
    }
    void line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
        int16_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
        int16_t dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
        int16_t error = dx + dy;
        for (;;) {
            pixel(x0, y0, color);
            if (x0 == x1 && y0 == y1) break;
            int16_t e2 = 2 * error;
            if (e2 >= dy) { error += dy; x0 += sx; }
            if (e2 <= dx) { error += dx; y0 += sy; }
        }
    }
    void text(int16_t x, int16_t y, const char* str, uint8_t scale, uint16_t color) {
        if (!str || !scale || y + 7 * scale <= originY_ || y >= originY_ + kRows) return;
        for (const char* p = str; *p; ++p, x += 6 * scale) {
            const Glyph5x7* glyph = nullptr;
            for (size_t i = 0; i < kFont5x7Count; ++i) if (kFont5x7[i].ch == *p) { glyph = &kFont5x7[i]; break; }
            if (!glyph) continue;
            for (uint8_t col = 0; col < 5; ++col) for (uint8_t row = 0; row < 7; ++row)
                if (glyph->col[col] & (1u << row)) fillRect(x + col * scale, y + row * scale, scale, scale, color);
        }
    }
    static int16_t textWidth(const char* str, uint8_t scale) {
        size_t n = str ? strlen(str) : 0;
        return n ? int16_t(n * 6 * scale - scale) : 0;
    }
    void textRight(int16_t right, int16_t y, const char* str, uint8_t scale, uint16_t color) {
        text(right - textWidth(str, scale) + 1, y, str, scale, color);
    }
    void textCenter(int16_t center, int16_t y, const char* str, uint8_t scale, uint16_t color) {
        text(center - textWidth(str, scale) / 2, y, str, scale, color);
    }
    void mask(int16_t x,int16_t y,uint16_t w,uint16_t h,const uint8_t* bytes,uint32_t offset,uint16_t color,uint8_t opacity=255) {
        if (!opacity || y+h<=originY_ || y>=originY_+kRows || x+w<=0 || x>=kWidth) return;
        const int16_t start=y<originY_?originY_-y:0;
        const int16_t end=y+h>originY_+kRows?originY_+kRows-y:h;
        const int16_t left=x<0?-x:0,right=x+w>kWidth?kWidth-x:w;
        for (int16_t row=start;row<end;++row) {
            const uint8_t* source=bytes+offset+row*w;
            uint16_t* target=buf_+(y+row-originY_)*kWidth+x+left;
            for (int16_t col=left;col<right;++col,++target) {
                uint8_t a=source[col];
                if (opacity!=255) a=blendDiv255(uint16_t(a)*opacity+127);
                if (a) *target=a==255?color:mix565(*target,color,a);
            }
        }
    }
    static uint16_t codepoint(const char*& p) {
        const uint8_t first=uint8_t(*p++);
        if ((first&0xE0)==0xC0 && *p) return uint16_t((first&31)<<6)|(uint8_t(*p++)&63);
        return first;
    }
    static const UiGlyph* findGlyph(const UiFontDef& font,uint16_t code) {
        // Generated glyph tables are sorted by codepoint; digit lookup is O(1).
        if (code>=48 && code<=57) {
            const uint16_t base=font.glyphs[0].code==45?1:font.glyphs[0].code==32?0:0;
            const uint16_t i=base+code-48;
            if (i<font.count && font.glyphs[i].code==code) return font.glyphs+i;
        }
        uint16_t lo=0,hi=font.count;
        while (lo<hi) {
            const uint16_t mid=lo+(hi-lo)/2;
            if (font.glyphs[mid].code<code) lo=mid+1; else hi=mid;
        }
        return lo<font.count && font.glyphs[lo].code==code?font.glyphs+lo:nullptr;
    }
    static int16_t bitmapWidth(const char* str,UiFont id) {
        const UiFontDef& font=kUiFonts[uint8_t(id)];
        int16_t width=0;
        for (const char* p=str;p && *p;) { const UiGlyph* glyph=findGlyph(font,codepoint(p)); if (glyph) width+=glyph->advance; }
        return width;
    }
    void bitmapText(int16_t x,int16_t y,const char* str,UiFont id,uint16_t color) {
        // All current fonts fit a 72px cell; small fonts fit 24px.
        const int16_t height=id==UiFont::Hero?72:id==UiFont::Value?32:24;
        if (y>=originY_+kRows || y+height<=originY_) return;
        const UiFontDef& font=kUiFonts[uint8_t(id)];
        for (const char* p=str;p && *p;) {
            const UiGlyph* glyph=findGlyph(font,codepoint(p));
            if (!glyph) continue;
            mask(x+glyph->left,y+glyph->top,glyph->width,glyph->height,font.mask,glyph->offset,color);
            x+=glyph->advance;
        }
    }
    void bitmapRight(int16_t right,int16_t y,const char* str,UiFont id,uint16_t color) {
        bitmapText(right-bitmapWidth(str,id)+1,y,str,id,color);
    }
    void bitmapCenter(int16_t center,int16_t y,const char* str,UiFont id,uint16_t color) {
        bitmapText(center-bitmapWidth(str,id)/2,y,str,id,color);
    }
    void prepareCharacter(const CharacterPose& p,const uint16_t* background) {
        using namespace suki_assets;
        if (!geometryReady_) {
            for (int x=256;x<480;++x) {
                edgeX_[x-256]=characterClamp(min((x-256)*255/12,(479-x)*255/7),0,255);
                hairX_[x-256]=characterClamp((abs(x-391)-46)*255/60,0,255);
            }
            geometryReady_=true;
        }
        for (int x=256;x<480;++x) {
            hairShift_[x-256]=int32_t(p.hair)*hairX_[x-256];
            steerShift_[x-256]=int32_t(p.steer)*(x-397);
        }
        if (faceReady_ && cachedEffort_==p.effort) return;
        for (int y=0;y<kFaceHeight;++y) for (int x=0;x<kFaceWidth;++x) {
            const int i=y*kFaceWidth+x;
            face_[i]=mix565(background[(y+kFaceY)*480+x+kFaceX],
                mix565(kFaceCalm[i],kFaceEffort[i],p.effort),kFaceMask[i]);
        }
        cachedEffort_=p.effort;faceReady_=true;
    }
    uint16_t characterSample(const uint16_t* background,int16_t x,int16_t y) const {
        using namespace suki_assets;
        x=characterClamp(x,0,479);y=characterClamp(y,0,319);
        if (x>=kFaceX && x<kFaceX+kFaceWidth && y>=kFaceY && y<kFaceY+kFaceHeight)
            return face_[(y-kFaceY)*kFaceWidth+x-kFaceX];
        return background[uint32_t(y)*480+x];
    }
    // Five fractional bits match RGB565's channel precision. Packed arithmetic
    // avoids nine per-channel multiplies for every bilinear output pixel.
    static uint16_t characterBlend(uint16_t bg,uint16_t fg,uint8_t alpha) {
        if (bg==fg) return bg;
        const uint32_t a=(uint16_t(alpha)+4)>>3,mask=0x07E0F81Fu;
        uint32_t b=(uint32_t(bg)|(uint32_t(bg)<<16))&mask;
        const uint32_t f=(uint32_t(fg)|(uint32_t(fg)<<16))&mask;
        b=(b+(((f-b)*a)>>5))&mask;
        return uint16_t(b|(b>>16));
    }
    void character(const CharacterPose& p,const uint16_t* background) {
        const int16_t y0=originY_<64?64:originY_,y1=originY_+kRows>218?218:originY_+kRows;
        for (int16_t y=y0;y<y1;++y) {
            const int16_t edgeY=characterClamp(min((y-64)*255/12,(217-y)*255/10),0,255);
            const int16_t head=y<173?255:characterClamp((211-y)*255/38,0,255);
            const int16_t hands=characterClamp((y-157)*255/41,0,255);
            const int32_t sway=int32_t(p.sway)*head,nod=int32_t(p.nod)*head;
            uint16_t* dest=buf_+(y-originY_)*480+256;
            for (int16_t x=256;x<480;++x,++dest) {
                const uint16_t i=x-256;
                const int16_t edge=min(int16_t(edgeX_[i]),edgeY);
                int16_t dx=(sway+hairShift_[i])/255;
                int16_t dy=(nod+steerShift_[i]*hands/90)/255;
                if (edge!=255) { dx=int32_t(dx)*edge/255;dy=int32_t(dy)*edge/255; }
                const int32_t sx=int32_t(x)*256-dx,sy=int32_t(y)*256-dy;
                const int16_t ix=sx>>8,iy=sy>>8;
                const uint8_t ax=sx&255,ay=sy&255;
                using namespace suki_assets;
                const uint16_t* row0=background+iy*480+ix;
                const uint16_t* row1=row0+480;
                if (ix>=kFaceX && ix+1<kFaceX+kFaceWidth && iy>=kFaceY && iy+1<kFaceY+kFaceHeight) {
                    row0=face_+(iy-kFaceY)*kFaceWidth+ix-kFaceX;row1=row0+kFaceWidth;
                } else if (ix>=kFaceX-1 && ix<kFaceX+kFaceWidth && iy>=kFaceY-1 && iy<kFaceY+kFaceHeight) {
                    const uint16_t top=characterBlend(characterSample(background,ix,iy),characterSample(background,ix+1,iy),ax);
                    const uint16_t bottom=characterBlend(characterSample(background,ix,iy+1),characterSample(background,ix+1,iy+1),ax);
                    *dest=characterBlend(top,bottom,ay);continue;
                }
                // The edge fade keeps coordinates inside the background; at
                // x=479 both fractions are zero, so the neighbour is not read.
                if (!ax) { *dest=ay?characterBlend(row0[0],row1[0],ay):row0[0];continue; }
                const uint16_t top=characterBlend(row0[0],row0[1],ax);
                *dest=ay?characterBlend(top,characterBlend(row1[0],row1[1],ax),ay):top;
            }
        }
    }

private:
    uint16_t face_[suki_assets::kFaceWidth*suki_assets::kFaceHeight]{};
    uint8_t edgeX_[224]{},hairX_[224]{};
    int32_t hairShift_[224]{},steerShift_[224]{};
    uint8_t cachedEffort_=0;
    bool geometryReady_=false,faceReady_=false;
    uint16_t* buf_ = nullptr;
    int16_t originY_ = 0;
};

} // namespace obd
#pragma GCC pop_options
