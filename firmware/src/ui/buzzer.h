// ============================================================
//  buzzer.h  报警蜂鸣器
//
//  设计取巧之处：它不自己判断"该不该响"，而是**直接吃报警位图**
//  （alarmFlags，bit0-3 = 四路报警激活，bit4-7 = 对应"严重"级）。
//  报警状态机已经过完整验证（去抖/保持/滞回），所以蜂鸣器天然继承
//  那套语义：单点噪声不会响，持续越限才响，回落自动停。
//
//  分档响法：
//    有严重级（bits4-7） 急促连响（约 5Hz）
//    只有预警级（bits0-3） 每 1.5 秒一声短鸣
//    无报警  静音
//
//   接线前必须确认型号，驱动方式不同：
//    有源（自带振荡） GPIO 拉到高电平就响，改 kPassive = false
//    无源（需方波驱动） 必须用 LEDC 给方波，改 kPassive = true
//  判别方法：接 3.3V 直流，响 = 有源；只"嗒"一声或不响 = 无源。
//
//  引脚：GPIO25（DAC1，无 strapping 功能，上电安全；不与 OLED 的
//        21/22 冲突，也不是外挂 Flash 的 6-11）
// ============================================================
#pragma once
#include <Arduino.h>
#include "../app/alarms.h"

#ifndef OBD_BUZZER_PIN
#define OBD_BUZZER_PIN 25
#endif
namespace obd {

// ------------------------------------------------------------
//  节奏表：偶数下标 = 响的时长，奇数下标 = 停的时长（毫秒），循环播放
//
//  为什么放在命名空间作用域而不是类内 static constexpr：
//  类内 static constexpr 数组一旦被"取地址"（下面 Pattern 里存的是指针）
//  就构成 ODR-use，在 C++11 下必须在类外再定义一遍，否则链接期报
//  undefined reference to obd::Buzzer::kPatXxx  这正是第一次编译踩的坑。
//  放在命名空间作用域，constexpr 自带内部链接，头文件里也安全。
// ------------------------------------------------------------
// 严重级的"急促度"做成 4 档，可运行时切换（串口/网页命令 F），方便按耳朵调。
// 注意：档位越高越接近"连续音" 有源蜂鸣器的振膜有惯性，20Hz 时多数会
// 糊成一片刺耳的长音，那反而是最催命的效果。
constexpr uint16_t kCritVariants[4][2] = {
    {  60,  60 },   // 0: 约 8.3Hz（最初的样子）
    {  50,  30 },   // 1: 约 12.5Hz    默认
    {  40,  20 },   // 2: 约 16.7Hz
    {  30,  20 },   // 3: 约 20Hz（多半糊成连续音）
};
constexpr uint16_t kPatStartup[] = {60,80,60,80,180,100}; // one-shot: two short, one longer
constexpr uint16_t kPatTemp [] = { 600, 900 };                    // 超温：长音
constexpr uint16_t kPatVolt [] = { 250, 1500 };                   // 低压：单短音
constexpr uint16_t kPatSpeed[] = { 90, 90, 90, 90, 90, 1200 };    // 超速：三短音
constexpr uint16_t kPatRpm  [] = { 70, 110, 70, 110 };            // 超转：双短音（换挡提示）

class Buzzer {
public:
    static constexpr uint8_t kPin = OBD_BUZZER_PIN;

    // 出厂假设"有源"（最常见）；不用改代码  串口命令 B 可随时切换：
    // 听得出哪种响，就用哪种，切换时会自动在新模式下鸣一声做确认。
    static constexpr bool     kDefaultPassive = false;
    static constexpr uint16_t kToneHz  = 2731;    // 无源蜂鸣器谐振频率附近最响
    static constexpr uint8_t  kLedcCh  = 7;       // LEDC 通道（避开别的用途）

    void begin() { applyMode(); }

    bool passive() const { return passive_; }

    // 运行时切换驱动方式，并立刻在新模式下鸣一声（靠耳朵确认）
    void setPassive(bool p) {
        passive_ = p;
        applyMode();
        output(true);  delay(120);
        output(false);
    }

    // Non-blocking; called by Core 1 only. Mode 0 stops previews, 1=startup,
    // 2=RPM double beep (two cycles), 3=redline rapid beeps (1.5 seconds).
    void preview(uint8_t mode,uint32_t now){
        previewMode_=mode<=3?mode:0;previewAt_=now;output(false);
    }
    void selfTest(){preview(1,millis());}
    bool sounding()const{return on_;}
    uint8_t previewMode()const{return previewMode_;}

    // ------------------------------------------------------------
    //  单一无源/有源蜂鸣器只能靠"节奏"区分故障类型  所以每种报警给
    //  一个能闭眼听出来的节奏，而不是统统"嘀嘀嘀"。
    //    双短音 = 该换挡了（提示性质，最不烦人）
    //    三短音 = 超速
    //    长  音 = 水温高（低沉、像警报）
    //    单短音 = 电压低（最稀疏）
    //    连续急促 = 任意严重级（进红线/断油区）
    //  优先级：严重级 > 超温 > 低压 > 超速 > 超转
    // ------------------------------------------------------------
    struct Pattern { const uint16_t* seq; uint8_t len; };

    Pattern pick(uint8_t f) const {
        if (f & 0xF0)                        return { critSeq_ , 2 };   // 任意严重级（档位可变，见下）
        // 说明：critSeq_ 是成员，pick 改成非静态（见下方的改动）
        if (f & (1u << ALARM_COOLANT))       return { kPatTemp , 2 };
        if (f & (1u << ALARM_VOLT_LOW))      return { kPatVolt , 2 };
        if (f & (1u << ALARM_SPEED))         return { kPatSpeed, 6 };
        return { kPatRpm, 4 };                                          // 只剩超转  换挡提示
    }

    // 急促度档位（0~3），默认 1 = 约 12.5Hz
    void setCritLevel(uint8_t lv) {
        crit_ = lv & 3u;
        critSeq_[0] = kCritVariants[crit_][0];
        critSeq_[1] = kCritVariants[crit_][1];
    }
    uint8_t critLevel() const { return crit_; }

    // 当前时刻在该节奏的哪一段：true=响，false=停
    static bool phaseOn(const Pattern& p, uint32_t now) {
        uint32_t total = 0;
        for (uint8_t i = 0; i < p.len; ++i) total += p.seq[i];
        if (total == 0) return false;
        uint32_t t = now % total;
        for (uint8_t i = 0; i < p.len; ++i) {
            if (t < p.seq[i]) return (i & 1u) == 0;   // 偶数段响、奇数段停
            t -= p.seq[i];
        }
        return false;
    }

    // 由 loop() 每轮调用（约 50Hz）
    void tick(uint8_t alarmFlags, bool linkUp, uint32_t now) {
        if(muted_){previewMode_=0;alarmKey_=0;output(false);return;}
        const uint8_t key=!linkUp?0:(alarmFlags&0xF0)?0xF0:
            (alarmFlags&(1u<<ALARM_COOLANT))?(1u<<ALARM_COOLANT):
            (alarmFlags&(1u<<ALARM_VOLT_LOW))?(1u<<ALARM_VOLT_LOW):
            (alarmFlags&(1u<<ALARM_SPEED))?(1u<<ALARM_SPEED):
            (alarmFlags&(1u<<ALARM_RPM))?(1u<<ALARM_RPM):0;
        if(key){
            if(key!=alarmKey_)alarmAt_=now; // First beep starts at alarm entry.
            alarmKey_=key;previewMode_=0;
            output(phaseOn(pick(key),uint32_t(now-alarmAt_)));return;
        }
        alarmKey_=0;
        if(previewMode_){
            const uint32_t elapsed=now-previewAt_;
            const uint32_t duration=previewMode_==1?560:previewMode_==2?2160:1500;
            if(elapsed>=duration){previewMode_=0;output(false);return;}
            const Pattern p=previewMode_==1?Pattern{kPatStartup,6}:previewMode_==2?Pattern{kPatRpm,4}:Pattern{critSeq_,2};
            output(phaseOn(p,elapsed));return;
        }
        output(false);
    }

    // 逐个试听全部节奏（串口命令 P） 阻塞式，仅供调试调音色
    void playAll() {
        const Pattern ps[5]   = { {kPatRpm,4}, {kPatSpeed,6}, {kPatTemp,2}, {kPatVolt,2}, {critSeq_,2} };
        const char*   names[5] = { "换挡提示 双短音", "超速 三短音", "超温 长音",
                                   "低压 单短音", "严重级 连续急促" };
        for (uint8_t k = 0; k < 5; ++k) {
            Serial.printf("   试听 %u/5：%s\r\n", (unsigned)(k + 1), names[k]);
            uint32_t total = 0;
            for (uint8_t i = 0; i < ps[k].len; ++i) total += ps[k].seq[i];
            total *= 2;                            // 每种放两遍
            for (uint32_t t = 0; t < total; t += 10) {
                output(phaseOn(ps[k], t));
                delay(10);
            }
            output(false);
            delay(500);
        }
        Serial.printf("   试听结束\r\n");
    }

    bool muted() const { return muted_; }
    void setMuted(bool m) { muted_ = m; if (m) output(false); }
    void toggleMute()     { setMuted(!muted_); }

private:
    void applyMode() {
        if (passive_) {
            ledcSetup(kLedcCh, kToneHz, 10);      // 方波：无源蜂鸣器靠它发声
            ledcAttachPin(kPin, kLedcCh);
            ledcWriteTone(kLedcCh, 0);
        } else {
            ledcDetachPin(kPin);                  // 交回普通 GPIO
            pinMode(kPin, OUTPUT);
            digitalWrite(kPin, LOW);
        }
        on_ = false;
    }

    void output(bool on) {
        if (on == on_) return;                // 状态缓存：不重复动硬件
        on_ = on;
        if (passive_) ledcWriteTone(kLedcCh, on ? kToneHz : 0);
        else          digitalWrite(kPin, on ? HIGH : LOW);
    }

    uint8_t previewMode_=0,alarmKey_=0;
    uint32_t previewAt_=0,alarmAt_=0;
    bool on_ = false;
    bool muted_ = false;
    bool passive_ = kDefaultPassive;
    uint8_t  crit_ = 1;                                  // 默认档位 1（约 12.5Hz）
    uint16_t critSeq_[2] = { kCritVariants[1][0], kCritVariants[1][1] };
};

} // namespace obd
