// ============================================================
//  serial_elm.h  用 USB 串口当链路，PC 上的 Python 假扮 ELM327
//
//  与 fake_elm.h 的区别：假适配器的"大脑"在 PC 上，因此能注入
//  固件里写不出来的东西  精确分片到达、超时、半包卡死、乱序、
//  NO DATA 与数据交替等。
//
//  注意：ESP32 自身日志与 ELM 数据共用同一根 USB 串口，PC 端按
//  行首前缀分流（AT / 01 / 22 开头是请求，其余是日志）。
// ============================================================
#pragma once
#include <Arduino.h>
#include "../obd/elm_client.h"

namespace obd {

class SerialElmIo : public ElmClient::Io {
public:
    bool begin(const char* = nullptr) { return true; }
    void tick(uint32_t) {}
    bool isConnected() { return true; }
    bool connected() override { return true; }          // ElmClient::Io 要求的名字
    int  available()   { return Serial.available(); }
    int  read()        { return Serial.read(); }
    size_t write(const char* buf, size_t n) override {
        return Serial.write((const uint8_t*)buf, n);
    }
    bool   supported() const { return true; }
    String describeState() const { return String("SERIAL/PC-ELM"); }
};

} // namespace obd