// ============================================================
//  ui_oled.h  仪表界面渲染（12864 单色）
//
//  布局：
//     y= 0   RPM                                    [报警词(反白)]
//     y= 7   转速大字(3, 1521)          左侧：转速条 + 红线刻度
//     y=30    分隔线 
//     y=32   COOL 93C                     SPD 88
//     y=40   VOLT 14.1                    THR 22
//     y=48   LINK 100%                    OK 1234
//     y=56   [底部反白条：报警时显示报警词，否则显示链路状态]
//
//  两条设计约束：
//   1) 每帧只把显存重画一遍，真正下发交给 flushDirty() 按"脏页"做 
//      整帧 1KB 在 400kHz 下约 23ms，而每帧通常只有转速那 1~2 页在变。
//   2) 报警时底部整条反白 + 报警词  单色屏上"反白"是最强的视觉告警，
//      比闪烁更省心（闪烁在 20Hz 刷新下容易看起来只是抖动）。
// ============================================================
#pragma once
#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include "oled_ssd1306.h"
#include "../app/vehicle_state.h"
#include "../app/alarms.h"
#include "../obd/pid_defs.h"

namespace obd {

class UiOled {
public:
    void attach(Ssd1306* d) { d_ = d; }
    bool active() const { return d_ && d_->present(); }

    // 报警词：带 '!' = 严重级
    static const char* alarmWord(uint8_t f) {
        if (f & (1u << (ALARM_COOLANT  + 4))) return "HOT!";
        if (f & (1u << (ALARM_RPM      + 4))) return "OVER!";
        if (f & (1u << (ALARM_SPEED    + 4))) return "FAST!";
        if (f & (1u << (ALARM_VOLT_LOW + 4))) return "LOWV!";
        if (f & (1u << ALARM_COOLANT))        return "HOT";
        if (f & (1u << ALARM_RPM))            return "OVER";
        if (f & (1u << ALARM_SPEED))          return "FAST";
        if (f & (1u << ALARM_VOLT_LOW))       return "LOWV";
        return nullptr;
    }

    // ------------------------------------------------------------
    //  转速闪屏（换挡提示 / 断油告警）
    //    阈值直接取 RPM 报警的 warn/crit，不另写一份，保证单一来源
    //    >= crit：快闪约 7Hz（已进断油区，必须立刻反应）
    //    >= warn：慢闪约 2Hz（该换挡了，还有余量）
    //  用整屏反白实现（SSD1306 硬件命令），所以可以跑得快而不卡总线。
    //  注意：由 loop() 每轮调用（约 50Hz），不要塞进 render()  render 只有 20Hz，
    //        50ms 的粒度做不出干净的 7Hz 方波。
    // ------------------------------------------------------------
    static float flashWarnRpm() { return kDefaultAlarms[ALARM_RPM].warn; }
    static float flashFastRpm() { return kDefaultAlarms[ALARM_RPM].crit; }

    void tickFlash(float rpm, uint32_t now) {
        if (!active()) return;
        bool on = false;
        if (rpm >= flashFastRpm())      on = ((now / 70)  & 1u) == 0;   // ~7Hz
        else if (rpm >= flashWarnRpm()) on = ((now / 250) & 1u) == 0;   // ~2Hz
        d_->setInvert(on);              // 未达阈值时自动回到 0xA6
    }

    void render(const VehicleSnapshot& s, float rpmShown, float speedShown, uint32_t now) {
        if (!active()) return;
        Ssd1306& d = *d_;
        char b[24];

        d.clear();                       // 只清显存，不碰总线

        // ---------- 转速大字 ----------
        d.drawText(0, 0, "RPM", 1);
        snprintf(b, sizeof(b), "%d", (int)(rpmShown + 0.5f));
        d.drawTextRight(127, 7, b, 3);

        // ---------- 转速条（0~7000）+ 6200 红线刻度 ----------
        const int16_t barX = 0, barY = 20, barW = 52, barH = 8;
        d.rect(barX, barY, barW, barH);
        int16_t inner = barW - 2;
        int16_t fill = (int16_t)(rpmShown / 7000.0f * inner + 0.5f);
        if (fill < 0) fill = 0;
        if (fill > inner) fill = inner;
        if (fill > 0) d.fillRect(barX + 1, barY + 1, fill, barH - 2, true);
        int16_t rl = (int16_t)(6200.0f / 7000.0f * inner) + barX + 1;
        d.vLine(rl, barY - 3, barH + 6, true);       // 红线刻度（断油约 6600-6800）

        d.hLine(0, 30, 128, true);

        // ---------- 三行小字 ----------
        snprintf(b, sizeof(b), "COOL %dC", (int)(s.coolantC + 0.5f));
        d.drawText(0, 32, b, 1);
        snprintf(b, sizeof(b), "SPD %d", (int)(speedShown + 0.5f));
        d.drawTextRight(127, 32, b, 1);

        snprintf(b, sizeof(b), "VOLT %.1f", s.volts);
        d.drawText(0, 40, b, 1);
        snprintf(b, sizeof(b), "THR %d", (int)(s.throttle + 0.5f));
        d.drawTextRight(127, 40, b, 1);

        snprintf(b, sizeof(b), "LINK %d%%", s.linkQuality);
        d.drawText(0, 48, b, 1);
        snprintf(b, sizeof(b), "OK %u", (unsigned)s.totalOk);
        d.drawTextRight(127, 48, b, 1);

        // ---------- 底部状态条 ----------
        const char* aw = alarmWord(s.alarmFlags);
        if (aw) {
            d.fillRect(0, 56, 128, 8, true);                    // 反白条
            d.drawText(2, 57, aw, 1, true);
            d.drawTextRight(126, 57, s.linkUp ? "LINK OK" : "NO LINK", 1, true);
        } else {
            d.drawText(2, 57, s.linkUp ? "READY" : "WAIT LINK", 1, false);
            snprintf(b, sizeof(b), "T %u", (unsigned)(now / 1000));
            d.drawTextRight(126, 57, b, 1, false);
        }
        // 注意：这里只重画显存；真正下发由 flushDirty() 按脏页完成
    }

private:
    Ssd1306* d_ = nullptr;
};

} // namespace obd