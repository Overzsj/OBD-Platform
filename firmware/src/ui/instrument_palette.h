#pragma once
#include "themes/active_theme.h"
namespace obd { namespace instrument {
constexpr uint16_t rgb(uint8_t r,uint8_t g,uint8_t b) {return ((r&248)<<8)|((g&252)<<3)|(b>>3);}
// Use the active SUKI theme's exact accents, including its yellow numerals.
static const uint16_t &background=theme::kBg,&pink=theme::kPink,&cyan=theme::kCyan;
static const uint16_t &white=theme::kWhite,&muted=theme::kMuted,&yellow=theme::kYellow;
constexpr uint16_t panel=rgb(12,8,19),header=rgb(30,16,43),edge=rgb(72,38,120);
constexpr uint16_t critical=rgb(240,69,98);
}}
