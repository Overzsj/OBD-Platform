// ============================================================
//  pid_defs.h — PID 定义表
//  目标车辆: 三菱翼神 1.8 MT (4B10, 2011) — 汽油机, 手动变速箱
//  ！MT 无 CVT, 因此不定义任何变速箱油温/滑轮比类 PID
// ============================================================
#pragma once
#include <stdint.h>
#include "../app/build_mode.h"

namespace obd {

// ---- 数据来源模式 ------------------------------------------
enum class Mode : uint8_t {
    Mode01 = 0x01,   // 标准 OBD-II 实时数据
    Mode22 = 0x22,   // 厂商私有 (三菱需实测探测)
};

// 换算类型：决定如何把回包字节转成浮点值
enum class Conv : uint8_t {
    Raw1,        // A
    BenchHundredths, // private bench F0/F1: uint16 / 100
    Maf,         // uint16 / 100 g/s
    CommandedRatio, // uint16 / 32768; target only, not measured AFR
    Raw2,        // (A*256)+B
    Rpm,         // ((A*256)+B)/4
    TempC,       // A-40        (按摄氏度, 协议本就是摄氏)
    Percent,     // A*100/255
    Volt,        // ((A*256)+B)/1000
    Timing,      // (A/2)-64    (点火提前角)
    FuelTrim,    // (A-128)*100/128
    Pressure,    // A           (kPa 绝对压力)
    Custom,      // 交给自定义换算函数 (Mode22 用)
};

// PID 类别：决定报警与显示语义
enum class Kind : uint8_t {
    Engine,      // 转速等发动机参数
    Vehicle,     // 车速
    Thermal,     // 温度类 (报警重点)
    Electrical,  // 电压
    Emissions,   // 排放/燃油修正 (仅诊断用, 不上表盘)
};

struct PidDef {
    uint8_t  pid;           // PID 编号 (如 0x0C)
    Mode     mode;
    Conv     conv;
    Kind     kind;
    uint8_t  bytes;         // 回包数据字节数 —— 批量请求解析的关键!
    uint8_t  decimals;      // 显示小数位
    float    vmin;
    float    vmax;
    uint16_t minIntervalMs; // 最小读取间隔 (避免把 ECU 问爆)
    const char* name;       // 显示名 (ASCII, 屏上字库安全)
    const char* unit;
    bool     enabled;       // 是否参与轮询
};

// ------------------------------------------------------------
//  当前六项界面原型只采集已验证的转速、水温、节气门。
//  油温、机油压力、空燃比实车来源待后续确认，不能用别的压力替代。
//  注意 bytes 字段：批量请求解析时必须靠它推进游标，
//  因为 0x0C 返回 2 字节而 0x0D/0x05 只返回 1 字节。
// ------------------------------------------------------------
#if OBD_BENCH_AUX
constexpr bool kBenchAux = true;
#else
constexpr bool kBenchAux = false;
#endif
constexpr PidDef kPidTable[] = {

    //  pid    mode          conv        kind              bytes dec  min    max    ms    名称          单位    启用
    { 0x0C, Mode::Mode01, Conv::Rpm,      Kind::Engine,      2,  0,     0,  OBD_AUTO_ADAPT?16383.75f:7000,  100, "RPM",        "rpm",  true  },
    { 0x0D, Mode::Mode01, Conv::Raw1,     Kind::Vehicle,     1,  0,     0,   OBD_AUTO_ADAPT?255:240,  200, "SPEED",      "km/h", true  },
    { 0x05, Mode::Mode01, Conv::TempC,    Kind::Thermal,     1,  0,   -40,   OBD_AUTO_ADAPT?215:150,  500, "COOLANT",    "C",    true  },
    { 0x42, Mode::Mode01, Conv::Volt,     Kind::Electrical,  2,  2,     0,    OBD_AUTO_ADAPT?65.535f:16, 1000, "BATTERY",    "V",    bool(OBD_DATA_TEST) },
    { 0x11, Mode::Mode01, Conv::Percent,  Kind::Engine,      1,  0,     0,   100,  200, "THROTTLE",   "%",    true  },

    // 以下默认关闭，P3/P4 阶段按需打开
    { 0x04, Mode::Mode01, Conv::Percent,  Kind::Engine,      1,  0,     0,   100,  500, "ENG LOAD",   "%",    false },
    { 0x0F, Mode::Mode01, Conv::TempC,    Kind::Thermal,     1,  0,   -40,   OBD_AUTO_ADAPT?215:100, 1000, "INTAKE AIR", "C",    false },
    { 0x10, Mode::Mode01, Conv::Maf,     Kind::Engine,      2,  2,     0, 655.35,  200, "MAF",        "g/s",  false },
    { 0x0B, Mode::Mode01, Conv::Pressure, Kind::Engine,      1,  0,     0,   255,  200, "MAP",        "kPa",  false },
    { 0x0E, Mode::Mode01, Conv::Timing,   Kind::Engine,      1,  1,   -64,    63,  200, "TIMING ADV", "deg",  false },
    { 0x06, Mode::Mode01, Conv::FuelTrim, Kind::Emissions,   1,  1,  -100,   100,  500, "STFT B1",    "%",    false },
    { 0x07, Mode::Mode01, Conv::FuelTrim, Kind::Emissions,   1,  1,  -100,   100,  500, "LTFT B1",    "%",    false },
    { 0x0A, Mode::Mode01, Conv::Pressure, Kind::Engine,      1,  0,     0,   765,  500, "FUEL PRESS", "kPa",  false },
    { 0x5C, Mode::Mode01, Conv::TempC,    Kind::Thermal,     1,  0,   -40,   OBD_AUTO_ADAPT?215:210, 500, "OIL TEMP",   "C",    kBenchAux },
    { 0x44, Mode::Mode01, Conv::CommandedRatio, Kind::Emissions, 2, 3, 0, 2, 2000, "TARGET RATIO", "ratio", false },
#if OBD_BENCH_AUX
    // PRIVATE TEST CHANNELS. Not standard OBD or Mitsubishi PID mappings.
    { 0xF0, Mode::Mode01, Conv::BenchHundredths, Kind::Engine, 2, 1, 0, 10, 300, "TEST OIL P", "bar", true },
    { 0xF1, Mode::Mode01, Conv::BenchHundredths, Kind::Engine, 2, 1, 8, 25, 300, "TEST AFR", ":1", true },
#endif
};

constexpr size_t kPidCount = sizeof(kPidTable) / sizeof(kPidTable[0]);

// 屏上要显示的完整读数个数（enabled 的个数在运行时统计）
constexpr size_t kMaxDisplayPids = 8;

// 单个 PID 的运行时状态（与静态定义分离）
struct PidState {
    float    value;        // 最近一次成功读到的值
    uint32_t lastOkMs;     // 最近一次成功的时间戳
    uint32_t lastReqMs;    // 最近一次发起请求的时间戳
    uint32_t okCount;      // 累计成功次数
    uint32_t failCount;    // 累计失败次数
    bool     everOk;       // 是否曾经成功过（区分"没读到"与"不支持"）
    bool     suspect;      // 连续失败过多，暂时降级
};

} // namespace obd
