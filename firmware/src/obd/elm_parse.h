// ============================================================
//  elm_parse.h — ELM327 文本回包解析
//
//  背景（为什么这个文件值得单独存在）：
//  ELM327 返回的是 ASCII 十六进制文本，不是二进制。
//  单 PID 查询形如:   "41 0C 1A F8"  或  "410C1AF8"  或  "410C1AF8\r\r>"
//  批量查询形如:      "410C1AF8410D3C41057B"   ← 多个 PID 拼在一行
//
//  解析难点：
//   1) 每个 PID 的**数据长度不同**（0x0C 是 2 字节、0x0D 是 1 字节），
//      所以推进游标必须查 PID 定义表的 bytes 字段，不能假设固定步长。
//   2) ATS0 关掉空格后是连续 hex；如果不关空格就要容忍空格。
//   3) 必须处理非数据响应：NO DATA / ? / SEARCHING... / UNABLE TO CONNECT /
//      STOPPED / CAN ERROR / BUFFER FULL —— 这些不是错误代码而是文本，
//      把它们的字符当成 hex 解析会得到垃圾数值。
// ============================================================
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pid_defs.h"
#include "pid_convert.h"

namespace obd {

// 把不可见字符渲染成可见转义（把真回车显示成 \r 两个字符）。
// 为什么需要：ELM 回包/请求里都带真 CR，直接 %s 打到终端上会把光标拉回
// 行首覆盖掉已有内容，日志就没法看了。
// 放在这里而不是 selftest.h：elm_client.h 也要用它做诊断，而 selftest.h
// 是最后才被包含的，放上面会导致"未声明"（2026-10-04 踩过）。
inline const char* vis(const char* s, char* out, size_t cap) {
    size_t j = 0;
    for (size_t i = 0; s[i] && j + 5 < cap; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\r')      { out[j++] = '\\'; out[j++] = 'r'; }
        else if (c == '\n') { out[j++] = '\\'; out[j++] = 'n'; }
        else if (c < 0x20)   { j += (size_t)snprintf(out + j, cap - j, "\\x%02X", c); }
        else                 { out[j++] = (char)c; }
    }
    out[j] = '\0';
    return out;
}


// ELM327 回包分类
enum class ReplyKind : uint8_t {
    Data,            // 含有效数据
    NoData,          // NO DATA —— 该 PID 不支持或 ECU 未响应
    Unsupported,     // ? —— 命令语法错 / 适配器不支持
    Searching,       // SEARCHING... —— 还在自动匹配协议，不是数据
    UnableToConnect, // UNABLE TO CONNECT —— 未点火或协议不匹配
    Stopped,         // STOPPED —— 缓冲溢出或被打断
    CanError,        // CAN ERROR
    BufferFull,      // BUFFER FULL
    Empty,           // 空响应（超时前什么都没读到）
};

// 把一个 hex 字符转成数值，非法返回 -1
inline int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// 把文本（可能含空格/回车）压成干净的大写 hex 串，返回长度。
// out 必须至少 inLen+1 字节。
inline size_t stripToHex(const char* in, char* out, size_t outCap) {
    size_t n = 0;
    for (const char* p = in; *p && n + 1 < outCap; ++p) {
        if (hexNibble(*p) >= 0) {
            char c = *p;
            if (c >= 'a' && c <= 'f') c -= 32;   // 统一大写，便于比较
            out[n++] = c;
        }
    }
    out[n] = '\0';
    return n;
}

// 对回包文本分类（在压 hex 之前做，因为要匹配原文关键词）
inline ReplyKind classify(const char* txt) {
    if (!txt) return ReplyKind::Empty;
    // 大小写不敏感的子串查找
    auto has = [&](const char* kw) -> bool {
        size_t kl = strlen(kw);
        for (const char* p = txt; *p; ++p) {
            size_t i = 0;
            while (i < kl && p[i] &&
                   ((p[i] >= 'a' && p[i] <= 'z') ? p[i] - 32 : p[i]) == kw[i]) i++;
            if (i == kl) return true;
        }
        return false;
    };

    if (has("NO DATA"))           return ReplyKind::NoData;
    if (has("UNABLE TO CONNECT")) return ReplyKind::UnableToConnect;
    if (has("SEARCHING"))         return ReplyKind::Searching;
    if (has("STOPPED"))           return ReplyKind::Stopped;
    if (has("CAN ERROR"))         return ReplyKind::CanError;
    if (has("BUFFER FULL"))       return ReplyKind::BufferFull;
    if (has("?"))                 return ReplyKind::Unsupported;
    if (strlen(txt) == 0)         return ReplyKind::Empty;
    return ReplyKind::Data;
}

// ------------------------------------------------------------
//  单 PID 回包解析
//  期望格式: <mode+0x40><pid><data[bytes]>
//  例如 0x0C: "41 0C 1A F8" -> mode 0x41, pid 0x0C, data = 1A F8
// ------------------------------------------------------------
struct ParsedPid {
    uint8_t pid;
    uint8_t data[4];
    uint8_t len;
    float   value;      // 已换算的物理量，NAN 表示不可用
};

// ELM CAF1 multiline replies carry a three-hex-digit byte count and
// sequence labels (0: .. F:), which are metadata, never payload bytes.
// Keep one bounded caller-owned buffer; reject missing/out-of-order frames.
inline size_t pidPayloadHex(const char* raw,char* out,size_t cap){
    if(!cap)return 0;out[0]=0;
    if(!strchr(raw,':')){
        if(classify(raw)==ReplyKind::Searching){raw=strstr(raw,"41");if(!raw)return 0;}
        return stripToHex(raw,out,cap);
    }
    size_t expected=0,used=0;unsigned sequence=0;bool numbered=false;
    for(const char* line=raw;*line;){
        const char* end=line;while(*end&&*end!='\r'&&*end!='\n')++end;
        const char* next=end;while(*next=='\r'||*next=='\n')++next;
        while(line<end&&(*line==' '||*line=='\t'))++line;
        while(end>line&&(end[-1]==' '||end[-1]=='\t'||end[-1]=='>'))--end;
        if(line==end){line=next;continue;}
        if(end-line>=2&&hexNibble(line[0])>=0&&line[1]==':'){
            if(!expected||unsigned(hexNibble(line[0]))!=(sequence&15))return 0;
            if(numbered&&used==expected*2)return 0;
            ++sequence;numbered=true;size_t digits=0;
            for(const char* p=line+2;p<end;++p){
                if(*p==' '||*p=='\t')continue;
                const int h=hexNibble(*p);if(h<0)return 0;
                ++digits;if(used<expected*2){if(used+1>=cap)return 0;out[used++]="0123456789ABCDEF"[h];}
            }
            if(!digits||(digits&1))return 0;
        }else if(!numbered&&end-line==3&&hexNibble(line[0])>=0&&hexNibble(line[1])>=0&&hexNibble(line[2])>=0){
            if(expected)return 0;
            expected=(hexNibble(line[0])<<8)|(hexNibble(line[1])<<4)|hexNibble(line[2]);
            if(!expected||expected*2>=cap)return 0;
        }else if(numbered)return 0;
        line=next;
    }
    if(!numbered||used!=expected*2)return 0;
    out[used]=0;return used;
}

// 返回解析成功的 PID 数量（批量回包可能含多个）
inline uint8_t parsePidReply(const char* raw, const PidDef* table, size_t tableN,
                             ParsedPid* out, uint8_t outCap) {
    if (!raw || (classify(raw)!=ReplyKind::Data&&classify(raw)!=ReplyKind::Searching)) return 0;

    // ！不能是 static：这个函数会被两个核同时调用  Core 0 的采集任务
    // 每秒解析约 28 次，Core 1 的向量测试也在调它。共用一块 static
    // 缓冲会让两边互相覆盖（2026-10-03 实测：500 组向量里偶发 1 组
    // 解析出根本不存在的 PID）。改成栈上局部变量，512 字节可接受。
    char hex[512];
    size_t n = pidPayloadHex(raw, hex, sizeof(hex));
    if (n < 4) return 0;

    auto pair = [&](size_t o) -> uint8_t {
        return (uint8_t)((hexNibble(hex[o]) << 4) | hexNibble(hex[o + 1]));
    };
    auto findPid = [&](uint8_t pid) -> const PidDef* {
        for (size_t k = 0; k < tableN; ++k) if (table[k].pid == pid) return &table[k];
        return nullptr;
    };

    uint8_t found = 0;
    size_t i = 0;
    bool haveMode = false;      // 已经见过模式字节 -> 之后允许它被省略

    // 两种回包格式都要吃：
    //    标准：每个 PID 前都带模式字节   "41 0C 1AF8 41 0D 3C"
    //    变体：模式字节只出现一次       "41 0C 1AF8 0D 3C"
    // ！2026-10-04 在 4B10 真车实测：这台车用   多 PID 请求
    // ("01 0C 0D 11") 回 "410C0A410D00"，老解析器把第二个 PID 直接吃掉了
    // （3 个只解出 1 个）。批量请求能不能用，卡点就在这里。
    while (i + 2 <= n && found < outCap) {
        const PidDef* def = nullptr;
        size_t dataOff = 0;

        if (i + 4 <= n) {
            uint8_t b0 = pair(i);
            if (b0 == 0x41 || b0 == 0x62) {          //  带模式字节
                def = findPid(pair(i + 2));
                if (!def) { i += 4; continue; }      // 表里没有的 PID，跳过其标识
                dataOff = i + 4;
            }
        }
        if (!def && haveMode) {                       //  省略模式字节：当前位置就是 PID
            def = findPid(pair(i));
            if (!def) { i += 2; continue; }
            dataOff = i + 2;
        }
        if (!def) { i += 2; continue; }

        size_t need = (size_t)def->bytes * 2;         // 数据部分的 hex 字符数
        if (dataOff + need > n) break;                // 数据不完整，停止

        ParsedPid pp{};
        pp.pid = def->pid;
        pp.len = def->bytes;
        for (uint8_t k = 0; k < def->bytes; ++k) {
            pp.data[k] = pair(dataOff + (size_t)k * 2);
        }
        pp.value = convertValue(def->conv, pp.data, pp.len);
        if (!plausible(*def, pp.value)) pp.value = NAN;

        out[found++] = pp;
        haveMode = true;
        i = dataOff + need;                           //  靠每个 PID 的 bytes 推进
    }
    return found;
}

// ------------------------------------------------------------
//  构造批量请求: "01 0C 0D 05\r"
//  一次问多个 PID 可以摊薄固定的 25-60ms 往返开销，
//  这是本项目相对单 PID 轮询最主要的提速手段。
//
//  ⚠️ 兼容性风险：并非所有 ECU 都支持单请求多 PID
//  （Torque 论坛有讨论指出这属于厂商实现差异）。
//  因此调用方必须：先用单 PID 验证链路，再试批量，
//  若返回 NO DATA 或垃圾数据则自动回退单 PID。
// ------------------------------------------------------------
inline size_t buildBatchRequest(const PidDef* table, size_t tableN,
                               const uint8_t* wantIdx, uint8_t wantN,
                               char* out, size_t outCap) {
    if (outCap < 8) return 0;
    size_t n = 0;
    // 模式统一取第一个 PID 的 mode（本项目暂不混模式批量）
    uint8_t mode = table[wantIdx[0]].mode == Mode::Mode22 ? 0x22 : 0x01;
    n += (size_t)snprintf(out + n, outCap - n, "%02X", mode);

    for (uint8_t k = 0; k < wantN; ++k) {
        const PidDef& d = table[wantIdx[k]];
        if (n + 4 >= outCap) break;
        n += (size_t)snprintf(out + n, outCap - n, " %02X", d.pid);
    }
    if (n + 2 < outCap) { out[n++] = '\r'; out[n] = '\0'; }
    return n;
}

} // namespace obd
