#pragma once
#include <stdint.h>
#include <stddef.h>
#include "build_mode.h"
#ifndef OBD_AUTO_ADAPT
#define OBD_AUTO_ADAPT 0
#endif

namespace obd {
enum class DataSource : uint8_t { StandardObd, BenchOnly, Unconfigured };
struct PidPolicy {
    uint8_t pid;
    uint16_t intervalMs;
    uint16_t staleMs;
    DataSource source;
};
// No manufacturer-specific PID or gearbox ratio is assumed to be verified.
constexpr PidPolicy kLancerPolicies[] = {
    {0x0C,100,2000,DataSource::StandardObd},
    {0x0D,200,2000,DataSource::StandardObd},
    {0x05,500,5000,DataSource::StandardObd},
    {0x11,200,3000,DataSource::StandardObd},
    {0x5C,500,7000,DataSource::BenchOnly},
    {0xF0,300,3000,DataSource::BenchOnly},
    {0xF1,300,3000,DataSource::BenchOnly},
    {0x42,1000,3000,DataSource::Unconfigured},
};
struct GearCalibration {
    bool calibrated;
    uint8_t count;
    float finalDrive;
    float rollingCircumferenceM;
    float ratios[6];
};
struct VehicleProfile {
    const char* id;
    const char* protocolCommand;
    GearCalibration gears;
};
constexpr VehicleProfile kVehicleProfile = {
    "STANDARD-OBD-AUTO", "ATSP0\r",
    {false,0,0,0,{0,0,0,0,0,0}}
};
inline const PidPolicy* pidPolicy(uint8_t pid) {
    for (const auto& p : kLancerPolicies) if (p.pid == pid) return &p;
    return nullptr;
}
inline bool profilePidEnabled(uint8_t pid) {
#if OBD_AUTO_ADAPT
    // Universal standard dashboard. Optional candidates stay on the bounded
    // support-probe queue; do not duplicate IAT/MAF/load requests here.
    if(pid==0x0C||pid==0x0D||pid==0x05||pid==0x11||pid==0x42||pid==0x5C)return true;
#endif
    const auto* p=pidPolicy(pid);
    if (!p) return false;
#if OBD_DATA_TEST
    return true;
#endif
    if (p->source==DataSource::StandardObd) return true;
#if OBD_BENCH_AUX
    return p->source==DataSource::BenchOnly;
#else
    return false;
#endif
}
inline uint16_t profileInterval(uint8_t pid, uint16_t fallback) {
    const auto* p=pidPolicy(pid); return p?p->intervalMs:fallback;
}
inline uint16_t profileStaleMs(uint8_t pid) {
    const auto* p=pidPolicy(pid); return p?p->staleMs:7000;
}
} // namespace obd
