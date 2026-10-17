// ============================================================
//  pid_convert.h — 原始字节 → 物理量 换算
//
//  为什么单独成文件：换算公式是纯函数，可以在 PC 上单测，
//  不需要硬件。整数运算优先，避免 ESP32 上 float 的迭代误差。
// ============================================================
#pragma once
#include <stdint.h>
#include <math.h>
#include "pid_defs.h"

namespace obd {

// 把 p[0..n-1] 按换算类型转成物理量。
// 返回 NAN 表示数据不足或无法换算（调用方应丢弃该样本，不要当成 0）。
inline float convertValue(Conv c, const uint8_t* p, uint8_t n) {
    if (n == 0 || p == nullptr) return NAN;

    switch (c) {
        case Conv::Raw1:
            return (float)p[0];

        case Conv::BenchHundredths:
            return n < 2 ? NAN : float((uint16_t(p[0]) << 8) | p[1]) / 100.0f;

        case Conv::Maf:
            return n < 2 ? NAN : float((uint16_t(p[0]) << 8) | p[1]) / 100.0f;
        case Conv::CommandedRatio:
            return n < 2 ? NAN : float((uint16_t(p[0]) << 8) | p[1]) / 32768.0f;

        case Conv::Raw2:
            if (n < 2) return NAN;
            return (float)(((uint16_t)p[0] << 8) | p[1]);

        case Conv::Rpm:
            if (n < 2) return NAN;
            return (float)(((uint16_t)p[0] << 8) | p[1]) / 4.0f;

        case Conv::TempC:
            // 协议本身就是摄氏度，无需换算成华氏
            return (float)p[0] - 40.0f;

        case Conv::Percent:
            return (float)p[0] * 100.0f / 255.0f;

        case Conv::Volt:
            if (n < 2) return NAN;
            return (float)(((uint16_t)p[0] << 8) | p[1]) / 1000.0f;

        case Conv::Timing:
            return (float)p[0] / 2.0f - 64.0f;

        case Conv::FuelTrim:
            return ((float)p[0] - 128.0f) * 100.0f / 128.0f;

        case Conv::Pressure:
            return (float)p[0];

        case Conv::Custom:
            return (float)p[0];      // 由调用方覆写

        default:
            return NAN;
    }
}

// ------------------------------------------------------------
//  合理性检查
//  目的：把"解析错误"和"真实极值"区分开。
//  例如 VIN 抓包里的填充字节被当成水温，会得到 -40 或 215 这种值。
// ------------------------------------------------------------
inline bool plausible(const PidDef& def, float v) {
    if (isnan(v)) return false;
    // 留一点余量，避免边界值被误杀
    return (v >= def.vmin - 1.0f) && (v <= def.vmax + 1.0f);
}

} // namespace obd
