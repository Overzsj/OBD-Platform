// ============================================================
//  pid_scheduler.h — 轮询调度器
//
//  参考项目 luca86r/obd2-car-monitor 的做法是"找下一个该读的 PID"：
//
//      while (!found) {
//          next = (abs(next + 1)) % MANAGED_PIDS_COUNT;
//          found = isRecentlyGetData(next) && isTimeToReadFromEml(next);
//      }
//
//  这里有个隐患：若所有 PID 都在节流窗口内（例如刚启动时 DPF 类
//  PID 的最小间隔是 10000ms），这个 while 会**无限空转**，卡死采集任务。
//
//  本实现改为"扫一轮、把到期的都收进来"：
//    * 一轮固定 O(n)，天然有界，不存在死循环
//    * 一次可带多个 PID（批量请求），摊薄 ELM327 往返开销
//    * 没到期就返回空，调用方 sleep 一帧即可
// ============================================================
#pragma once
#include <stdint.h>
#include "pid_defs.h"
#include "../app/vehicle_profile.h"

namespace obd {

// 调度器状态：每个 PID 的到期时间与健康度
struct SchedEntry {
    uint32_t nextDueMs;    // 下次允许请求的时间
    uint32_t okCount;
    uint32_t failCount;
    bool     enabled;      // 运行时开关（可被探针关闭不支持的 PID）
    int8_t   reported;     // -1 unknown, 0 not advertised, 1 advertised
};

constexpr uint8_t kMaxBatch = 6;   // 单请求最多带几个 PID（ELM327 建议不超过 6）

class PidScheduler {
public:
    void begin() {
        for (size_t i = 0; i < kPidCount; ++i) {
            e_[i].nextDueMs = millis();
            e_[i].okCount   = 0;
            e_[i].failCount = 0;
            e_[i].enabled   = profilePidEnabled(kPidTable[i].pid);
            e_[i].reported  = -1;
            lastReqMs_[i] = lastOkMs_[i] = 0;
        }
    }

    // 第一优先级：这几个 PID 永远优先被调度（转速/车速/水温/电压）
    enum : uint8_t { kHighPriorityCount = 4 };

    // 选出本轮要请求的 PID 下标集合。
    // 返回个数；0 表示都还没到期（调用方应让出 CPU）。
    uint8_t selectDue(uint32_t now, uint8_t* outIdx, uint8_t cap) {
        uint8_t n = 0;
        uint8_t due[kPidCount];

        // 只"收集"到期项，绝不在这里改 nextDueMs。
        // ！2026-10-03 测试台发现的严重 bug：原实现在选中的瞬间就把
        // nextDueMs 推后，而 ElmClient 在非批量路径下只用 idx[0] 
        // 其余被选中的 PID 白白推后，于是除第一个以外全部被永久饿死。
        // 实测现象：只有 RPM 在刷新，车速/水温/电压恒为 0，并因此
        // 触发恒久的假低压报警。时间戳改由 markRequested() 提交。
        for (size_t i = 0; i < kPidCount; ++i) {
            if (!e_[i].enabled) continue;
            if (int32_t(now - e_[i].nextDueMs) < 0) continue;
            due[n++] = (uint8_t)i;
        }

        // 最逾期在前；同一到期时间才按优先级排序，避免慢链路饿死低优先级项。
        // 后者是关键：刚读过的 PID 逾期程度变小、自动排到后面，
        // 其余 PID 才有机会被 idx[0] 选中，轮转自然公平。
        for (uint8_t a = 1; a < n; ++a) {
            uint8_t v = due[a], b = a;
            while (b > 0 && orderBefore(v, due[b - 1])) {
                due[b] = due[b - 1];
                --b;
            }
            due[b] = v;
        }
        if (n>cap) n=cap;
        for (uint8_t i=0;i<n;++i) outIdx[i]=due[i];
        return n;
    }

    // 提交本轮真正发出去的请求：只有这些 PID 的到期时间才推后。
    // （推后动作必须在这里，而不是 selectDue 里  见上面的注释）
    void markRequested(const uint8_t* idx, uint8_t n, uint32_t now) {
        for (uint8_t k = 0; k < n; ++k) {
            if (idx[k] >= kPidCount) continue;
            lastReqMs_[idx[k]]   = now;
            e_[idx[k]].nextDueMs = now + profileInterval(kPidTable[idx[k]].pid,kPidTable[idx[k]].minIntervalMs);
        }
    }

    void reportOk(uint8_t idx) {
        if (idx >= kPidCount) return;
        e_[idx].okCount++;
        e_[idx].failCount = 0;
        lastOkMs_[idx] = millis();
    }

    // Failed PIDs cool down, then are probed again without reconnecting.
    void reportFail(uint8_t idx) {
        if (idx >= kPidCount) return;
        if (e_[idx].failCount < 1000) ++e_[idx].failCount;
        if (e_[idx].failCount >= kFailBeforeDisable)
            e_[idx].nextDueMs = millis() + (e_[idx].reported==0?30000:10000);
    }

    // 某 PID 自上次成功以来的间隔（判断数据是否过期）
    bool isStale(uint8_t idx, uint32_t now, uint32_t maxAgeMs) const {
        if (idx >= kPidCount) return true;
        if (lastOkMs_[idx] == 0) return true;
        return (now - lastOkMs_[idx]) > maxAgeMs;
    }

    uint32_t lastRequestMs(uint8_t idx) const {
        return (idx < kPidCount) ? lastReqMs_[idx] : 0;
    }

    const SchedEntry& entry(size_t i) const { return e_[i]; }

    void setEnabled(uint8_t idx, bool on) {
        if (idx < kPidCount) e_[idx].enabled = on;
    }
    bool isEnabled(uint8_t idx) const { return idx < kPidCount && e_[idx].enabled; }
    void setReported(uint8_t idx,int8_t value){if(idx<kPidCount)e_[idx].reported=value;}

    // 统计：当前实际轮询一轮的名义耗时（用于给 UI 显示"预计刷新率"）
    uint32_t nominalCycleMs() const {
        uint32_t total = 0;
        for (size_t i = 0; i < kPidCount; ++i) {
            if (e_[i].enabled) total += kPidTable[i].minIntervalMs;
        }
        return total;
    }

private:
    static constexpr uint32_t kFailBeforeDisable = 5;

    // 排序键：高优先级在前；同组内最逾期在前
    bool orderBefore(uint8_t a, uint8_t b) const {
        if (e_[a].nextDueMs != e_[b].nextDueMs)
            return int32_t(e_[a].nextDueMs-e_[b].nextDueMs) < 0;
        bool ha = isHighPriority(a), hb = isHighPriority(b);
        if (ha != hb) return ha;
        return false;
    }

    // 高优先级 = 表盘核心 4 项（按 pid 值判断，避免依赖表内顺序）
    static bool isHighPriority(size_t i) {
        switch (kPidTable[i].pid) {
            case 0x0C:   // RPM
            case 0x0D:   // SPEED
            case 0x05:   // COOLANT
            case 0x42:   // BATTERY
                return true;
            default:
                return false;
        }
    }

    SchedEntry e_[kPidCount];
    uint32_t   lastReqMs_[kPidCount] = {0};
    uint32_t   lastOkMs_[kPidCount]  = {0};
};

} // namespace obd
