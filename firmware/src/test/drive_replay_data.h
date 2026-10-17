#pragma once
#include <stdint.h>
namespace obd {
struct ReplayRow {uint32_t ms;float values[8];uint32_t ok,fail;uint8_t fresh,quality,state;float candidates[3];uint8_t candidateMask;};
constexpr uint32_t kReplayDuration=0,kReplaySourceBoot=0;
static const ReplayRow kReplayRows[]={
// Synthetic empty placeholder. No recorded vehicle samples distributed.
{0,{0,0,0,0,0,0,0,0},0,0,0,0,0,{0,0,0},0}
};
constexpr uint32_t kReplayCount=sizeof(kReplayRows)/sizeof(kReplayRows[0]);
}
