// ============================================================
//  vehicle_state.h — 采集侧与显示侧之间的数据契约
//
//  设计要点（修正参考项目的缺陷）：
//  参考项目 luca86r/obd2-car-monitor 用 `volatile float fValue`
//  跨核共享，注释写着 "written by ELM327 task, read by main loop"。
//  但 volatile 只禁止编译器优化掉读取，它：
//    * 不保证 4 字节写入的原子性（理论可读到半新半旧的位模式）
//    * 不提供内存屏障，读侧可能看到写侧的重排序
//  正确做法是让"采集侧产生不可变快照、显示侧整份读取"，
//  并用序列号（seqlock）校验读取期间没有发生写入。
// ============================================================
#pragma once
#include <stdint.h>
#include <string.h>
#include "../obd/pid_defs.h"
#include "vehicle_profile.h"
#include "device_settings.h"
#include "display_settings.h"
#include "../obd/support_probe.h"

namespace obd {

// TFT validity is tracked per PID: an unsupported/old value must not become a
// zero or a fabricated reading just because another PID has responded.
enum ValueField : uint8_t {
    VF_RPM, VF_OIL_TEMP, VF_COOLANT, VF_OIL_PRESSURE, VF_AFR, VF_THROTTLE,
    VF_SPEED, VF_VOLTS, VF_COUNT  // legacy diagnostic fields; not displayed/polled
};

// 采集侧→显示侧的一份完整快照。
// 只放"显示与报警需要的最终物理量"，不放协议细节。
struct VehicleSnapshot {
    // ---- 物理量（单位见 pid_defs.h 的 unit 字段）----
    float rpm;          // 转速
    float speed;        // 车速 km/h
    float coolantC;     // 冷却液温度
    float adapterVolts=0;uint32_t adapterVoltsMs=0;bool adapterVoltsValid=false;
    uint8_t probeStates[3]{};int8_t oilReported=-1,voltsReported=-1;
    float candidateValues[4]{};uint32_t candidateMs[4]{};uint8_t candidateStates[4]{};
    float volts;        // 模块电压
    float throttle;     // 节气门 %
    float oilTempC;      // 油温 °C；当前原型实车来源未接入
    float oilPressureBar; // 机油压力 bar；只有明确来源才标记有效
    float afr;          // 空燃比；模拟值不能写入实车模式
    float extra[kMaxDisplayPids];   // 其余可选 PID，按启用顺序排列
    uint8_t extraCount;
    uint8_t validMask;
    uint32_t valueMs[VF_COUNT];

    // ---- 健康状态 ----
    uint32_t lastOkMs;      // 最近一次任意 PID 成功的时间
    uint32_t totalOk;       // 累计成功
    uint32_t totalFail;     // 累计失败
    uint8_t  linkQuality;   // 0..100 粗略链路质量（成功/尝试）
    bool     linkUp;        // 蓝牙+ELM327 是否就绪

    // ---- 报警锁存（由报警模块计算后回填，便于 UI 直接读取）----
    uint8_t  alarmFlags;
    uint8_t estimatedGear; // 0=unknown, positive=estimated, never a direct ECU gear.
    DeviceSettings settings{};
    DisplaySettings display{};
    PresentationOptions presentation{};
    uint8_t elmState=0,transportState=0;
    uint32_t reconnects=0,reinits=0;
    uint8_t testStage=255,eventCount=0;
    uint32_t testPhaseMs=0,testCycle=0;
};

inline bool valueFresh(const VehicleSnapshot& s, ValueField field, uint32_t now) {
    const uint8_t pids[VF_COUNT]={0x0C,0x5C,0x05,0xF0,0xF1,0x11,0x0D,0x42};
    return field<VF_COUNT && s.linkUp && (s.validMask&(1u<<field)) &&
        uint32_t(now-s.valueMs[field])<=profileStaleMs(pids[field]);
}

// Shared producer mapping, also exercised by host pipeline tests.
inline void syncProbeSnapshot(const SupportProbe& probe,VehicleSnapshot& s){
    s.oilReported=probe.reported(0x5C);s.voltsReported=probe.reported(0x42);
    const uint8_t ids[]={4,3,5};
    for(uint8_t i=0;i<3;++i)s.probeStates[i]=uint8_t(probe.results[ids[i]].state);
    for(uint8_t i=0;i<4;++i){const auto& r=probe.results[i+6];s.candidateStates[i]=uint8_t(r.state);s.candidateValues[i]=r.value;s.candidateMs[i]=r.ms;}
}
inline bool candidateFresh(const VehicleSnapshot& s,uint8_t i,uint32_t now){
    return i<4&&s.linkUp&&s.candidateStates[i]==uint8_t(ProbeState::Valid)&&isfinite(s.candidateValues[i])&&uint32_t(now-s.candidateMs[i])<=15000;
}
enum class ReadingStatus:uint8_t {Missing,Fresh,Stale,Disconnected};
inline ReadingStatus candidateStatus(const VehicleSnapshot& s,uint8_t i,uint32_t now){
    if(!s.transportState)return ReadingStatus::Disconnected;
    if(candidateFresh(s,i,now))return ReadingStatus::Fresh;
    return i<4&&s.candidateStates[i]==uint8_t(ProbeState::Valid)?ReadingStatus::Stale:ReadingStatus::Missing;
}
inline ReadingStatus fieldStatus(const VehicleSnapshot& s,ValueField f,uint32_t now){
    if(!s.transportState)return ReadingStatus::Disconnected;
    if(valueFresh(s,f,now))return ReadingStatus::Fresh;
    return s.validMask&(1u<<f)?ReadingStatus::Stale:ReadingStatus::Missing;
}
inline uint8_t audibleSnapshotFlags(const VehicleSnapshot& s){
    uint8_t flags=s.alarmFlags;
    if(!(s.presentation.sounds&2))flags&=uint8_t(~0x22);
    if(!(s.presentation.sounds&4))flags&=uint8_t(~0x11);
#if OBD_DATA_TEST
    if(s.testStage==1||s.testStage==13)flags&=uint8_t(~0x11);
#endif
    return flags;
}

// ------------------------------------------------------------
//  Seqlock：写侧独占，读侧无锁重试
//  seq 为奇数 => 正在写入；偶数 => 数据稳定
// ------------------------------------------------------------
class SnapshotStore {
public:
    // 写侧：采集任务调用
    void write(const VehicleSnapshot& s) {
        uint32_t s0 = seq_;
        seq_ = s0 + 1;              // 变奇：标记"写入中"
        __sync_synchronize();       // 屏障：确保数据写入不早于 seq 自增
        data_ = s;
        __sync_synchronize();
        seq_ = s0 + 2;              // 变偶：标记"稳定"
    }

    // 读侧：显示任务调用。返回 false 表示采集中，调用方应沿用上一帧。
    bool read(VehicleSnapshot& out) const {
        uint32_t s1 = seq_;
        if (s1 & 1u) return false;  // 正在写，直接放弃本次
        __sync_synchronize();
        out = data_;
        __sync_synchronize();
        uint32_t s2 = seq_;
        return (s1 == s2) && !(s2 & 1u);   // 两次读数一致才算成功
    }

private:
    volatile uint32_t seq_ = 0;
    VehicleSnapshot   data_{};
};

// ------------------------------------------------------------
//  显示侧插值与缓动
//
//  为什么需要：ELM327 单 PID 往返约 25-60ms，6 个参数轮询
//  实测只有约 2-6 Hz。若直接把采样值画到指针上，视觉上是"跳变"。
//  这里用一阶低通（指数趋近）把 2-6 Hz 的阶跃变成连续运动。
//
//  重要：报警量不要缓动。水温报警必须反映真实值，否则会延迟触发。
// ------------------------------------------------------------
class Smoother {
public:
    void seed(float v) { shown_ = v; }

    // dt: 距上次调用的秒数；tau: 时间常数(秒)，越小越"跟手"
    float update(float target, float dt, float tau) {
        if (tau <= 0.0f) { shown_ = target; return shown_; }
        // 1 - exp(-dt/tau) 的一阶近似，避免在 MCU 上调用 expf 的开销
        float k = dt / (tau + dt);
        shown_ += (target - shown_) * k;
        return shown_;
    }

    float value() const { return shown_; }

private:
    float shown_ = 0.0f;
};

} // namespace obd
