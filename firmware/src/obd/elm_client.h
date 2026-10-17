// ============================================================
//  elm_client.h — ELM327 文本协议客户端
//
//  设计要点：
//   1) 非阻塞状态机。参考项目用 ELMduino 的 nb_rx_state 做非阻塞读取，
//      这是正确思路；这里实现一个更小的等价物，避免依赖外部库
//      在断线/异常时的行为不可控。
//   2) 适配器能力探测。山寨 ELM327 的 AT 命令支持不全
//      （Linux can327 文档明确：版本号是假的、部分命令返回 ?、
//       AT CSM 不支持）。逐个试、记下结果、缓存，不硬编码假设。
//   3) 以 '>' 提示符判断响应结束，而不是固定延时。
// ============================================================
#pragma once
#include <Arduino.h>
#include <cstring>
#include <cstdio>
#include "pid_defs.h"
#include "elm_parse.h"
#include "pid_scheduler.h"

namespace obd {

// ------------------------------------------------------------
//  适配器能力位
// ------------------------------------------------------------
struct ElmCaps {
    bool echoOff;        // ATE0
    bool linefeedsOff;   // ATL0
    bool spacesOff;      // ATS0
    bool headersOff;     // ATH0
    bool autoProtocol;   // ATSP0
    bool autoTiming;     // ATAT1
    bool canAutoFormat;  // ATCAF0
    bool hasVoltageCmd;  // AT RV (读电压)
    char version[24];    // ATZ 返回的版本串
    char protocol[28];   // ATDP 返回的当前协议（诊断用）
    uint8_t atOkCount;   // 成功接受的 AT 命令数
    uint8_t atFailCount;
};

enum class ElmState : uint8_t {
    Idle = 0,
    Reset,          // 发 ATZ
    Probe,          // 逐个探测 AT 命令
    ApplyCaps,      // 回放探测结论
    Ready,          // 正常轮询
    Error,          // 需要重连
};

class ElmClient {
public:
    static constexpr uint16_t kRespBufSize = 512;
    static constexpr uint16_t kCmdBufSize  = 48;
    static constexpr uint32_t kPromptTimeoutMs = 600;   // 等 '>' 的常规超时
    static constexpr uint32_t kSearchTimeoutMs = 30000; // 自动搜索协议时的宽容窗口

    void begin() {
        enter(ElmState::Reset, millis());
        clearResp();
        cmdLen_        = 0;
        bytesOut_      = 0;
        bytesIn_       = 0;
        wantLen_       = 0;
        memset(&caps_, 0, sizeof(caps_));
        strcpy(caps_.version, "unknown");
        strcpy(caps_.protocol, "?");
    }

    ElmState state() const { return st_; }
    const ElmCaps& caps() const { return caps_; }

    // ------------------------------------------------------------
    //  由外部（蓝牙任务）在收到数据时调用，喂入字节
    // ------------------------------------------------------------
    void feed(const uint8_t* data, size_t n) {
        bytesIn_ += n;
        for (size_t i = 0; i < n; ++i) {
            char c = (char)data[i];
            if (c == '>') { promptSeen_ = true; continue; }
            if (respLen_ + 1 < kRespBufSize) resp_[respLen_++] = c;
        }
        // ！必须补 NUL 终止：classify()/parsePidReply() 是把 resp_ 当
        // C 字符串读的。少了这一句，较短的回包会连同上一次长回包的残留
        // 尾巴一起被解析  2026-10-03 分片测试实测：批量回包之后的
        // "41057B" 会凭空拖出上一次的 "410D3C..."，多出几个假读数。
        resp_[respLen_] = '\0';
    }

    // ------------------------------------------------------------
    //  主循环：需被周期性调用（建议 5-10ms 一次）
    //  io 抽象：返回实际写出的字节数；纯虚避免绑定具体蓝牙实现
    // ------------------------------------------------------------
    struct Io {
        // 把 buf 的 n 字节发给 ELM327，返回已发送字节数
        virtual size_t write(const char* buf, size_t n) = 0;
        // 是否已连接
        virtual bool connected() = 0;
    };

    void tick(Io& io, uint32_t now) {
        if (!io.connected()) {
            if (st_ != ElmState::Idle) enter(ElmState::Idle, now);
            return;
        }

        switch (st_) {
            case ElmState::Idle:
                enter(ElmState::Reset, now);
                break;

            case ElmState::Reset:
                // ！2026-10-03 测试台发现：原实现从不发 ATZ，只是干等 3 秒
                // 超时，于是版本号永远是 unknown、每次重连白费 3 秒。
                if (!resetSent_) {
                    clearResp(); promptSeen_ = false;
                    bytesOut_ += io.write("ATZ\r", 4);
                    resetSent_   = true;
                    resetSentMs_ = now;
                    break;
                }
                if (promptSeen_ || (now - resetSentMs_ > 3000)) {
                    captureVersion();
                    probeIdx_ = 0;
                    enter(ElmState::Probe, now);
                }
                break;

            case ElmState::Probe:
                tickProbe(io, now);
                break;

            case ElmState::ApplyCaps:
                // 探测已在 Probe 阶段逐条应用，这里只做收尾统计
                enter(ElmState::Ready, now);
                break;

            case ElmState::Ready:
                // 正常轮询由 poll() 驱动
                break;

            case ElmState::Error:
                if (now - stateEnteredMs_ > 3000) enter(ElmState::Reset, now);
                break;
        }
    }

    // ------------------------------------------------------------
    //  正常轮询：构造并发出一个（可能是批量的）请求
    //  返回 true 表示已发出请求，调用方应等 replyReady()
    // ------------------------------------------------------------
    bool poll(Io& io, PidScheduler& sched, uint32_t now) {
        if (st_ != ElmState::Ready) return false;
        if (awaiting_) return false;                  // 上一个还没回

        uint8_t idx[kMaxBatch];
        uint8_t n = sched.selectDue(now, idx, kMaxBatch);
        if (n == 0) return false;

        if (useBatch_ && n > 1) {
            cmdLen_ = buildBatchRequest(kPidTable, kPidCount, idx, n, cmd_, kCmdBufSize);
            if (batchProbeLeft_ > 0) {   // 试探期间才打，平时不刷屏
                char vb[24];
                Serial.printf("[批量] 发出: \"%s\"\r\n", vis(cmd_, vb, sizeof(vb)));
            }
        } else {
            // 单 PID 回退路径（批量不被支持时）
            const PidDef& d = kPidTable[idx[0]];
            uint8_t mode = (d.mode == Mode::Mode22) ? 0x22 : 0x01;
            cmdLen_ = (size_t)snprintf(cmd_, kCmdBufSize, "%02X %02X\r", mode, d.pid);
            n = 1;
        }
        if (cmdLen_ == 0) return false;

        clearResp();
        promptSeen_ = false;
        wantIdx_    = idx;  wantLen_ = n;
        memcpy(wantIdxLocal_, idx, n);
        wantIdx_    = wantIdxLocal_;

        bytesOut_ += io.write(cmd_, cmdLen_);
        sched.markRequested(wantIdx_, wantLen_, now);
        reqSentMs_ = now;
        awaiting_  = true;
        return true;
    }

    // 回包是否已到齐。
    // ！关键：适配器在自动搜索协议时会周期性回 "SEARCHING..."，而它**只要收到
    // 新命令就会中止搜索并回 "STOPPED"**。若按常规 600ms 超时就发下一条，
    // 搜索会被无限打断、永远锁不上协议  2026-10-04 在车上实测就是这个现象
    //（一直 SEARCHING/STOPPED 交替）。所以看到 SEARCHING 就把窗口放宽到 30 秒。
    bool replyReady(uint32_t now) const {
        if (!awaiting_) return false;
        if (promptSeen_) return true;
        if (strstr(resp_, "SEARCH") != nullptr) return (now - reqSentMs_) > kSearchTimeoutMs;
        return (now - reqSentMs_) > kPromptTimeoutMs;
    }

    // ------------------------------------------------------------
    //  处理回包：把结果写回调度器健康度，并返回解析出的 PID 个数
    // ------------------------------------------------------------
    uint8_t consumeReply(uint32_t now, PidScheduler& sched,
                         ParsedPid* out, uint8_t outCap) {
        uint8_t got = 0;
        ReplyKind kind = classify(resp_);

        lastPrompt_ = promptSeen_;
        if (promptSeen_ && (kind == ReplyKind::Data || kind == ReplyKind::Searching)) {
            const char* payload=resp_;
            got = parsePidReply(payload, kPidTable, kPidCount, out, outCap);
            uint8_t valid=0;
            for (uint8_t i=0;i<got;++i) {
                bool wanted=wantLen_==0;
                for (uint8_t k=0;k<wantLen_;++k)
                    wanted=wanted || out[i].pid==kPidTable[wantIdx_[k]].pid;
                if (wanted && isfinite(out[i].value)) out[valid++]=out[i];
            }
            got=valid;
            if (got) kind = ReplyKind::Data; // SEARCHING followed by a valid reply.
        }

        // 用解析结果更新每个 PID 的健康度
        uint8_t wantedOk = 0;
        for (uint8_t k = 0; k < wantLen_; ++k) {
            uint8_t idx = wantIdx_[k];
            bool ok = false;
            for (uint8_t g = 0; g < got; ++g) {
                if (out[g].pid == kPidTable[idx].pid && !isnan(out[g].value)) { ok = true; break; }
            }
            if (ok) { sched.reportOk(idx); ++wantedOk; }
            else    sched.reportFail(idx);
        }

        // Only an actual multi-PID request can confirm batch support.
        // Single-PID replies and raw scans must not consume probe attempts.
        // A later complete batch failure also falls back without waiting for reinit.
        if (wantLen_ > 1 && (batchProbeLeft_ > 0 || wantedOk == 0)) {
            char vb[200];
            Serial.printf("[批量] 试探: got=%u 回包=\"%s\"  -> %s\r\n",
                          (unsigned)got, vis(resp_, vb, sizeof(vb)),
                          wantedOk ? "批量可用，继续试探" : "批量不可用，退回单 PID");
            if (wantedOk == 0) { useBatch_ = false; batchProbeLeft_ = 0; }
            else if (--batchProbeLeft_ == 0) { useBatch_ = true; }
        }

        awaiting_   = false;
        snprintf(lastText_, sizeof(lastText_), "%s", resp_);   // 先存原文，再清缓冲
        clearResp();
        promptSeen_ = false;
        lastKind_   = kind;
        return got;
    }

    // 丢弃已缓冲的内容（体检开始前做同步用）
    void dropBuffered() { clearResp(); promptSeen_ = false; }

    // 是否正在等回包（PID 体检要据此决定何时发下一条）
    bool awaiting() const { return awaiting_; }
    bool searching() const {return awaiting_ && strstr(resp_,"SEARCH")!=nullptr;}

    // 直接发一条原始命令（绕过调度器与健康度统计），用于"真车 PID 体检"。
    // 返回 true = 已发出，调用方随后照常等 replyReady() / consumeReply()。
    bool pollRaw(Io& io, const char* cmd, uint32_t now) {
        if (st_ != ElmState::Ready || awaiting_) return false;
        size_t n = strlen(cmd);
        if (n == 0 || n >= kCmdBufSize) return false;
        memcpy(cmd_, cmd, n + 1);
        cmdLen_ = n;
        clearResp();
        promptSeen_ = false;
        wantLen_ = 0;                     // 0  consumeReply 不动健康度
        bytesOut_ += io.write(cmd_, cmdLen_);
        reqSentMs_ = now;
        awaiting_  = true;
        return true;
    }

    ReplyKind lastReplyKind() const { return lastKind_; }
    bool lastReplyHadPrompt() const { return lastPrompt_; }
    // 返回"上一条已消费的回包原文"。
    // ！不能直接返回 resp_：poll() 会立刻清空它开始收下一条，于是打印出来的
    // 文本和 lastKind_ 可能不是同一条回包（2026-10-04 诊断时踩到）。
    const char* lastReply() const { return lastText_; }
    const char* lastCommand() const { return cmd_; }
    bool usingBatch() const { return useBatch_; }
    uint32_t bytesOut() const { return bytesOut_; }
    uint32_t bytesIn() const { return bytesIn_; }

    // 启动批量试探：先试 N 次，全成功才长期启用
    void enableBatchProbe(uint8_t tries = 3) {
        batchProbeLeft_ = tries;
        useBatch_       = true;
    }

    void requestReinit() { enter(ElmState::Reset, millis()); }
#if OBD_DATA_TEST
    // Only the fake transport can safely discard both ends of a transaction.
    void resetSimulatedTransaction(){enter(st_==ElmState::Ready?ElmState::Ready:ElmState::Reset,millis());}
#endif

private:
    void enter(ElmState s, uint32_t now) {
        st_ = s;
        stateEnteredMs_ = now;
        resetSent_ = false;
        probeSent_ = false;
        awaiting_ = false;
        wantLen_ = 0;
        if (s == ElmState::Reset || s == ElmState::Idle) {
            probeIdx_ = 0;
            useBatch_ = false;
            batchProbeLeft_ = 0;
            caps_ = ElmCaps{};
        }
        clearResp();
        promptSeen_ = false;
        cmdLen_ = 0;
    }

    // 清空回包缓冲。只把长度置 0 是不够的  resp_ 是 C 字符串，
    // 必须同时写 '\0'，否则下一次较短的回包仍会读到上一次的残留。
    void clearResp() { respLen_ = 0; resp_[0] = '\0'; }

    void captureVersion() {
        // ATZ 回包形如 "ELM327 v1.5" 或含 "ELM327 v2.1"
        const char* p = strstr(resp_, "ELM327");
        if (!p) p = strstr(resp_, "ELM");
        if (p) {
            size_t i = 0;
            while (p[i] && p[i] != '\r' && p[i] != '\n' && i + 1 < sizeof(caps_.version)) {
                caps_.version[i] = p[i];
                ++i;
            }
            caps_.version[i] = '\0';
            // 去掉首尾空格
            while (i > 0 && caps_.version[i - 1] == ' ') caps_.version[--i] = '\0';
        }
    }

    // 探测表：逐条试，看返回是否含 '?'
    struct ProbeItem {
        const char* cmd;
        uint8_t     bit;     // 对应 ElmCaps 的哪一项
    };

    void tickProbe(Io& io, uint32_t now) {
        static const ProbeItem kProbes[] = {
            {"ATE0\r",  0},   // echoOff
            {"ATL0\r",  1},   // linefeedsOff
            {"ATS0\r",  2},   // spacesOff
            {"ATH0\r",  3},   // headersOff
            {"ATAT1\r", 5},   // autoTiming
            {kVehicleProfile.protocolCommand, 4},
            {"ATDP\r",  6},   // 查询当前协议（诊断用；不占 caps 位）
        };
        constexpr uint8_t kProbeCount = sizeof(kProbes) / sizeof(kProbes[0]);

        if (probeIdx_ >= kProbeCount) { enter(ElmState::ApplyCaps, now); return; }

        // 第一次进入某条探测：发命令
        if (!probeSent_) {
            clearResp(); promptSeen_ = false;
            bytesOut_ += io.write(kProbes[probeIdx_].cmd, strlen(kProbes[probeIdx_].cmd));
            probeSentMs_ = now;
            probeSent_ = true;
            return;
        }

        // 等回包或超时
        if (!promptSeen_ && (now - probeSentMs_) < 1500) return;

        if (kProbes[probeIdx_].bit == 6) {               // ATDP：抄下协议串
            size_t i = 0;
            while (resp_[i] && resp_[i] != '\r' && resp_[i] != '\n' && i + 1 < sizeof(caps_.protocol)) {
                caps_.protocol[i] = resp_[i];
                ++i;
            }
            caps_.protocol[i] = '\0';
            probeIdx_++;
            probeSent_ = false;
            return;
        }
        bool rejected = !promptSeen_ || strstr(resp_, "OK") == nullptr || strchr(resp_, '?') != nullptr;
        if (!rejected) {
            switch (kProbes[probeIdx_].bit) {
                case 0: caps_.echoOff      = true; break;
                case 1: caps_.linefeedsOff = true; break;
                case 2: caps_.spacesOff    = true; break;
                case 3: caps_.headersOff   = true; break;
                case 4: caps_.autoProtocol = true; break;
                case 5: caps_.autoTiming   = true; break;
            }
            caps_.atOkCount++;
        } else {
            caps_.atFailCount++;
        }

        probeIdx_++;
        probeSent_ = false;
    }

    ElmState st_ = ElmState::Idle;
    uint32_t stateEnteredMs_ = 0;

    char   resp_[kRespBufSize] = {0};
    uint16_t respLen_ = 0;
    bool   promptSeen_ = false;
    bool   lastPrompt_ = false;

    char   cmd_[kCmdBufSize] = {0};
    size_t cmdLen_ = 0;

    uint8_t wantIdxLocal_[kMaxBatch] = {0};
    const uint8_t* wantIdx_ = nullptr;
    uint8_t wantLen_ = 0;

    bool     awaiting_   = false;
    uint32_t reqSentMs_  = 0;
    ReplyKind lastKind_  = ReplyKind::Empty;
    char      lastText_[kRespBufSize] = {0};   // 最近一条已消费回包的原文（诊断用）

    // 探针
    bool     resetSent_  = false;
    uint32_t resetSentMs_= 0;
    uint8_t  probeIdx_   = 0;
    bool     probeSent_  = false;
    uint32_t probeSentMs_= 0;

    // 批量试探
    bool     useBatch_      = false;
    uint8_t  batchProbeLeft_= 0;

    uint32_t bytesOut_ = 0;
    uint32_t bytesIn_  = 0;

    ElmCaps caps_{};
};

} // namespace obd
