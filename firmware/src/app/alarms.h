// ============================================================
//  alarms.h — 报警状态机
//  目标: 超温 / 超转 / 超速 —— 对应三菱翼神 1.8 MT (4B10)
//
//  为什么不能简单写 `if (temp > 100) beep()`：
//    1) 被测量本身有噪声，单点比较会抖出满屏假报警
//    2) 总线偶发丢帧会让值短暂掉到 0，造成误解除
//    3) 阈值附近反复穿越会产生"咔咔咔"的蜂鸣器噪声
//  因此需要：连续样本去抖 + 持续时间确认 + 滞回解除
// ============================================================
#pragma once
#include <stdint.h>

namespace obd {

enum AlarmId : uint8_t {
    ALARM_COOLANT = 0,
    ALARM_RPM,
    ALARM_SPEED,
    ALARM_VOLT_LOW,
    ALARM_COUNT
};

enum class Sev : uint8_t { None = 0, Warn = 1, Critical = 2 };

struct AlarmCfg {
    float    warn;          // 预警阈值
    float    crit;          // 严重阈值
    float    hyst;          // 滞回量：解除需回落 warn - hyst
    uint8_t  debounceNeed;  // 连续越限样本数
    uint16_t holdMs;        // 持续越限时间才置位
    bool     enabled;
    bool     isLow;         // true = 低于阈值报警（如电压）
    const char* label;
};

struct AlarmState {
    bool     active;
    Sev      sev;
    float    peak;          // 本次报警期间的最差值（便于复盘）
    uint32_t sinceMs;
    uint16_t debounceCnt;
    uint32_t overSinceMs;
};

// ------------------------------------------------------------
//  项目默认提示阈值 —— 当前车型配置为 2011 翼神 1.8L / 4B10 / MT。
//  这些值仅用于外接仪表显示与提示，不是三菱原厂 ECU 保护标定。
//  未以中国市场该车的原厂手册核实红线、断油点或温度保护值；
//  不用其他市场 4B10 的功率、扭矩或车主断油记录推导安全上限。
//  用户提供断油点 6500 转：5000 提示、6000 红线预警；水温仍为可调设置。
// ------------------------------------------------------------
constexpr AlarmCfg kDefaultAlarms[ALARM_COUNT] = {
    // warn   crit   hyst  deb  holdMs  enabled isLow  label
    { 100.0f, 108.0f,  5.0f, 3,  200,  true,  false, "COOLANT" },  // 水温
    { 5000.0f, 6000.0f, 300.0f, 2, 100, true,  false, "RPM"     },  // 断油前预警
    { 120.0f, 140.0f,  5.0f, 3,  300,  true,  false, "SPEED"   },  // 车速(km/h)
    { 12.0f,  11.2f,   0.3f, 5, 2000,  true,  true,  "BATTERY" },  // 电压过低
};

// ------------------------------------------------------------
//  单个报警的评估。返回值是该报警当前的严重度。
//
//  over 由调用方按 isLow 语义先算好（越限=true），本函数只管时序逻辑。
// ------------------------------------------------------------
inline Sev evaluateAlarm(const AlarmCfg& cfg, AlarmState& st,
                         float value, bool over, uint32_t now) {
    if (!cfg.enabled) { st.active = false; st.sev = Sev::None; return Sev::None; }

    if (over) {
        // 去抖：连续 N 个样本都越限才认为可信
        if (st.debounceCnt < cfg.debounceNeed) st.debounceCnt++;
        if (st.debounceCnt >= cfg.debounceNeed) {
            if (st.overSinceMs == 0) st.overSinceMs = now;
            // 保持：持续越限超过 holdMs 才真正置位
            if (!st.active && (now - st.overSinceMs) >= cfg.holdMs) {
                st.active  = true;
                st.sinceMs = now;
                st.peak    = value;
            }
        }
        if (st.active) {
            // 记录本次报警期间最差值
            if (cfg.isLow) { if (value < st.peak) st.peak = value; }
            else           { if (value > st.peak) st.peak = value; }
        }
    } else {
        if (st.debounceCnt) st.debounceCnt--;
        if (st.debounceCnt == 0) st.overSinceMs = 0;

        // 滞回解除：必须回落到 warn - hyst 之外才解除，防止阈值抖动
        float release = cfg.isLow ? (cfg.warn + cfg.hyst) : (cfg.warn - cfg.hyst);
        bool released = cfg.isLow ? (value > release) : (value < release);
        if (st.active && released) {
            st.active = false;
            st.sev    = Sev::None;
        }
    }

    // 严重度分级（滞回思路一致）
    if (st.active) {
        bool isCrit = cfg.isLow ? (value <= cfg.crit) : (value >= cfg.crit);
        st.sev = isCrit ? Sev::Critical : Sev::Warn;
    } else {
        st.sev = Sev::None;
    }
    return st.sev;
}

// 便捷包装：按 isLow 语义自动算 over
inline Sev evaluateAlarmAuto(const AlarmCfg& cfg, AlarmState& st,
                             float value, uint32_t now) {
    bool over = cfg.isLow ? (value <= cfg.warn) : (value >= cfg.warn);
    return evaluateAlarm(cfg, st, value, over, now);
}

// 把全部报警状态压成位图，便于放进 VehicleSnapshot
inline uint8_t packAlarmFlags(const AlarmState (&st)[ALARM_COUNT]) {
    uint8_t f = 0;
    for (uint8_t i = 0; i < ALARM_COUNT; i++) {
        if (st[i].active)  f |= (1u << i);
        if (st[i].sev == Sev::Critical) f |= (1u << (i + 4));  // 高 4 位存"严重"
    }
    return f;
}

} // namespace obd
