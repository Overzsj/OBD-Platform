// ============================================================
//  bt_spp.h — 经典蓝牙 SPP 客户端（连 ELM327）
//
//  API 依据：已实际读取框架源码
//    framework-arduinoespressif32/libraries/BluetoothSerial/src/BluetoothSerial.h
//  关键签名（不要凭记忆写）：
//    bool begin(String localName = String(), bool isMaster = false);
//    bool connect(const BTAddress &remoteAddress, int channel = 0,
//                 esp_spp_sec_t sec_mask = ESP_SPP_SEC_ENCRYPT|ESP_SPP_SEC_AUTHENTICATE,
//                 esp_spp_role_t role = ESP_SPP_ROLE_MASTER);
//    bool connect(String remoteName);
//    bool connected(int timeout = 0);
//    BTScanResults* discover(int timeout);
//    std::map<int,std::string> getChannels(const BTAddress&);
//
//  本框架的 sdkconfig 默认 CONFIG_BT_ENABLED=y / CONFIG_BT_CLASSIC_ENABLED=y
//  / CONFIG_BT_SPP_ENABLED=y / CONFIG_BLUEDROID_ENABLED=y，可直接使用。
//
//  失败模式（有据可查，必须处理）：
//    * BluetoothSerial::connect() 失败后会残留 _isRemoteAddressSet 状态，
//      导致后续 connect() 一直失败（arduino-esp32 已修，但旧核仍可能中招）
//    * ELMduino README 建议：连不上时改用 38400 波特率；用 MAC 而不是设备名；
//      先清除已配对设备
//    * 山寨适配器广播名五花八门，不能硬编码单一名字
// ============================================================
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "adapter_catalog.h"
#include "memory_budget.h"

#if defined(CONFIG_BT_ENABLED) && defined(CONFIG_BLUEDROID_ENABLED)
  #define OBD_HAS_BT 1
  #include <BluetoothSerial.h>
  #include <esp_heap_caps.h>
#else
  #define OBD_HAS_BT 0
#endif

namespace obd {

// ELM327 适配器常见的广播名关键字。
// 参考项目 luca86r 把名字硬编码为 "vLinker MC-Android"，
// 换个适配器就搜不到；这里改为关键字匹配。
struct NamePattern {
    const char* keyword;
    const char* note;
};

constexpr NamePattern kAdapterPatterns[] = {
    {"ELM327",  "通用/山寨适配器最常见"},
    {"OBDII",   "极常见（ELMduino README 提到的名字）"},
    {"OBD2",    "变体"},
    {"VLINKER", "Vgate vLinker 系列"},
    {"VGATE",   "Vgate 系列"},
    {"ICAR",    "Vgate iCar 系列"},
    {"OBDLINK", "ScanTool OBDLink 系列"},
    {"KONNWEI", "Konnwei 系列"},
    {"VEEPEAK", "Veepeak 系列"},
};
constexpr size_t kAdapterPatternCount = sizeof(kAdapterPatterns) / sizeof(kAdapterPatterns[0]);

class BtSpp {
public:
    void target(const char* mac,const char* pin){explicitMac_=mac;pin_=pin;}
    enum class St : uint8_t {
        Unsupported = 0,   // 该芯片/配置不支持经典蓝牙（如 ESP32-S3）
        Idle,
        Scanning,
        Connecting,
        Connected,
        Lost,              // 曾连上后掉线
        Failed,
        LowMemory,
    };

    bool begin(const char* localName = "OBD-Gauge") {
#if OBD_HAS_BT
        localName_ = localName;
        prefs_.begin("obdbt", false);
        // 载入上次成功的 MAC
        String saved = explicitMac_.length()?explicitMac_:prefs_.getString("mac", "");
        if (saved.length() >= 17) {
            savedMac_ = saved;
            haveSavedMac_ = true;
        }
        if (!serialBt_.begin(localName, true)) {  // true = master
            st_ = St::Failed;
            return false;
        }
        if(pin_.length())serialBt_.setPin(pin_.c_str());
        st_ = St::Idle;
        lastActionMs_ = millis();
        return true;
#else
        (void)localName;
        st_ = St::Unsupported;
        return false;
#endif
    }

    St state() const { return st_; }
    // 注意：不能标 const —— BluetoothSerial::connected(int) 是非 const 成员函数
    bool isConnected() {
#if OBD_HAS_BT
        return st_ == St::Connected && serialBt_.connected();
#else
        return false;
#endif
    }
    bool supported() const { return OBD_HAS_BT; }
    const String& savedMac() const { return savedMac_; }
    const String& foundName() const { return foundName_; }

    // ------------------------------------------------------------
    //  状态机：需被周期性调用
    // ------------------------------------------------------------
    void tick(uint32_t now) {
#if OBD_HAS_BT
        switch (st_) {
            case St::Unsupported: return;

            case St::Connected:
                if (!serialBt_.connected()) {
                    st_ = St::Lost;
                    lostAtMs_ = now;
                    retryCount_ = 0;
                }
                return;

            case St::Lost:
                // 掉线后先等 2 秒（发动机启动瞬间电压跌落常导致掉线）
                if (now - lostAtMs_ < 2000) return;
                st_ = St::Idle;
                return;

            case St::LowMemory:
            case St::Failed:
                // 退避重试，间隔随失败次数增长，上限 30 秒
                if (now - lastActionMs_ < backoffMs()) return;
                st_ = St::Idle;
                return;

            case St::Idle:
                if (now - lastActionMs_ < 1000) return;
                lastActionMs_ = now;
                if(!connectionMemoryReady(now))return;
                // 优先用已保存的 MAC（ELMduino README 明确建议用 MAC 而非设备名）
                if (haveSavedMac_) {
                    tryConnectMac(savedMac_);
                    if (st_ == St::Connected) return;
                }
                startScan(now);
                return;

            case St::Scanning:
                if (now - scanStartMs_ < kScanMs) return;
                finishScanAndConnect(now);
                return;

            case St::Connecting:
                if (now - scanStartMs_ < 4000) return;   // 给 connect 一点时间
                st_ = St::Failed;
                retryCount_++;
                lastActionMs_ = now;
                return;
        }
#endif
    }

    // ------------------------------------------------------------
    //  Stream 透传（供 ELM327 客户端读写）
    // ------------------------------------------------------------
    int available() {
#if OBD_HAS_BT
        return isConnected() ? serialBt_.available() : 0;
#else
        return 0;
#endif
    }
    int read() {
#if OBD_HAS_BT
        return isConnected() ? serialBt_.read() : -1;
#else
        return -1;
#endif
    }
    size_t write(const char* buf, size_t n) {
#if OBD_HAS_BT
        if (!isConnected()) return 0;
        return serialBt_.write((const uint8_t*)buf, n);
#else
        (void)buf; (void)n;
        return 0;
#endif
    }

    void requestReconnect(uint32_t now) {
#if OBD_HAS_BT
        serialBt_.disconnect();
        st_=St::Lost;lostAtMs_=now;retryCount_=0;
#endif
    }

    void forgetMac() {
#if OBD_HAS_BT
        prefs_.remove("mac");
        savedMac_ = "";
        haveSavedMac_ = false;
#endif
    }

    String describeState() const {
        switch (st_) {
            case St::Unsupported: return "UNSUPPORTED(no classic BT)";
            case St::Idle:        return "IDLE";
            case St::Scanning:    return "SCANNING";
            case St::Connecting:  return "CONNECTING";
            case St::Connected:   return "CONNECTED";
            case St::Lost:        return "LOST";
            case St::Failed:      return "FAILED";
            case St::LowMemory:   return "NO_MEMORY";
        }
        return "?";
    }

    uint8_t retryCount() const { return retryCount_; }

private:
    static constexpr uint32_t kScanMs = 8000;

    uint32_t backoffMs() const {
        uint32_t b = 2000u << (retryCount_ > 4 ? 4 : retryCount_);
        return b > 30000u ? 30000u : b;
    }

#if OBD_HAS_BT
    bool connectionMemoryReady(uint32_t now) {
        const size_t free=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
        const size_t largest=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
        // Conservative preflight, not a guarantee against all stack allocations.
        if(btConnectionMemoryReady(free,largest))return true;
        Serial.printf("BT_MEMORY|defer_connect|free=%u|largest=%u\n",unsigned(free),unsigned(largest));
        st_=St::LowMemory;lastActionMs_=now;if(retryCount_<5)++retryCount_;return false;
    }
    static bool looksLikeAdapter(const String& name) {
        String up = name;
        up.toUpperCase();
        for(auto bad:adapters::discouraged)if(up.indexOf(bad)>=0)return false;
        for(auto name:adapters::names)if(up.indexOf(name)>=0)return true;
        for (size_t i = 0; i < kAdapterPatternCount; ++i) {
            if (up.indexOf(kAdapterPatterns[i].keyword) >= 0) return true;
        }
        return false;
    }

    void tryConnectMac(const String& mac) {
        BTAddress addr(mac);
        if (serialBt_.connect(addr)) {
            st_ = St::Connected;
            retryCount_ = 0;
            return;
        }
        // 注意：失败后本框架内部可能残留 _isRemoteAddressSet，
        // 因此这里不反复重试同一个 MAC，改为回落到扫描
    }

    void startScan(uint32_t now) {
        scanStartMs_ = now;
        scanFound_ = false;
        scanSeen_  = 0;
        foundName_ = "";
        foundMac_  = "";
        // discoverAsync 回调里只挑名字像适配器的设备
        bool ok = serialBt_.discoverAsync([this](BTAdvertisedDevice* dev) {
            String nm(dev->getName().c_str());
            if(explicitMac_.length()) {
                String mac(dev->getAddress().toString().c_str());
                if(mac.equalsIgnoreCase(explicitMac_)){foundName_=nm;foundMac_=mac;scanFound_=true;}return;
            }
            if (nm.length() == 0) return;
            // 诊断：把扫到的每个设备都打出来（限 25 条）。
            // 为什么需要：下面只认关键字表里的名字，万一适配器名字不在表里
            //（山寨货命名千奇百怪），日志里就什么都看不到、完全无从判断。
            // 有了这份清单，就能看到它到底叫什么，再补关键字或按 MAC 连。
            if (scanSeen_ < 25) {
                bool hit = looksLikeAdapter(nm);
                Serial.printf("   [扫描] %-22s RSSI%4d %s\r\n", nm.c_str(),
                              dev->getRSSI(), hit ? "<== 认得这个名字" : "");
                scanSeen_++;
            }
            if (!looksLikeAdapter(nm)) return;
            if (scanFound_) return;              // 只记第一个匹配的
            scanFound_ = true;
            foundName_ = nm;
            foundMac_  = dev->getAddress().toString().c_str();
        });
        if (!ok) {
            st_ = St::Failed;
            retryCount_++;
            lastActionMs_ = now;
            return;
        }
        st_ = St::Scanning;
    }

    void finishScanAndConnect(uint32_t now) {
        serialBt_.discoverAsyncStop();
        delay(300);                              // 让扫描彻底停下来

        if (!scanFound_) {
            Serial.printf("[扫描结束] 共 %u 个有名字的设备，但没有一个匹配适配器关键字表\r\n",
                          (unsigned)scanSeen_);
            st_ = St::Failed;
            retryCount_++;
            lastActionMs_ = now;
            return;
        }
        Serial.printf("[扫描结束] 命中 %s (%s)，本轮共扫到 %u 个设备\r\n",
                      foundName_.c_str(), foundMac_.c_str(), (unsigned)scanSeen_);

        if(!connectionMemoryReady(millis()))return;
        BTAddress addr(foundMac_);
        auto channels = serialBt_.getChannels(addr);
        int ch = channels.empty() ? 0 : channels.begin()->first;

        st_ = St::Connecting;
        scanStartMs_ = now;
        if (serialBt_.connect(addr, ch)) {
            st_ = St::Connected;
            retryCount_ = 0;
            // 记住 MAC，下次直接连（避免每次扫 8 秒）
            prefs_.putString("mac", foundMac_);
            savedMac_ = foundMac_;
            haveSavedMac_ = true;
        } else {
            st_ = St::Failed;
            retryCount_++;
            lastActionMs_ = now;
        }
    }

    BluetoothSerial serialBt_;
    Preferences     prefs_;
    uint8_t         scanSeen_ = 0;     // 本轮扫描到多少设备（诊断用）
    String          localName_;
#endif

    St       st_ = St::Unsupported;
    uint32_t lastActionMs_ = 0;
    uint32_t scanStartMs_  = 0;
    uint32_t lostAtMs_     = 0;
    uint8_t  retryCount_   = 0;

    String   savedMac_;
    String   explicitMac_,pin_;
    bool     haveSavedMac_ = false;
    String   foundName_;
    String   foundMac_;
    bool     scanFound_ = false;
};

} // namespace obd
