#pragma once
#include <stdint.h>
#include <cmath>
#include "suki_character.h"

#ifndef TFT_CHARACTER_ANIMATION
#define TFT_CHARACTER_ANIMATION 1
#endif
#ifndef TFT_CHARACTER_RPM_LINKED
#define TFT_CHARACTER_RPM_LINKED obd::theme::kCharacterRpmLinked
#endif

namespace obd {
struct CharacterPose { uint8_t effort; int16_t sway,nod,hair,steer; };
inline int32_t characterClamp(int32_t value,int32_t low,int32_t high) { return value<low?low:value>high?high:value; }
inline uint8_t characterEase(int32_t v) { v=characterClamp(v,0,255);return uint32_t(v*v)*(765-2*v)/65025; }
inline int16_t characterWave(uint32_t now,uint16_t period,uint8_t offset=0) {
    return int8_t(pgm_read_byte(suki_assets::kCharacterSine+((uint32_t(now%period)*64/period+offset)&63)));
}
inline CharacterPose characterPose(uint32_t now,float rpm,bool good,bool linked=TFT_CHARACTER_RPM_LINKED) {
    const uint16_t t=now%4800;
    uint8_t effort=linked?(good && std::isfinite(rpm)?characterEase(int32_t((rpm-1800)*255/3800)):0):
        t<600?0:t<1800?characterEase(uint32_t(t-600)*255/1200):t<2600?255:t<4100?255-characterEase(uint32_t(t-2600)*255/1500):0;
    return {effort,int16_t(characterWave(now,2400)*2),int16_t(characterWave(now,1600,16)*(1+effort/128)),
        int16_t(characterWave(now,2400,8)*4),int16_t(characterWave(now,4800,16)*(1+effort/85))};
}
// Dashboard expression follows the configured RPM alarm, never a demo timer.
// Use measured RPM for prompt response while retaining the alarm's hysteresis.
inline CharacterPose gaugeCharacterPose(uint32_t now,float rpm,bool good,float warn,bool rpmAlarm) {
    CharacterPose pose=characterPose(now,rpm,good,true);
    pose.effort=0;
    if(good && std::isfinite(rpm) && std::isfinite(warn)) {
        pose.effort=rpmAlarm || rpm>=warn?255:
            characterEase(int32_t((rpm-(warn-600.0f))*255.0f/600.0f));
    }
    return pose;
}
inline void characterOffset(const CharacterPose& p,int16_t x,int16_t y,int16_t& dx,int16_t& dy) {
    const int16_t edge=characterClamp(min(min((x-256)*255/12,(479-x)*255/7),min((y-64)*255/12,(217-y)*255/10)),0,255);
    const int16_t head=y<173?255:characterClamp((211-y)*255/38,0,255);
    const int16_t hair=characterClamp((abs(x-391)-46)*255/60,0,255),hands=characterClamp((y-157)*255/41,0,255);
    dx=((int32_t(p.sway)*head+int32_t(p.hair)*hair)/255)*edge/255;
    dy=((int32_t(p.nod)*head+int32_t(p.steer)*(x-397)*hands/90)/255)*edge/255;
}
} // namespace obd
