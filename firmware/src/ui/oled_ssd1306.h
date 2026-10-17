// ============================================================
//  oled_ssd1306.h  极简 SSD1306 12864 I2C 驱动（零第三方依赖）
//
//  为什么自己写，不上 U8g2 / Adafruit_SSD1306：
//   1) 本机访问 GitHub 受限，U8g2 全量包很大，Adafruit 要两个包
//   2) 这块屏只服务测试台，需要的东西很少：初始化 + 画数字/文字/条
//   3) 与本项目"手写 ISO-TP、手写分级刷屏"的取向一致，且零依赖最稳
//
//  接线：SDA=GPIO21   SCL=GPIO22   VCC=3V3   GND=GND
//  地址：0x3C 或 0x3D（7 位）
//
//  内存：片上一块 12864 显存 = 1KB，画完再整帧下发，
//        这样上层只要"改数字"即可，不用关心 SSD1306 的页/列寻址。
// ============================================================
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <string.h>
#include "font5x7.h"

namespace obd {

class Ssd1306 {
public:
    static constexpr uint8_t kWidth  = 128;
    static constexpr uint8_t kHeight = 64;
    static constexpr uint8_t kPages  = kHeight / 8;

    // ------------------------------------------------------------
    //  扫 I2C 总线。打印所有有应答的地址，返回找到的 OLED 地址
    //  （优先 0x3C/0x3D，否则返回第一个有应答的地址，便于排错）
    // ------------------------------------------------------------
    uint8_t scanBus() {
        ensureBus();
        uint8_t first = 0, oled = 0;
        Serial.printf("I2C 扫描中（SDA=GPIO21 SCL=GPIO22）...\r\n");
        for (uint8_t a = 0x03; a <= 0x77; ++a) {
            Wire.beginTransmission(a);
            if (Wire.endTransmission() == 0) {
                Serial.printf("  应答: 0x%02X\r\n", a);
                if (!first) first = a;
                if ((a == 0x3C || a == 0x3D) && !oled) oled = a;
            }
        }
        if (!first) {
            Serial.println("  没有任何设备应答  检查：VCC 是否接 3V3、GND 是否共地、");
            Serial.println("  SDA/SCL 是否接反、模块引脚是否按丝印而非按位置接。");
        }
        lastFound_ = oled ? oled : first;
        return lastFound_;
    }

    bool begin(uint8_t addr) {
        ensureBus();
        addr_ = addr;
        ok_ = probe();
        return ok_;
    }

    bool probe() {
        ensureBus();
        Wire.beginTransmission(addr_);
        return Wire.endTransmission() == 0;
    }

    uint8_t address() const { return addr_; }
    bool    present() const { return ok_; }

    // ------------------------------------------------------------
    //  整屏反白：SSD1306 自带硬件命令（0xA7 反白 / 0xA6 正常），
    //  2 字节就能切换整屏，不必重发 1KB 显存  所以拿它做"闪屏"
    //  告警既醒目又不占 I2C 总线。状态做了缓存，不重复发命令。
    // ------------------------------------------------------------
    void setInvert(bool on) {
        if (inverted_ == on) return;
        inverted_ = on;
        ++invertCount_;
        uint8_t c = on ? 0xA7 : 0xA6;
        cmd(&c, 1);
    }
    bool     inverted()    const { return inverted_; }
    uint32_t invertCount() const { return invertCount_; }

    // ------------------------------------------------------------
    //  初始化序列（SSD1306 数据手册的通用上电流程）
    // ------------------------------------------------------------
    void init() {
        static const uint8_t kInit[] = {
            0xAE,              // 关显示
            0x20, 0x00,        // 水平寻址模式（写完一页自动跳到下一页）
            0xB0,              // 起始页 0
            0xC8,              // COM 扫描方向：上下翻转（模块装配决定）
            0x00,              // 低列地址 0
            0x10,              // 高列地址 0
            0x40,              // 起始行 0
            0x81, 0x7F,        // 对比度
            0xA1,              // 段重映射：左右翻转（模块装配决定）
            0xA6,              // 正常显示（非反白）
            0xA8, 0x3F,        // 多路复用比 = 64 行
            0xA4,              // 显示跟随显存
            0xD3, 0x00,        // 显示偏移 0
            0xD5, 0x80,        // 时钟分频/振荡频率
            0xD9, 0xF1,        // 预充电周期
            0xDA, 0x12,        // COM 引脚配置（12864 用 0x12）
            0xDB, 0x40,        // VCOMH 反选电平
            0x8D, 0x14,        // 使能内部电荷泵
            0xAF               // 开显示
        };
        cmd(kInit, sizeof(kInit));
        inverted_ = false;          // init 序列末尾就是 0xA6（正常显示）
        clear();
        flush();
    }

    void clear() { memset(buf_, 0, sizeof(buf_)); }

    // ---- 绘图（只改显存，不碰总线）----
    void setPixel(int16_t x, int16_t y, bool on = true) {
        if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) return;
        uint8_t m = (uint8_t)(1u << (y & 7));
        if (on) buf_[y >> 3][x] |= m; else buf_[y >> 3][x] &= (uint8_t)~m;
    }

    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, bool on = true) {
        for (int16_t j = y; j < y + h; ++j)
            for (int16_t i = x; i < x + w; ++i) setPixel(i, j, on);
    }

    void hLine(int16_t x, int16_t y, int16_t w, bool on = true) { fillRect(x, y, w, 1, on); }
    void vLine(int16_t x, int16_t y, int16_t h, bool on = true) { fillRect(x, y, 1, h, on); }

    void rect(int16_t x, int16_t y, int16_t w, int16_t h, bool on = true) {
        hLine(x, y, w, on); hLine(x, y + h - 1, w, on);
        vLine(x, y, h, on); vLine(x + w - 1, y, h, on);
    }

    // ------------------------------------------------------------
    //  文字绘制（5x7 点阵，scale 倍整体放大）
    //  scale=1 用于标签与数值行，scale=3 用于转速大字
    // ------------------------------------------------------------
    static const uint8_t* glyph(char c) {
        for (size_t i = 0; i < kFont5x7Count; ++i)
            if (kFont5x7[i].ch == c) return kFont5x7[i].col;
        return nullptr;                 // 字库里没有的字：画成空白，不崩
    }
    int16_t textWidth(const char* s, uint8_t sc) const {
        int16_t n = (int16_t)strlen(s);
        return n > 0 ? (int16_t)(n * 6 * sc - sc) : 0;
    }
    void drawChar(int16_t x, int16_t y, char c, uint8_t sc = 1, bool inv = false) {
        const uint8_t* g = glyph(c);
        if (!g) return;
        for (uint8_t col = 0; col < 5; ++col)
            for (uint8_t row = 0; row < 7; ++row)
                if (g[col] & (1u << row)) {
                    if (sc == 1) setPixel(x + col, y + row, !inv);
                    else         fillRect(x + col * sc, y + row * sc, sc, sc, !inv);
                }
    }
    void drawText(int16_t x, int16_t y, const char* s, uint8_t sc = 1, bool inv = false) {
        for (; *s; ++s, x += 6 * sc) drawChar(x, y, *s, sc, inv);
    }
    void drawTextRight(int16_t xr, int16_t y, const char* s, uint8_t sc = 1, bool inv = false) {
        drawText((int16_t)(xr - textWidth(s, sc) + 1), y, s, sc, inv);
    }

    // ------------------------------------------------------------
    //  只下发"变化过的页"（脏页）。
    //  为什么要这样：整帧 1KB，I2C 400kHz 下约 23ms；而仪表每帧通常
    //  只有转速那 1~2 页在变（每页约 3ms）。整帧下发会白占总线并阻塞
    //  Core 1 的串口命令处理。
    // ------------------------------------------------------------
    void flushDirty() {
        for (uint8_t p = 0; p < kPages; ++p) {
            if (memcmp(buf_[p], prev_[p], kWidth) == 0) continue;
            uint8_t win[] = {0x21, 0x00, 0x7F, 0x22, p, p};
            cmd(win, sizeof(win));
            const uint8_t* q = buf_[p];
            for (size_t off = 0; off < kWidth; off += 32) {
                size_t n = (kWidth - off) > 32 ? 32 : (kWidth - off);
                Wire.beginTransmission(addr_);
                Wire.write(0x40);
                Wire.write(q + off, n);
                if (Wire.endTransmission() != 0) return;
            }
            memcpy(prev_[p], buf_[p], kWidth);
        }
    }

    // ------------------------------------------------------------
    //  整帧下发。为什么分块：ESP32 的 Wire 缓冲区默认 128 字节，
    //  一次塞 1024 字节会溢出并被截断。
    // ------------------------------------------------------------
    void flush() {
        // 显式设窗口：列 0-127，页 0-7，然后连续写 1024 字节
        static const uint8_t kWin[] = {0x21, 0x00, 0x7F, 0x22, 0x00, 0x07};
        cmd(kWin, sizeof(kWin));

        const uint8_t* p = &buf_[0][0];
        size_t left = sizeof(buf_);
        while (left) {
            size_t chunk = left > 32 ? 32 : left;      // 32 字节/次，留足控制字节余量
            Wire.beginTransmission(addr_);
            Wire.write(0x40);                          // 控制字节：后面是数据
            Wire.write(p, chunk);
            if (Wire.endTransmission() != 0) return;   // 失败就放弃本帧，别卡住
            p += chunk; left -= chunk;
        }
        memcpy(prev_, buf_, sizeof(buf_));
    }

    // ------------------------------------------------------------
    //  接线自检图案：外框 + 竖条纹 + 上半屏实心块
    //  一眼可判：外框缺边=SDA/SCL 有问题；条纹不对=寻址错位；
    //            块不实=对比度/电荷泵问题。
    // ------------------------------------------------------------
    void testPattern() {
        clear();
        rect(0, 0, kWidth, kHeight);
        for (uint8_t x = 4; x < kWidth - 4; x += 8)
            vLine(x, 4, kHeight - 8);
        fillRect(4, 4, kWidth - 8, 20);
        flush();
    }

private:
    void ensureBus() {
        if (busReady_) return;
        Wire.begin(21, 22);
        Wire.setClock(400000);        // 400kHz：1KB 整帧约 25ms，够 20~30Hz 刷屏
        busReady_ = true;
    }

    void cmd(const uint8_t* c, size_t n) {
        Wire.beginTransmission(addr_);
        Wire.write(0x00);             // 控制字节：后面是指令
        Wire.write(c, n);
        Wire.endTransmission();
    }

    uint8_t buf_[kPages][kWidth];      // 当前帧显存
    uint8_t prev_[kPages][kWidth];     // 已下发的显存（脏页比较用）
    uint8_t addr_ = 0x3C;
    bool    ok_ = false;
    bool     inverted_ = false;
    uint32_t invertCount_ = 0;
    uint8_t lastFound_ = 0;
    bool    busReady_ = false;
};

} // namespace obd