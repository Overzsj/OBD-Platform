// ============================================================
//  fake_elm.h  片上假 ELM327（测试台的传输层替身）
//
//  为什么需要：真适配器还没到、车也没有。把"假 ELM327"做成一个
//  传输层实现，就能让 采集解析调度报警显示 整链在桌面上
//  闭环跑起来，不依赖适配器、不依赖 PC、不依赖蓝牙。
//
//  它同时扮演两个角色：
//     Transport：begin/tick/isConnected/available/read/write
//     ELM327   ：认 AT 命令、按请求回数据帧、以 '>' 收尾
//
//  仿真车况是一段可复现剧本（怠速加速巡航超温回落+掉压），
//  专门设计成能触发全部四条报警（水温高/转速高/车速高/电压低），
//  包含 warn 与 crit 两级，跑完取模自动循环。
// ============================================================
#pragma once
#include <Arduino.h>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include "../obd/elm_client.h"
#include "../obd/pid_defs.h"
#include "data_script.h"

namespace obd {

class FakeElmIo : public ElmClient::Io {
public:
    static constexpr size_t   kRxCap     = 256;
    static constexpr size_t   kCmdCap    = 32;
    static constexpr uint16_t kLatencyMs = 18;      // 模拟真实单次往返 25~60ms 的下限

    // ---------------- Transport 接口 ----------------
    bool begin(const char* = nullptr) {
        rxLen_ = cmdLen_ = 0;
        connected_ = true;
        t0_ = millis();
        pending_ = false;
        spaces_ = true;      // 真实 ELM327 上电默认：回显开、空格开
        echo_   = true;
        pendingResp_[0] = 0;
        return true;
    }

    void tick(uint32_t now) {
#if OBD_DATA_TEST
        const auto sample=labSample(labStage(now),labPhaseMs(now));
        connected_=sample.connected;
        if(!connected_ || sample.silent) {pending_=false;rxLen_=cmdLen_=0;return;}
#endif
        if (pending_ && (int32_t)(now - respAt_) >= 0) {
            push(pendingResp_);
            pending_ = false;
        }
    }

    bool isConnected() { return connected_; }
    bool connected() override { return connected_; }   // ElmClient::Io 要求的名字
    int  available()   { return (int)rxLen_; }
    int  read() {
        if (rxLen_ == 0) return -1;
        int c = rx_[0];
        memmove(rx_, rx_ + 1, --rxLen_);
        return c;
    }

    size_t write(const char* buf, size_t n) override {
        for (size_t i = 0; i < n; ++i) handleChar(buf[i]);
        return n;
    }

    bool   supported() const { return true; }
    String describeState() const { return String("FAKE/") + phaseName(); }
#if OBD_DATA_TEST
    uint8_t labStage(uint32_t now) const {return forcedStage_>=0?uint8_t(forcedStage_):uint32_t(now-t0_)%kLabCycleMs/kLabStageMs;}
    uint32_t labPhaseMs(uint32_t now) const {return uint32_t(now-t0_)%kLabStageMs;}
    uint32_t labCycle(uint32_t now) const {return uint32_t(now-t0_)/kLabCycleMs+1;}
    void labControl(int8_t stage,uint32_t now) {forcedStage_=stage;t0_=now;rxLen_=cmdLen_=0;pending_=false;}
#endif

    // UI prototype data only. These values do not represent an OBD pressure PID
    // or a calibrated real sensor and are compiled solely into the FAKE target.
    void sampleDisplayAux(uint32_t now,float& oilC,float& oilBar,float& afr) const {
        const float t=((now-t0_)%kCycleMs)/1000.0f;
        if (t<6) { oilC=20+t*1.4f; oilBar=1.3f; afr=14.7f; }
        else if (t<18) { const float k=(t-6)/12; oilC=28+k*30; oilBar=1.3f+k*2.7f; afr=14.7f-k*1.8f; }
        else if (t<30) { const float k=(t-18)/12; oilC=90+k*6; oilBar=3.2f; afr=14.7f; }
        else if (t<38) { const float k=(t-30)/8; oilC=118+k*16; oilBar=4.6f; afr=12.2f; }
        else { const float k=(t-38)/10; oilC=130-k*30; oilBar=3; afr=14.7f; }
        oilC=int(oilC+.5f);
    }

private:
    // ---------------- 仿真车况 ----------------
    enum Phase { PH_IDLE, PH_ACCEL, PH_CRUISE, PH_OVERHEAT, PH_COOLDOWN };
    static constexpr uint32_t kCycleMs = 48000;

    struct Sim { float rpm, speed, coolant, volts, throttle; };

    void stepSim(uint32_t t) {                     // t 属于 [0, kCycleMs)
#if OBD_DATA_TEST
        const auto sample=labSample(labStage(millis()),labPhaseMs(millis()));
        s_.rpm=sample.rpm;s_.speed=sample.speed;s_.coolant=sample.water;s_.volts=sample.volts;s_.throttle=sample.tps;
        return;
#endif
        if (t < 6000) {                            // 怠速暖机
            ph_ = PH_IDLE;
            s_.rpm = 850; s_.speed = 0;
            s_.coolant = 20.0f + t * 3.0f / 1000.0f;
            s_.volts = 14.2f; s_.throttle = 4.0f;
        } else if (t < 18000) {                    // 加速
            float k = (t - 6000) / 12000.0f;
            ph_ = PH_ACCEL;
            s_.rpm = 850 + k * 3000; s_.speed = k * 85;
            s_.coolant = 38 + k * 40;
            s_.volts = 13.9f; s_.throttle = 4 + k * 45;
        } else if (t < 30000) {                    // 巡航
            ph_ = PH_CRUISE;
            s_.rpm = 2650; s_.speed = 88;
            s_.coolant = 88 + (t - 18000) / 12000.0f * 8;
            s_.volts = 14.1f; s_.throttle = 22;
        } else if (t < 38000) {                    // 超温 + 超转 + 超速
            float k = (t - 30000) / 8000.0f;
            ph_ = PH_OVERHEAT;
            s_.rpm = 6300 + k * 500;               // 越过 warn 6200，末段越过 crit 6600
            s_.speed = 112 + k * 30;               // 越过 warn 120，末段越过 crit 140
            s_.coolant = 98 + k * 14;              // 越过 warn 100，末段越过 crit 108
            s_.volts = 13.6f; s_.throttle = 78;
        } else {                                   // 回落 + 电压跌落（覆盖低报分支）
            float k = (t - 38000) / 10000.0f;
            ph_ = PH_COOLDOWN;
            s_.rpm = 2400; s_.speed = 70;
            s_.coolant = 105 - k * 20;
            // 掉压窗口必须够长：报警去抖改成"按样本"语义后，
            // BATTERY 需 5 样本1000ms + 2000ms 保持  7 秒才会报，
            // 原来的 2 秒窗口已经测不出这条报警了（2026-10-03）。
            s_.volts = (t > 39000 && t < 49000) ? 10.8f : 14.2f;   // 越过低报 warn12/crit11，持续 9 秒
            s_.throttle = 18;
        }
    }

    const char* phaseName() const {
#if OBD_DATA_TEST
        return labStageName(labStage(millis()));
#endif
        switch (ph_) {
            case PH_IDLE:     return "IDLE";
            case PH_ACCEL:    return "ACCEL";
            case PH_CRUISE:   return "CRUISE";
            case PH_OVERHEAT: return "OVERHEAT";
            default:          return "COOLDOWN";
        }
    }

    // ---------------- 请求解析 ----------------
    void handleChar(char c) {
        if (c == '\r' || c == '\n') {
            if (cmdLen_) { cmd_[cmdLen_] = 0; handleLine(); cmdLen_ = 0; }
            return;
        }
        if (cmdLen_ + 1 < kCmdCap) cmd_[cmdLen_++] = c;
    }

    void handleLine() {
        char* p = cmd_;
        while (*p == ' ') ++p;                     // 去前导空格
        stepSim((millis() - t0_) % kCycleMs);
#if OBD_DATA_TEST
        const auto sample=labSample(labStage(millis()),labPhaseMs(millis()));
        if(sample.silent || !sample.connected)return;
#endif

        char out[96];
        if (strncasecmp(p, "AT", 2) == 0) {
            if (strncasecmp(p, "ATZ", 3) == 0) {          // 复位：回版本号并恢复默认
                spaces_ = true; echo_ = true;
                snprintf(out, sizeof(out), "ELM327 v1.5\r\r>");
            } else if (strncasecmp(p, "ATDP", 4) == 0) {
                snprintf(out,sizeof(out),"SIMULATED CAN\r\r>");
            } else if (strncasecmp(p, "ATE", 3) == 0) {
                echo_ = (p[3] != '0');
                snprintf(out, sizeof(out), "OK\r\r>");
            } else if (strncasecmp(p, "ATS", 3) == 0) {
                spaces_ = (p[3] != '0');
                snprintf(out, sizeof(out), "OK\r\r>");
            } else if (strncasecmp(p, "ATL", 3) == 0 || strncasecmp(p, "ATH", 3) == 0 ||
                       strncasecmp(p, "ATAT", 4) == 0 || strncasecmp(p, "ATSP", 4) == 0 ||
                       strncasecmp(p, "ATCAF", 5) == 0) {
                snprintf(out, sizeof(out), "OK\r\r>");
            } else if (strncasecmp(p, "AT RV", 5) == 0) {
                snprintf(out, sizeof(out), "%.1fV\r\r>", s_.volts);
            } else {
                snprintf(out, sizeof(out), "?\r\r>");      // 山寨适配器的典型反应
            }
        } else {
            buildDataReply(p, out, sizeof(out));
        }

        pendingResp_[0] = 0;                       // 回显（ATE1 时适配器会把命令回显一遍）
        if (echo_) snprintf(pendingResp_, sizeof(pendingResp_), "%s\r", p);
        strncat(pendingResp_, out, sizeof(pendingResp_) - strlen(pendingResp_) - 1);
        respAt_  = millis() + kLatencyMs;
        pending_ = true;
    }

    // 把 "01 0C 0D 05" 这类请求翻成 "410C1AF8410D3C41057B"
    void buildDataReply(const char* req, char* out, size_t cap) {
        uint8_t mode = 0, pids[8], np = 0;
        const char* q = req;
        while (*q && np < 8) {
            while (*q == ' ') ++q;
            if (!isxdigit((unsigned char)q[0]) || !isxdigit((unsigned char)q[1])) break;
            char two[3] = { q[0], q[1], 0 };
            uint8_t v = (uint8_t)strtoul(two, nullptr, 16);
            if (mode == 0 && np == 0) mode = v; else pids[np++] = v;
            q += 2;
        }
        if (np == 0) { snprintf(out, cap, "NO DATA\r\r>"); return; }

        size_t j = 0; uint8_t hit = 0;
        for (uint8_t i = 0; i < np; ++i) {
            char dat[16];
            if (!encodePid(pids[i], dat, sizeof(dat))) continue;
            hit++;
            // 回包格式必须是 [模式+0x40][PID][数据]，漏掉 PID 字节
            // 会让客户端拿到 "411AF8" 这种无主数据，全部解析失败。
            j += (size_t)snprintf(out + j, cap - j, "%02X%s%02X%s%s",
                                  (uint8_t)(mode + 0x40), spaces_ ? " " : "",
                                  pids[i],               spaces_ ? " " : "", dat);
        }
        if (!hit) { snprintf(out, cap, "NO DATA\r\r>"); return; }
        strncat(out, "\r\r>", cap - strlen(out) - 1);
    }

    // 物理量  协议字节（与 pid_convert.h 互为逆运算）
    bool encodePid(uint8_t pid, char* out, size_t cap) {
#if OBD_DATA_TEST
        const auto sample=labSample(labStage(millis()),labPhaseMs(millis()));
        if(pid==0x05 && sample.waterMissing)return false;
        if(pid==0x0F){snprintf(out,cap,"%02X",unsigned(sample.intake+40+.5f));return true;}
        if(pid==0x10){snprintf(out,cap,"%04X",unsigned(sample.maf*100+.5f));return true;}
        if(pid==0x04){snprintf(out,cap,"%02X",unsigned(sample.load*255/100+.5f));return true;}
        if(pid==0x5C) {snprintf(out,cap,"%02X",unsigned(sample.oil+40+.5f));return true;}
        if(pid==0xF0) {snprintf(out,cap,"%04X",unsigned(sample.pressure*100+.5f));return true;}
        if(pid==0xF1) {snprintf(out,cap,"%04X",unsigned(sample.afr*100+.5f));return true;}
#endif
        switch (pid) {
            case 0x0C: snprintf(out, cap, "%04X", (unsigned)(s_.rpm * 4.0f + 0.5f));             return true;
            case 0x0D: snprintf(out, cap, "%02X", (unsigned)(s_.speed + 0.5f));                  return true;
            case 0x05: snprintf(out, cap, "%02X", (unsigned)(s_.coolant + 40.0f + 0.5f));        return true;
            case 0x42: snprintf(out, cap, "%04X", (unsigned)(s_.volts * 1000.0f + 0.5f));        return true;
            case 0x11: snprintf(out, cap, "%02X", (unsigned)(s_.throttle * 255.0f / 100.0f + 0.5f)); return true;
            default:   return false;
        }
    }

    void push(const char* s) {
        size_t n = strlen(s);
        for (size_t i = 0; i < n && rxLen_ < kRxCap; ++i) rx_[rxLen_++] = s[i];
    }

    // ---------------- 状态 ----------------
    uint8_t rx_[kRxCap] = {0};   size_t rxLen_ = 0;
    char    cmd_[kCmdCap] = {0}; size_t cmdLen_ = 0;
    char    pendingResp_[160] = {0};
    bool    pending_ = false;    uint32_t respAt_ = 0;
    bool    connected_ = false;  uint32_t t0_ = 0;
    bool    spaces_ = true, echo_ = true;
    Phase   ph_ = PH_IDLE;
    Sim     s_{};
#if OBD_DATA_TEST
    int8_t forcedStage_=-1;
#endif
};

} // namespace obd
