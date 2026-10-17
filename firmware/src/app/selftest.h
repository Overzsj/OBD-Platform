// ============================================================
//  selftest.h  协议层自检
//
//  为什么要在固件里跑自检：解析器的错误（换算系数写错、游标推进
//  错误）在车上表现为"水温显示 -40"、"转速总是 0"这类现象，很难
//  定位。趁没有硬件时把已知正确的回包格式固化成断言，硬件一到就
//  能立刻区分"是解析器错"还是"是链路错"。
//
//  真值来源：OBD-II 标准换算公式（SAE J1979）
//  ！教训（2026-10-03）：期望值不能手算。本文件第一版把 0x1AF8 的
//  转速写成 1728（正确是 6904/4 = 1726），一次产生三行假 FAIL 
//  手写期望值必须用独立实现交叉验证，见 tools/fake_elm327.py。
//
//  输出两种格式，缺一不可：
//    "  [PASS] name detail"    人看
//    "TEST|name|PASS"          机器看（PC 端脚本解析）
//    "SELFTEST|9/9|PASS"       总判
// ============================================================
#pragma once
#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include "../obd/pid_defs.h"
#include "../obd/elm_parse.h"
#include "../obd/pid_convert.h"

namespace obd {

// 自检记账：每条用例都过 result()，人读一行、机读一行
struct TestRun {
    uint8_t total  = 0;
    uint8_t passed = 0;
    void result(const char* name, bool ok, const char* detail) {
        ++total;
        if (ok) ++passed;
        Serial.printf("  [%s] %s %s\r\n", ok ? "PASS" : "FAIL", name, detail);
        Serial.printf("TEST|%s|%s\r\n", name, ok ? "PASS" : "FAIL");
    }
};

inline void runProtocolSelfTest() {
    Serial.println();
    Serial.println("===== 协议层自检 =====");
    TestRun run;
    char d[128];

    // ---- 单 PID: 转速 0x0C = ((A*256)+B)/4 ----
    // "410C1AF8" -> A=0x1A=26, B=0xF8=248 -> (26*256+248)=6904, /4 = 1726 rpm
    {
        ParsedPid out[4];
        uint8_t n = parsePidReply("410C1AF8", kPidTable, kPidCount, out, 4);
        bool ok = (n == 1) && (out[0].pid == 0x0C) && (fabsf(out[0].value - 1726.0f) < 0.5f);
        snprintf(d, sizeof(d), "410C1AF8 -> %.1f (期望 1726.0)", n ? out[0].value : -999.0f);
        run.result("rpm_single", ok, d);
    }

    // ---- 带空格的回包（若忘了发 ATS0）----
    {
        ParsedPid out[4];
        uint8_t n = parsePidReply("41 0C 1A F8", kPidTable, kPidCount, out, 4);
        bool ok = (n == 1) && (fabsf(out[0].value - 1726.0f) < 0.5f);
        snprintf(d, sizeof(d), "\"41 0C 1A F8\" -> %.1f", n ? out[0].value : -999.0f);
        run.result("rpm_spaced", ok, d);
    }

    // ---- 冷却液温度 0x05 = A-40 : "41057B" -> 123-40 = 83C ----
    {
        ParsedPid out[4];
        uint8_t n = parsePidReply("41057B", kPidTable, kPidCount, out, 4);
        bool ok = (n == 1) && (fabsf(out[0].value - 83.0f) < 0.1f);
        snprintf(d, sizeof(d), "41057B -> %.1f (期望 83.0)", n ? out[0].value : -999.0f);
        run.result("coolant", ok, d);
    }

    // ---- 车速 0x0D = A（单字节！游标推进的关键用例）: "410D3C" -> 60 ----
    {
        ParsedPid out[4];
        uint8_t n = parsePidReply("410D3C", kPidTable, kPidCount, out, 4);
        bool ok = (n == 1) && (fabsf(out[0].value - 60.0f) < 0.1f);
        snprintf(d, sizeof(d), "410D3C -> %.1f (期望 60.0)", n ? out[0].value : -999.0f);
        run.result("speed", ok, d);
    }

    // ---- 模块电压 0x42 = ((A*256)+B)/1000 : "414236B0" -> 14.000V ----
    {
        ParsedPid out[4];
        uint8_t n = parsePidReply("414236B0", kPidTable, kPidCount, out, 4);
        bool ok = (n == 1) && (fabsf(out[0].value - 14.0f) < 0.01f);
        snprintf(d, sizeof(d), "414236B0 -> %.3f (期望 14.000)", n ? out[0].value : -999.0f);
        run.result("battery", ok, d);
    }

    // ---- 批量回包：混合 2 字节与 1 字节 PID ----
    // 这是解析器最关键的用例：推进游标必须按每个 PID 的 bytes 字段
    {
        ParsedPid out[8];
        uint8_t n = parsePidReply("410C1AF8410D3C41057B", kPidTable, kPidCount, out, 8);
        bool ok = (n == 3)
               && (fabsf(out[0].value - 1726.0f) < 0.5f)
               && (fabsf(out[1].value - 60.0f)   < 0.1f)
               && (fabsf(out[2].value - 83.0f)   < 0.1f);
        size_t j = (size_t)snprintf(d, sizeof(d), "解析出 %u 个", n);
        for (uint8_t i = 0; i < n && j + 20 < sizeof(d); ++i)
            j += (size_t)snprintf(d + j, sizeof(d) - j, " [%02X]=%.1f", out[i].pid, out[i].value);
        run.result("batch3", ok, d);
    }

    // ---- 变体格式：模式字节只出现一次（4B10 真车实测格式）----
    {
        ParsedPid out[8];
        uint8_t n = parsePidReply("410C1AF80D3C057B", kPidTable, kPidCount, out, 8);
        bool ok = (n == 3)
               && (out[0].pid == 0x0C) && (fabsf(out[0].value - 1726.0f) < 0.5f)
               && (out[1].pid == 0x0D) && (fabsf(out[1].value - 60.0f)   < 0.1f)
               && (out[2].pid == 0x05) && (fabsf(out[2].value - 83.0f)   < 0.1f);
        size_t j = (size_t)snprintf(d, sizeof(d), "解析出 %u 个", n);
        for (uint8_t i = 0; i < n && j + 20 < sizeof(d); ++i)
            j += (size_t)snprintf(d + j, sizeof(d) - j, " [%02X]=%.1f", out[i].pid, out[i].value);
        run.result("batch_no_mode", ok, d);
    }

    // ---- 非数据响应必须被正确识别，不能当数值 ----
    {
        struct { const char* txt; ReplyKind want; } cases[] = {
            {"NO DATA",            ReplyKind::NoData},
            {"?",                  ReplyKind::Unsupported},
            {"SEARCHING...",       ReplyKind::Searching},
            {"UNABLE TO CONNECT",  ReplyKind::UnableToConnect},
            {"STOPPED",            ReplyKind::Stopped},
            {"CAN ERROR",          ReplyKind::CanError},
            {"BUFFER FULL",        ReplyKind::BufferFull},
        };
        uint8_t pass = 0, total = sizeof(cases) / sizeof(cases[0]);
        for (auto& c : cases) {
            ReplyKind got = classify(c.txt);
            ParsedPid out[4];
            uint8_t n = parsePidReply(c.txt, kPidTable, kPidCount, out, 4);
            if (got == c.want && n == 0) pass++;
            else Serial.printf("      -> \"%s\" 分类错误或误解析出 %u 个值\r\n", c.txt, n);
        }
        snprintf(d, sizeof(d), "非数据响应识别 %u/%u", pass, total);
        run.result("nondata", pass == total, d);
    }

    // ---- 量程校验：越界值必须被拒 ----
    {
        const PidDef& cool = kPidTable[2];      // COOLANT, vmin -40 / vmax 150
        bool ok = !plausible(cool, 300.0f) && plausible(cool, 90.0f);
        snprintf(d, sizeof(d), "300 应拒绝=%d, 90 应接受=%d",
                 (int)!plausible(cool, 300.0f), (int)plausible(cool, 90.0f));
        run.result("plausible", ok, d);
    }

    // ---- 批量请求构造 ----
    {
        uint8_t idx[3] = {0, 1, 2};             // RPM, SPEED, COOLANT
        char buf[32], got[48], want[24];
        size_t n = buildBatchRequest(kPidTable, kPidCount, idx, 3, buf, sizeof(buf));
        bool ok = (n > 0) && (strcmp(buf, "01 0C 0D 05\r") == 0);
        snprintf(d, sizeof(d), "\"%s\" (期望 \"%s\")",
                 vis(buf, got, sizeof(got)), vis("01 0C 0D 05\r", want, sizeof(want)));
        run.result("batreq", ok, d);
    }

    Serial.printf("===== 自检结束：%u/%u 通过 =====\r\n", (unsigned)run.passed, (unsigned)run.total);
    Serial.printf("SELFTEST|%u/%u|%s\r\n", (unsigned)run.passed, (unsigned)run.total,
                  run.passed == run.total ? "PASS" : "FAIL");
    Serial.println();
}

} // namespace obd