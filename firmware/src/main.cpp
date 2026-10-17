// ============================================================
//  ESP32 OBD 仪表  主程序
//  目标车辆: 三菱翼神 1.8 MT (4B10, 2011)
//
//  架构（双核）：
//    Core 0: link_task  链路状态机 + ELM327 轮询 + 写快照
//    Core 1: loop()     读快照 + 插值 + 串口命令 +（后续）刷屏
//
//  链路可替换（编译期 -DOBD_TRANSPORT=N，见 platformio.ini 的两个
//  测试台环境 esp32dev-fake / esp32dev-serial）：
//    0 = BT     真机：ESP32 经典蓝牙  成品 ELM327
//    1 = FAKE   测试台：片上假 ELM327（不需要适配器 / PC / 车）
//    2 = SERIAL 测试台：USB 串口当链路，PC 上的 Python 假扮 ELM327
//  价值：除"蓝牙物理链路"之外的一切都能先在桌面上验证完，配件到齐
//  后只需换回 BT 并补链路测试。
// ============================================================
#include <Arduino.h>
#include <atomic>
#include <esp_heap_caps.h>

#include "obd/pid_defs.h"
#include "obd/pid_scheduler.h"
#include "obd/elm_client.h"
#include "app/vehicle_state.h"
#include "app/alarms.h"
#include "app/selftest.h"
#include "app/gear_estimator.h"
#include "obd/link_recovery.h"
#include "app/device_storage.h"
#include "app/telemetry_pipeline.h"
#include "app/telemetry_signals.h"
#include "app/drive_journal.h"
#include "app/probe_archive.h"
#include "app/response_trace.h"
#include "obd/can_query.h"
#include "obd/auto_discovery.h"
#ifndef OBD_AUTO_ADAPT
#define OBD_AUTO_ADAPT 0
#endif
static obd::AutoDiscovery g_discovery;
#ifndef OBD_SUPPORT_PROBE
#define OBD_SUPPORT_PROBE 0
#endif
#include "ui/page_navigation.h"
#include "freertos/queue.h"
#include "ui/oled_ssd1306.h"
#include "ui/ui_oled.h"
#include "ui/buzzer.h"
#if OBD_DRIVE_REPLAY
#include "test/drive_replay.h"
static obd::DriveReplay g_replay;
#endif

#ifndef OBD_DISPLAY_TFT
#define OBD_DISPLAY_TFT 0
#endif
#if OBD_DISPLAY_TFT
#include "ui/ui_ili9488.h"
#include "ui/runtime_theme.h"
#endif

// ---- 链路选择 ----
#ifndef OBD_TRANSPORT
#define OBD_TRANSPORT 0
#endif
#define OBD_TP_BT     0
#define OBD_TP_FAKE   1
#define OBD_TP_SERIAL 2
#if OBD_TRANSPORT == OBD_TP_BT && (!defined(OBD_BT_PC_SIM) || !OBD_BT_PC_SIM)
#define OBD_MULTI_ADAPTER 1
#else
#define OBD_MULTI_ADAPTER 0
#endif

#if OBD_TRANSPORT == OBD_TP_BT
  #if defined(OBD_BT_PC_SIM) && OBD_BT_PC_SIM
    #include "bt/bt_spp_pc.h"
  #else
    #include "bt/adapter_transport.h"
  #endif
#if OBD_MULTI_ADAPTER
  typedef obd::AdapterTransport Transport;
#else
  typedef obd::BtSpp       Transport;
#endif
  #if defined(OBD_BT_PC_SIM) && OBD_BT_PC_SIM
    static const char* const kTransportName = "BT-PC-SIM";
  #else
    static const char* const kTransportName = "BT";
  #endif
#elif OBD_TRANSPORT == OBD_TP_FAKE
  #include "test/fake_elm.h"
  typedef obd::FakeElmIo   Transport;
  static const char* const kTransportName = "FAKE";
#else
  #include "test/serial_elm.h"
  typedef obd::SerialElmIo Transport;
  static const char* const kTransportName = "SERIAL";
#endif

// ---- 全局对象 ----
static Transport          g_tp;
static obd::ElmClient     g_elm;
static obd::PidScheduler  g_sched;
static obd::SnapshotStore g_store;
static obd::AlarmState    g_alarmState[obd::ALARM_COUNT];
static uint8_t audibleAlarmFlags(const obd::VehicleSnapshot& s) {
    return obd::audibleSnapshotFlags(s);
}
static TaskHandle_t       g_linkTask = nullptr;
static obd::DeviceStorage g_device;
static obd::DriveJournal g_journal;
static obd::SupportProbe g_support;
static obd::CanQuery g_canQuery;
static obd::ProbeArchive g_probeArchive;
static obd::ResponseArchive g_responseArchive;
struct UsbCommand { char text[384]; };
static std::atomic<uint32_t> g_adapterRestartAt{0};
static QueueHandle_t g_usbQueue=nullptr;
static std::atomic<uint8_t> g_requestedPage{0},g_visiblePage{0};
static std::atomic<uint32_t> g_pageFrames[obd::kDisplayPages];
static std::atomic<uint32_t> g_lightFrames{0},g_lightMilliHz{0};
static std::atomic<uint32_t> g_bootFrames{0},g_bootRenderUs{0},g_bootBytes{0},g_bootLastMs{0};
static std::atomic<bool> g_bootActive{false};
static std::atomic<bool> g_bootReplayRequested{false};

// UI 刷新统计（Core 1 写、Core 0 打印；纯计数，允许竞态）
static volatile uint32_t g_uiFrames = 0;
static volatile uint32_t g_uiMaxMs  = 0;

// ---- 显示侧插值器 ----
static obd::Smoother g_rpmSmooth, g_speedSmooth;

// OLED（SSD1306 128x64 I2C，SDA=21 SCL=22）
static obd::Ssd1306 g_oled;
#if OBD_DISPLAY_TFT
static obd::Ili9488 g_tft;
static obd::UiTft g_ui;
// SERIAL controls arrive on Core 0; only Core 1 may draw or use TFT SPI.
static std::atomic<bool> g_tftTestRequested{false};
#else
static obd::UiOled  g_ui;
#endif
static obd::Buzzer  g_buzzer;
static std::atomic<int8_t> g_buzzerPreview{-1};

// 真车 PID 体检的状态（命令 D 触发，scanTick 驱动）
static const uint8_t kScanBitmapPids[4] = { 0x00, 0x20, 0x40, 0x60 };
static bool    g_scanOn    = false;
static uint8_t g_scanStep  = 0;      // 0~3 = 查支持位；>=4 = 逐条 dump
static uint8_t g_scanIdx   = 0;
static bool    g_scanSync  = false;  // 体检开始前先做一次同步
static bool    g_scanPending = false; // D 命令已下，等当前轮询收完再进体检

// ------------------------------------------------------------
//  ElmClient 需要的 IO 抽象：桥接到当前链路
// ------------------------------------------------------------
struct LinkIo : public obd::ElmClient::Io {
    size_t write(const char* buf, size_t n) override { return g_tp.write(buf, n); }
    bool   connected() override { return g_tp.isConnected(); }
};
static LinkIo g_linkIo;

// Shared with host integration tests: decode samples and advance alarms once per new sample.
static void applyParsed(const obd::ParsedPid* p,uint8_t n,obd::VehicleSnapshot& s,uint8_t& freshMask,uint32_t observedMs) {
    obd::acceptParsed(p,n,s,freshMask,observedMs);
}
static void evalAlarms(obd::VehicleSnapshot& s,uint32_t now,uint8_t freshMask) {
    obd::evaluateSnapshot(s,g_alarmState,g_device.settings,freshMask,now);
}

// ------------------------------------------------------------
//  开机报告：芯片信息 + PID 表 + 报警阈值 + 协议层自检
//  为什么做成可重复调用：本机 CH340 板在 ESP32 复位后的 0.1~1.0s
//  窗口内串口输出会整段丢失（读句柄持续报 ERROR_ACCESS_DENIED），
//  开机 banner 与自检报告抓不到，因此报告必须能随时重放
//  （串口命令 'T'，见 loop）。
// ------------------------------------------------------------
static const char* resetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:   return "上电";
        case ESP_RST_EXT:       return "外部复位";
        case ESP_RST_SW:        return "软件复位";
        case ESP_RST_PANIC:     return "程序异常";
        case ESP_RST_INT_WDT:   return "中断看门狗";
        case ESP_RST_TASK_WDT:  return "任务看门狗";
        case ESP_RST_WDT:       return "其他看门狗";
        case ESP_RST_DEEPSLEEP: return "深度睡眠唤醒";
        case ESP_RST_BROWNOUT:  return "供电电压过低";
        case ESP_RST_SDIO:      return "SDIO复位";
        default:               return "未知";
    }
}

static void printStartupReport() {
    Serial.println();
    Serial.println("================================================");
    Serial.println(" ESP32 OBD Gauge  --  三菱翼神 1.8 MT (4B10)");
    Serial.println("================================================");
    Serial.printf("芯片      : %s rev %d\r\n", ESP.getChipModel(), ESP.getChipRevision());
    Serial.printf("核心数    : %d\r\n", ESP.getChipCores());
    Serial.printf("Flash     : %u bytes | PSRAM: %u bytes\r\n",
                  (unsigned)ESP.getFlashChipSize(), (unsigned)ESP.getPsramSize());
    Serial.printf("可用堆    : %u bytes\r\n", (unsigned)ESP.getFreeHeap());
    Serial.printf("SDK       : %s\r\n", ESP.getSdkVersion());
    const esp_reset_reason_t resetReason = esp_reset_reason();
    Serial.printf("重启原因  : %d (%s)\r\n", (int)resetReason, resetReasonName(resetReason));
    Serial.printf("构建时间  : %s %s\r\n", __DATE__, __TIME__);
    Serial.printf("链路通道  : %s\r\n", kTransportName);
#if OBD_TRANSPORT == OBD_TP_BT
    Serial.printf("经典蓝牙  : %s\r\n", g_tp.supported() ? "支持" : "不支持(需经典 ESP32)");
#else
    Serial.println("经典蓝牙  : 未链接（测试固件，不占 Flash）");
#endif

    Serial.printf("PROFILE|id=%s|protocol=AUTO|gear=%s|private_pid=bench-only\r\n",
                  obd::kVehicleProfile.id, obd::GearEstimator::configured(obd::kVehicleProfile.gears)?"CALIBRATED":"UNCALIBRATED");
    Serial.println("------------------------------------------------");
    Serial.println(" 启用的 PID:");
    uint8_t enabled = 0;
    for (size_t i = 0; i < obd::kPidCount; ++i) {
        const obd::PidDef& p = obd::kPidTable[i];
        if (!obd::profilePidEnabled(p.pid)) continue;
        enabled++;
        Serial.printf("   %02X  %-10s %-6s  %u字节  %ums\r\n",
                      p.pid, p.name, p.unit, p.bytes, obd::profileInterval(p.pid,p.minIntervalMs));
    }
    Serial.printf(" 启用 %u / 定义 %u\r\n", enabled, (unsigned)obd::kPidCount);

    Serial.println("------------------------------------------------");
    Serial.printf(" 蜂鸣器    : GPIO%u %s（串口命令 B 可切换驱动）\r\n",
                  (unsigned)obd::Buzzer::kPin,
                  g_buzzer.passive() ? "无源(方波驱动)" : "有源(电平驱动)");
    Serial.println(" 提示音    : 双短音=换挡提示  三短音=超速  长音=超温  单短音=低压  连续急促=严重级");
#if OBD_DISPLAY_TFT
    Serial.printf(" TFT      : ILI9488 480x320 RGB666 | SCLK=%d MOSI=%d CS=%d DC=%d RST=%d | SPI=%uHz\r\n",
                  TFT_PIN_SCLK, TFT_PIN_MOSI, TFT_PIN_CS, TFT_PIN_DC, TFT_PIN_RST, unsigned(TFT_SPI_HZ));
    Serial.printf(" 默认换挡灯: >= %.0f 慢闪 / >= %.0f 快闪 | 实际设置见 USB GET | 16行缓冲15360字节，变化块刷新 | 无PSRAM\r\n",
                  obd::UiTft::flashWarnRpm(), obd::UiTft::flashFastRpm());
#else
    Serial.printf(" 转速闪屏  : >= %.0f 慢闪(约2Hz) / >= %.0f 快闪(约7Hz)  整屏反白，硬件命令\r\n",
                  obd::UiOled::flashWarnRpm(), obd::UiOled::flashFastRpm());
#endif
    Serial.println("------------------------------------------------");
    Serial.println(" 默认报警阈值（实际保存值见 USB GET）:");
    for (uint8_t i = 0; i < obd::ALARM_COUNT; ++i) {
        const obd::AlarmCfg& a = obd::kDefaultAlarms[i];
        Serial.printf("   %-8s %s warn=%.1f crit=%.1f hyst=%.1f\r\n",
                      a.label, a.isLow ? "(低报)" : "(高报)", a.warn, a.crit, a.hyst);
    }

    // 协议层自检：无硬件也能验证解析逻辑
    obd::runProtocolSelfTest();
}

// ------------------------------------------------------------
//  串口命令。BT / FAKE 模式走 loop() 的串口输入；
//  SERIAL 模式下串口被 ELM 数据占用，改由链路任务识别 '@' 前缀的控制行
//  （见 linkTaskFunc 里的注释） 两处共用这一个 dispatchCommand。
//    T            重放开机报告 + 协议层自检
//    V <hex回包>  用真实解析器解析并回机器可读结果（PC 向量测试用）
//    W <hex回包>  逐字节喂进真实 feed() 再取结果（分片到达测试用）
//    S            输出当前快照（机器可读）
// ------------------------------------------------------------
static void handleVector(const char* arg, bool fragmented) {
    while (*arg == ' ') ++arg;

    obd::ParsedPid out[8];
    uint8_t n = 0;
    if (!fragmented) {
        n = obd::parsePidReply(arg, obd::kPidTable, obd::kPidCount, out, 8);
    } else {
        // 分片路径：走真实的 ElmClient::feed()，一次只喂一个字节。
        // 真链路上蓝牙分片到达是最容易翻车的地方，必须单独测。
        static obd::ElmClient    probeClient;
        static obd::PidScheduler probeSched;
        probeClient.begin();
        for (const char* q = arg; *q; ++q) {
            uint8_t b = (uint8_t)*q;
            probeClient.feed(&b, 1);
        }
        // W is a complete diagnostic vector; model the ELM prompt explicitly.
        const uint8_t prompt='>';
        probeClient.feed(&prompt,1);
        n = probeClient.consumeReply(millis(), probeSched, out, 8);
    }

    // 整行一次性输出：Core 0 的每秒日志也在写同一个串口，分多次 printf
    // 会被它插进中间（2026-10-03 实测 VEC 行被 "[FAKE ...]" 拦腰截断，
    // 于是 PC 端解析出 "2[FAKE FAKE/COOLDOWN] ELM状态=..." 这种鬼东西）。
    // 只有单次 write 才受串口锁保护，所以必须先拼好缓冲再一次输出。
    char line[384];
    size_t j = 0;
    j += (size_t)snprintf(line + j, sizeof(line) - j, "VEC|%s|%u", arg, n);
    for (uint8_t i = 0; i < n && j + 20 < sizeof(line); ++i) {
        j += (size_t)snprintf(line + j, sizeof(line) - j, "|%02X=%.3f", out[i].pid, out[i].value);
    }
    if (j + 12 < sizeof(line)) {
        snprintf(line + j, sizeof(line) - j, "|%s",
                 (obd::classify(arg) == obd::ReplyKind::Data) ? "DATA" : "NONDATA");
    }
    Serial.println(line);
}

static void printSnapshot() {
    obd::VehicleSnapshot s{};
    if (!g_store.read(s)) { Serial.println("SNAP|NO_DATA"); return; }
    Serial.printf("SNAP|rpm=%.0f|speed=%.0f|coolant=%.0f|volts=%.2f|throttle=%.0f"
                  "|oil=%.1f|oilp=%.2f|afr=%.2f|alarm=0x%02X|link=%u|ok=%u|fail=%u|ready=%u|valid=0x%02X|ms=%u|gear_est=%u|gear_cal=%u\r\n",
                  s.rpm, s.speed, s.coolantC, s.volts, s.throttle,
                  s.oilTempC, s.oilPressureBar, s.afr, s.alarmFlags, (unsigned)s.linkQuality,
                  (unsigned)s.totalOk, (unsigned)s.totalFail,
                  (unsigned)s.linkUp, (unsigned)s.validMask, (unsigned)millis(),
                  (unsigned)s.estimatedGear,(unsigned)obd::GearEstimator::configured(obd::kVehicleProfile.gears));
}

#include "app/usb_control.h"

static void dispatchCommand(const char* cmd) {
    while (*cmd == ' ') ++cmd;
    if (*cmd == 0) return;
    if (!strncmp(cmd,"Q ",2)) {
        UsbCommand request{};
        if(strlen(cmd)>=sizeof(request.text)) {usbReply(0,"error",false,"too_long");return;}
        strcpy(request.text,cmd);
        if(!g_usbQueue || xQueueSend(g_usbQueue,&request,0)!=pdTRUE) {
            unsigned id=0;sscanf(cmd,"Q %u",&id);usbReply(id,"error",false,"busy");
        }
        return;
    }
    switch (cmd[0]) {
        case 'T': case 't': printStartupReport();        break;
        case 'V': case 'v': handleVector(cmd + 1, false); break;
        case 'W': case 'w': handleVector(cmd + 1, true);  break;
        case 'S': case 's': printSnapshot();             break;
        case 'B': case 'b':                              // 切换有源/无源驱动
            g_buzzer.setPassive(!g_buzzer.passive());
            Serial.printf("蜂鸣器|驱动方式=%s|刚鸣了一声，听到了吗？\r\n",
                          g_buzzer.passive() ? "无源(方波)" : "有源(电平)");
            break;
        case 'R': case 'r':                              // 重新启用批量请求试探
            g_elm.enableBatchProbe(3);
            Serial.println("已重新启用批量请求试探（最多3次确认，失败立即退回单PID）");
            break;
        case 'D': case 'd':                              // 真车 PID 体检
            g_scanPending = true;                 // 交给链路任务在安全时机切入
            g_scanStep = 0; g_scanIdx = 0; g_scanSync = false;
            Serial.println("SCAN|start|先查支持位 01 00/20/40/60，再逐条 dump 定义表里的 PID");
            break;
        case 'P': case 'p':                              // 逐个试听全部提示音
            g_buzzer.playAll();
            break;
        case 'M': case 'm':                              // 蜂鸣器静音开关
            dispatchCommand("Q 0 MUTE_TOGGLE");
            break;
        case 'I': case 'i': {                            // I2C 扫描
            uint8_t a = g_oled.scanBus();
            Serial.printf("I2C|%s|addr=0x%02X\r\n", a ? "OK" : "NONE", a);
            break;
        }
        case 'O': case 'o': {                            // Display test pattern
#if OBD_DISPLAY_TFT
            g_tftTestRequested.store(true, std::memory_order_relaxed);
#else
            uint8_t a = g_oled.scanBus();
            if (!a) { Serial.println("OLED|FAIL|I2C 无应答"); break; }
            g_oled.begin(a);
            g_oled.init();
            g_oled.testPattern();
            Serial.printf("OLED|OK|addr=0x%02X|已下发外框+竖条纹+上半屏实心块\r\n", a);
#endif
            break;
        }
        case 'F': case 'f':                              // 严重级急促度档位
            dispatchCommand("Q 0 SOUND_NEXT");
            break;
        default:  Serial.printf("CMD|?|未知命令: %s\r\n", cmd); break;
    }
}

// ------------------------------------------------------------
//  真车 PID 体检（命令 D）
//  为什么需要：换一台车、换一个适配器，第一步都该先摸清"这台车支持哪些
//  PID、每条回包的原始字节长什么样"。光看"读数不对"是查不出原因的 
//  必须看到原始回包。顺序：先查支持位 01 00/20/40/60，再逐条 dump 我们
//  定义表里的每个 PID。
// ------------------------------------------------------------
static void scanTick(obd::ElmClient& e, obd::ElmClient::Io& io, uint32_t now) {
    if (!g_scanOn || e.state() != obd::ElmState::Ready || e.awaiting()) return;

    // 同步：把"迟到回包"读干净再开始。
    // 为什么必须做：ELM 协议没有请求 ID，一条超时后才到的回包会被当成下一条
    // 命令的回复  实测体检的第一条 01 00 收到了上一条 01 0D 的 "410D00"，
    // 结果最关键的"支持位 01-20"整段丢了。这里用 300ms 窗口把在途字节吸干。
    if (!g_scanSync) {
        uint32_t t0 = millis();
        while (millis() - t0 < 300) {
            while (g_tp.available() > 0) g_tp.read();
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        e.dropBuffered();
        g_scanSync = true;
        Serial.println("SCAN|synced|已清空在途字节，开始查询");
    }

    char cmd[12];
    if (g_scanStep < 4) {
        snprintf(cmd, sizeof(cmd), "01 %02X\r", kScanBitmapPids[g_scanStep]);
    } else if (g_scanIdx < obd::kPidCount) {
        snprintf(cmd, sizeof(cmd), "01 %02X\r", obd::kPidTable[g_scanIdx].pid);
    } else {
        g_scanOn = false;
        Serial.println("SCAN|done");
        return;
    }
    e.pollRaw(io, cmd, now);
}

// ------------------------------------------------------------
//  Core 0：链路 + 轮询任务
// ------------------------------------------------------------
static void linkTaskFunc(void*) {
#if OBD_DRIVE_REPLAY
    obd::VehicleSnapshot replay{};g_replay.begin(millis());
    for(;;){
        uint32_t now=millis();UsbCommand request;
        if(xQueueReceive(g_usbQueue,&request,0)==pdTRUE)processUsb(request.text,replay,now);
        replay.display=g_device.display;replay.settings=g_device.settings;replay.presentation=g_device.presentation;
        g_replay.apply(replay,g_alarmState,millis());g_store.write(replay);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
#endif
    g_tp.begin("OBD-Gauge");
    g_elm.begin();
    g_sched.begin();

    obd::VehicleSnapshot snap{};
    snap.display=g_device.display;snap.settings=g_device.settings;snap.presentation=g_device.presentation;
    uint32_t lastLog = 0;
    obd::LinkRecovery recovery;
    obd::GearEstimator gear;
    obd::ElmState lastState = obd::ElmState::Idle;
#if OBD_TRANSPORT == 1 && !OBD_DATA_TEST
    uint32_t lastAuxMs = 0;
#endif
    bool     ctrlIn  = false;                 // 正在收 '@' 控制行
    char     ctrl[128] = {0};
    int      ctrlLen = 0;
    bool     ctrlOverflow = false;
    uint64_t linkHist = 0;                    // 链路质量滑动窗口（最近 64 次）
    uint8_t  linkN    = 0;
    uint32_t okAtStart = 0, failAtStart = 0;

    for (;;) {
        uint32_t now = millis();
        UsbCommand request;
        if(xQueueReceive(g_usbQueue,&request,0)==pdTRUE)processUsb(request.text,snap,now);
        now=millis();
        g_tp.tick(now);
        g_elm.tick(g_linkIo, now);
        const auto curState = g_elm.state();
        if (curState != lastState) {
            if (curState == obd::ElmState::Ready) {
                g_sched.begin();
#if OBD_AUTO_ADAPT
                g_discovery.start(now);
#endif
                g_elm.enableBatchProbe(3);
                recovery.sessionStarted(now);
#if OBD_SUPPORT_PROBE
                g_support.start(now,g_device.boot,OBD_DATA_TEST?200:800);
                obd::syncProbeSnapshot(g_support,snap);
                for(int i=0;i<3;++i)snap.probeStates[i]=uint8_t(obd::ProbeState::Pending);
                snap.adapterVoltsValid=false;snap.oilReported=snap.voltsReported=-1;
#endif
                Serial.println("LINK|READY|scheduler restarted|batch probe enabled");
            }
            lastState = curState;
        }

        if (g_tp.isConnected()) {
            // 把链路上收到的字节喂给 ELM327 解析器，同时分离出"@ 控制行"。
            // 为什么需要：SERIAL 模式下串口被 ELM 数据占用，loop() 的串口命令
            // 被禁用了，网页端就没办法静音/试听/调急促度。ELM 的回包只可能是
            // 十六进制或 AT 文本，绝不会出现 '@'，所以用它当前缀既不冲突也不占带宽。
            // （2026-10-04 为网页端手动调试加的）
            while (g_tp.available() > 0) {
                uint8_t buf[64];
                size_t k = 0;
                while (k < sizeof(buf) && g_tp.available() > 0) {
                    int c = g_tp.read();
                    if (c < 0) break;
                    if (c == '@') { ctrlIn = true; ctrlLen = 0; ctrlOverflow=false; continue; }
                    if (ctrlIn) {
                        if (c == '\r' || c == '\n') {
                            if (ctrlLen && !ctrlOverflow) { ctrl[ctrlLen] = 0; dispatchCommand(ctrl); }
                            ctrlIn = false; ctrlLen = 0;
                        } else if (ctrlLen + 1 < (int)sizeof(ctrl)) {
                            ctrl[ctrlLen++] = (char)c;
                        } else ctrlOverflow=true;
                        continue;                 // 控制行不喂给 ELM 解析器
                    }
                    buf[k++] = (uint8_t)c;
                }
                if (k) g_elm.feed(buf, k);
            }

            const bool ready = (g_elm.state() == obd::ElmState::Ready);
            if (snap.linkUp != ready) {
                snap.linkUp = ready;
                if (!ready) snap.validMask = 0;  // A new session must acquire each PID again.
                g_store.write(snap);
            }

#if OBD_TRANSPORT == 1 && !OBD_DATA_TEST
            if (ready && now-lastAuxMs>=200) {
                lastAuxMs=now;
                g_tp.sampleDisplayAux(now,snap.oilTempC,snap.oilPressureBar,snap.afr);
                for (auto field : {obd::VF_OIL_TEMP,obd::VF_OIL_PRESSURE,obd::VF_AFR}) {
                    snap.validMask|=uint8_t(1u<<field); snap.valueMs[field]=now;
                }
                g_store.write(snap);
            }
#endif

            const auto transportAction=recovery.checkTransport(now,ready,g_elm.bytesIn(),g_elm.searching());

            // D 命令可能正落在一次普通轮询中间  必须等这条轮询收完再切进体检，
            // 否则它的回包会被当成体检第一条查询的结果，而且会误推进步骤计数，
            // 结果"01 00 支持位"根本没被真正查询（2026-10-04 在车上实测）。
            if (g_scanPending && !g_elm.awaiting()) {
                g_scanPending = false;
                g_scanOn      = true;
                g_scanStep    = 0;
                g_scanIdx     = 0;
                g_scanSync    = false;
            }

            // 发起轮询：体检模式下一律走原始命令，否则按调度器选 PID
            if (transportAction!=obd::RecoveryAction::None) {
                // Do not enqueue another command on the expired session.
            }
#if OBD_AUTO_ADAPT
            else if(!g_elm.awaiting()&&g_discovery.due(now)){
                if(g_elm.pollRaw(g_linkIo,g_discovery.command(),now))g_discovery.sent();
            }
#endif
            else if(g_canQuery.active()){
                if(!g_elm.awaiting()&&g_canQuery.due(now)&&g_elm.pollRaw(g_linkIo,g_canQuery.command(),now))g_canQuery.sent();
            } else if (g_scanOn) {
                scanTick(g_elm, g_linkIo, now);
            }
#if OBD_SUPPORT_PROBE
            else if(!g_elm.awaiting()&&g_support.due(now)) {
                if(g_elm.pollRaw(g_linkIo,g_support.command(),now))g_support.sent();
            }
#endif
            else {
                g_elm.poll(g_linkIo, g_sched, now);
            }

            // 收取回包
            if (transportAction==obd::RecoveryAction::None && g_elm.replyReady(now)) {
                obd::ParsedPid parsed[8];
                uint8_t got = g_elm.consumeReply(now, g_sched, parsed, 8);
                // Parsing / USB / SPI work can cross a millisecond boundary.
                // Timestamp and evaluate this sample with the same clock value.
                now=millis();
#if OBD_AUTO_ADAPT
                if(g_discovery.pending()){
                    g_discovery.accept(g_elm.lastReply(),g_elm.lastReplyHadPrompt(),now);
                    recovery.observe(false,g_elm.lastReplyHadPrompt(),now);
                    if(g_discovery.complete()){
                        for(uint8_t i=0;i<obd::kPidCount;++i)g_sched.setReported(i,g_discovery.reported(obd::kPidTable[i].pid));
                        Serial.printf("AUTO|READY|vin=%s|protocol=%s\n",g_discovery.vin,g_discovery.protocol);
                    }
                }else
#endif
                if(g_canQuery.pending()){
                    g_canQuery.accept(g_elm.lastReply(),g_elm.lastReplyHadPrompt(),now);
                    recovery.observe(false,g_elm.lastReplyHadPrompt(),now);
                    if(g_canQuery.reinit){g_elm.requestReinit();g_canQuery.reinit=false;}
                }else
#if OBD_SUPPORT_PROBE
                if(g_support.pending()) {
                    const uint8_t id=g_support.current();const bool initial=g_support.initial();
                    g_support.accept(g_elm.lastReply(),g_elm.lastReplyHadPrompt(),now);
                    const auto& result=g_support.results[id];
                    if(initial&&!obd::kSimulatedSource)g_probeArchive.save(id,result);
                    obd::syncProbeSnapshot(g_support,snap);
                    if(result.state==obd::ProbeState::Valid&&id>=3&&id<=5){
                        if(id==5){snap.adapterVolts=result.value;snap.adapterVoltsMs=now;snap.adapterVoltsValid=true;}
                        else {
                            const uint8_t pid=id==3?0x42:0x5C;
                            const auto field=id==3?obd::VF_VOLTS:obd::VF_OIL_TEMP;
                            if(id==3)snap.volts=result.value;else snap.oilTempC=result.value;
                            snap.validMask|=1u<<field;snap.valueMs[field]=now;
                            for(size_t i=0;i<obd::kPidCount;++i)if(obd::kPidTable[i].pid==pid)g_sched.setEnabled(i,true);
                        }
                    }
                    recovery.observe(result.state==obd::ProbeState::Valid,g_elm.lastReplyHadPrompt(),now);
                    Serial.printf("PROBE|id=%u|state=%u|prompt=%u|saved=%u|raw=",id,unsigned(result.state),unsigned(result.prompt),unsigned(initial&&g_probeArchive.writeOk));
                    for(const char* p=result.raw;*p;++p)Serial.printf("%02X",unsigned(uint8_t(*p)));Serial.println();
                    g_store.write(snap);
                } else
#endif
                {
                if (g_scanOn) {
                    // 体检模式：只报原始回包，不污染快照与报警状态
                    char vb[180];
                    if (g_scanStep < 4) {
                        Serial.printf("SCAN|支持位|pid=01 %02X|kind=%d|raw=\"%s\"\r\n",
                                      kScanBitmapPids[g_scanStep], (int)g_elm.lastReplyKind(),
                                      obd::vis(g_elm.lastReply(), vb, sizeof(vb)));
                        g_scanStep++;
                    } else {
                        Serial.printf("SCAN|数据  |pid=01 %02X|%s|kind=%d|raw=\"%s\"\r\n",
                                      obd::kPidTable[g_scanIdx].pid, obd::kPidTable[g_scanIdx].name,
                                      (int)g_elm.lastReplyKind(),
                                      obd::vis(g_elm.lastReply(), vb, sizeof(vb)));
                        g_scanIdx++;
                    }
                    if (g_scanStep >= 4 && g_scanIdx >= obd::kPidCount) {
                        g_scanOn = false;
                        Serial.println("SCAN|done");
                    }
                    g_store.write(snap);
                    vTaskDelay(pdMS_TO_TICKS(5));
                    continue;                  // 跳过常规的解析/报警/统计
                }

                uint8_t fresh = 0;                // 本轮哪些报警量拿到了新样本
                if (got) {
                    snap.lastOkMs = now;
                    snap.extraCount = 0;          // 每轮重建，避免无限增长
#if OBD_SUPPORT_PROBE
                    g_responseArchive.observe(parsed,got,snap,g_device.boot,now,g_elm.lastCommand(),g_elm.lastReply(),g_elm.lastReplyHadPrompt());
#endif
                    applyParsed(parsed, got, snap, fresh,now);
                } else {
                    snap.totalFail++;
                }
                recovery.observe(got>0,g_elm.lastReplyHadPrompt(),now);

                // 链路质量 = 最近 64 次尝试的成功率（滑动窗口）。
                // 为什么不用累计成功率：一次瞬断会把累计比值永久拉低 
                // 2026-10-03 回归实测：一次 12 秒静默把链路钉死在 93%，
                // 之后无论多好都回不到 100%，这个指标就失去诊断意义了。
                linkHist = (linkHist << 1) | (got ? 1ull : 0ull);
                if (linkN < 64) ++linkN;
                uint64_t mask = (linkN >= 64) ? ~0ull : ((1ull << linkN) - 1ull);
                snap.linkQuality = (uint8_t)(__builtin_popcountll(linkHist & mask) * 100 / linkN);

                evalAlarms(snap, now, fresh);
                g_store.write(snap);
                } // normal polling reply
            }


            const auto action=transportAction!=obd::RecoveryAction::None?transportAction:
                recovery.check(now,ready,g_elm.awaiting(),g_scanOn || g_scanPending);
            if (action!=obd::RecoveryAction::None) {
                Serial.printf("RECOVERY|action=%s|reason=%s|rx_bytes=%lu\r\n",
                    action==obd::RecoveryAction::Reconnect?"RECONNECT":"REINIT",
                    transportAction!=obd::RecoveryAction::None?"transport_no_rx":"adapter_silent",(unsigned long)g_elm.bytesIn());
                snap.linkUp=false;snap.validMask=0;snap.alarmFlags=0;snap.estimatedGear=0;
                memset(g_alarmState,0,sizeof(g_alarmState));
                gear.reset();
                ++snap.reinits;
                if(action==obd::RecoveryAction::Reconnect)++snap.reconnects;
                g_scanOn=false;g_scanPending=false;g_support.cancel();g_canQuery.cancel();g_discovery.cancel();snap.adapterVoltsValid=false;
                g_elm.requestReinit();
#if OBD_TRANSPORT == OBD_TP_BT
                if (action==obd::RecoveryAction::Reconnect) g_tp.requestReconnect(now);
#endif
                g_store.write(snap);
            }
        } else {
            recovery.transportDisconnected();g_support.cancel();g_canQuery.cancel();g_discovery.cancel();snap.adapterVoltsValid=false;
            snap.linkUp = false;
            snap.validMask = 0;
            snap.linkQuality = 0;
            snap.alarmFlags=0;
            memset(g_alarmState,0,sizeof(g_alarmState));
            g_store.write(snap);
        }

        now=millis();
        obd::expireAlarms(snap,g_alarmState,now);
        g_device.events.sample(0,g_alarmState[0].sev,snap.coolantC,snap.rpm,obd::valueFresh(snap,obd::VF_COOLANT,now),snap.linkQuality,g_device.boot,now);
        g_device.events.sample(1,g_alarmState[1].sev,snap.rpm,snap.rpm,obd::valueFresh(snap,obd::VF_RPM,now),snap.linkQuality,g_device.boot,now);
        g_device.tick(now);
#if OBD_DATA_TEST
        snap.testStage=g_tp.labStage(now);snap.testPhaseMs=g_tp.labPhaseMs(now);snap.testCycle=g_tp.labCycle(now);

#endif
        snap.eventCount=g_device.events.history.count;
        snap.display=g_device.display;snap.settings=g_device.settings;snap.presentation=g_device.presentation;snap.elmState=uint8_t(g_elm.state());snap.transportState=g_tp.isConnected();
        snap.estimatedGear=gear.update(obd::kVehicleProfile.gears,snap.rpm,snap.speed,
            obd::valueFresh(snap,obd::VF_RPM,now) && obd::valueFresh(snap,obd::VF_SPEED,now) &&
            uint32_t(now-snap.valueMs[obd::VF_RPM])<=500 && uint32_t(now-snap.valueMs[obd::VF_SPEED])<=500,now);
        g_store.write(snap);

        if(g_journal.due(now)){
            uint8_t mode=3;
#if OBD_MULTI_ADAPTER
            mode=uint8_t(g_tp.mode());
#elif OBD_TRANSPORT == OBD_TP_BT
            mode=0;
#endif
            g_journal.tick(snap,g_device.boot,now,ESP.getFreeHeap(),mode,g_tp.describeState().c_str());
        }
        // 每秒输出诊断
        if (now - lastLog >= 1000) {
            lastLog = now;
            uint32_t dOk = snap.totalOk - okAtStart;
            uint32_t dFail = snap.totalFail - failAtStart;
            okAtStart = snap.totalOk;
            failAtStart = snap.totalFail;

            Serial.printf("[%s %s] ELM状态=%d 能力(ate0=%d ats0=%d ath0=%d sp0=%d) 版本=%s 协议=[%s]\r\n",
                          kTransportName, g_tp.describeState().c_str(), (int)g_elm.state(),
                          (int)g_elm.caps().echoOff, (int)g_elm.caps().spacesOff,
                          (int)g_elm.caps().headersOff, (int)g_elm.caps().autoProtocol,
                          g_elm.caps().version, g_elm.caps().protocol);
            Serial.printf("       1秒内 成功%u 失败%u | 累计 %u/%u (%u%%) | 批量=%d | 链路 %u/%u 字节\r\n",
                          (unsigned)dOk, (unsigned)dFail,
                          (unsigned)snap.totalOk, (unsigned)(snap.totalOk + snap.totalFail),
                          (unsigned)snap.linkQuality, (int)g_elm.usingBatch(),
                          (unsigned)g_elm.bytesOut(), (unsigned)g_elm.bytesIn());
            Serial.printf("       堆 现在%u 最小%u | UI已刷%u帧 单帧峰值%ums | 闪屏%u次 | 运行%us\r\n",
                          (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                          (unsigned)g_uiFrames, (unsigned)g_uiMaxMs,
#if OBD_DISPLAY_TFT
                          (unsigned)g_ui.flashCount(),
#else
                          (unsigned)g_oled.invertCount(),
#endif
                          (unsigned)(now / 1000));
            // 有失败就把"最近一条回包"的原样打出来（转义后）。
            // 为什么关键：适配器说 "UNABLE TO CONNECT" / "NO DATA" / "SEARCHING"
            // 是完全不同的三种处境，不看原文就只能瞎猜。
            if (dFail > 0) {
                char vb[200];
                Serial.printf("       最近回包: 类型=%d 原文=\"%s\"\r\n",
                              (int)g_elm.lastReplyKind(),
                              obd::vis(g_elm.lastReply(), vb, sizeof(vb)));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);

    g_device.begin();
    g_journal.begin(); // Simulated sources mount only for maintenance, never append.
#if OBD_SUPPORT_PROBE
    if(!obd::kSimulatedSource){g_probeArchive.begin();g_responseArchive.begin();}
#endif
    g_journal.tick(obd::VehicleSnapshot{},g_device.boot,millis(),ESP.getFreeHeap(),3,"UNAVAILABLE");
    g_usbQueue=xQueueCreate(4,sizeof(UsbCommand));
    if(!g_usbQueue) {Serial.println("USB_QUEUE_FAILED");while(true)delay(1000);}
    printStartupReport();

#if OBD_DISPLAY_TFT
    obd::mountRuntimeTheme();
    Serial.printf("THEME|%s|%s\n",obd::g_runtimeTheme.status,obd::g_runtimeTheme.identity);
    g_tft.begin();
    g_ui.attach(&g_tft);
    g_ui.settings(g_device.settings);
    g_ui.setService([](const obd::VehicleSnapshot& s, uint32_t t) {
        g_buzzer.tick(audibleAlarmFlags(s), s.linkUp, t);
    });
    Serial.println("TFT|INIT_SENT|ILI9488 RGB666初始化指令已下发（SPI无读回，请检查实屏）");
#else
    // OLED 上电自检：扫总线  初始化  交给仪表界面
    uint8_t oledAddr = g_oled.scanBus();
    if (oledAddr) {
        g_oled.begin(oledAddr);
        g_oled.init();
        g_ui.attach(&g_oled);
        Serial.printf("OLED|OK|addr=0x%02X|仪表界面已接管\r\n", oledAddr);
    } else {
        Serial.println("OLED|NONE|未检测到屏，界面不启用（其余功能不受影响）");
    }
#endif

    // High-trigger active module on GPIO25: non-blocking startup rhythm.
    g_buzzer.begin();
    g_buzzer.setMuted(g_device.settings.muted);
    g_buzzer.setCritLevel(g_device.settings.soundLevel);
    if(!g_device.settings.muted&&(g_device.presentation.sounds&1))g_buzzer.selfTest();

    g_rpmSmooth.seed(0);
    g_speedSmooth.seed(0);

    // Core 0 跑采集，Core 1（Arduino loop）跑显示与串口命令
    xTaskCreatePinnedToCore(linkTaskFunc, "link_task", 8192, nullptr, 2, &g_linkTask, 0);
    Serial.println("Core 0 采集任务已启动；Core 1 负责显示/命令。");
    Serial.printf("串口命令: T=报告 V<hex>=向量 W<hex>=分片 S=快照 I=I2C扫描 O=屏幕测试 B=切驱动 M=静音 P=试听 F=急促度 D=体检 R=重试批量  (链路=%s)\r\n",
                  kTransportName);
    Serial.println("================================================");
}

void loop() {
    static uint32_t lastMs = 0;
    static obd::VehicleSnapshot local{};
    static uint32_t lastPrint = 0;

    uint32_t now = millis();
    if (lastMs == 0) lastMs = now;
    float dt = (now - lastMs) / 1000.0f;
    lastMs = now;
    if (dt > 0.5f) dt = 0.5f;

#if OBD_TRANSPORT != OBD_TP_SERIAL
    // 串口命令行（复位后 0.1~1.0s 输出会丢，故留这个重放入口）
    static char    s_cmd[384];
    static bool s_overflow=false;
    static uint16_t s_cmdLen = 0;
    while (Serial.available() > 0) {
        int c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (s_cmdLen && !s_overflow) { s_cmd[s_cmdLen] = 0; dispatchCommand(s_cmd); }
            s_cmdLen=0;s_overflow=false;
        } else if (s_cmdLen + 1 < sizeof(s_cmd)) {
            s_cmd[s_cmdLen++] = (char)c;
        } else {s_overflow=true;}
    }
#endif

    obd::VehicleSnapshot fresh;
    if (g_store.read(fresh)) local = fresh;   // 读不到就沿用上一帧
    // A concurrent acquisition may publish a sample after this loop began.
    now=millis();

    g_buzzer.setMuted(local.settings.muted);g_buzzer.setCritLevel(local.settings.soundLevel);
    const int8_t soundRequest=g_buzzerPreview.exchange(-1);
    if(soundRequest>=0)g_buzzer.preview(uint8_t(soundRequest),now);
#if OBD_DISPLAY_TFT
    g_ui.settings(local.settings);
    if(g_bootReplayRequested.exchange(false))g_ui.replayBoot();
#if OBD_TEST_PAGE_CYCLE_MS > 0
    static obd::PageCycleTimer pageCycle;
    if(pageCycle.tick(now,g_ui.bootAnimating(),OBD_TEST_PAGE_CYCLE_MS)) {
        uint8_t current=g_requestedPage.load();
        const uint8_t next=uint8_t(obd::adjacentPage(obd::DisplayPage(current),true));
        if(g_requestedPage.compare_exchange_strong(current,next))
            Serial.printf("TEST_PAGE|uptime_ms=%lu|page=%s\n",(unsigned long)now,obd::pageName(obd::DisplayPage(next)));
    }
#endif
    g_ui.page(obd::DisplayPage(g_requestedPage.load()));
#endif
    // 插值：把 2-6Hz 的采样平滑成连续运动
    float rpmShown   = g_rpmSmooth.update(local.rpm, dt, 0.15f);
    float speedShown = g_speedSmooth.update(local.speed, dt, 0.25f);
    // 水温刻意不插值：报警量必须反映真实值，否则触发会延迟

    // Buzzer/shift-light timing stays independent of the display frame limiter.
    g_ui.tickFlash(obd::valueFresh(local,obd::VF_RPM,now)?local.rpm:0, now);
    g_buzzer.tick(audibleAlarmFlags(local), local.linkUp, now);
#if OBD_DISPLAY_TFT
    const uint32_t uiInterval = g_ui.bootAnimating()?16:TFT_FRAME_MS;
    static bool testActive = false;
    static uint32_t testStartedMs = 0;
    if (g_tftTestRequested.exchange(false, std::memory_order_relaxed)) {
        g_ui.testPattern(local);
        now = millis();
        testStartedMs = now;
        testActive = true;
        Serial.println("TFT|PATTERN|RGB色条与480x320外框已下发；显示1.5秒，请目视检查颜色/偏移");
    }
    if (testActive && uint32_t(now - testStartedMs) >= 1500) {
        testActive = false;
        g_ui.invalidate();
    }
#else
    const uint32_t uiInterval = 50;
#endif
    static uint32_t lastUi = 0;
    if (now - lastUi >= uiInterval
#if OBD_DISPLAY_TFT
        && !testActive
#endif
    ) {
        lastUi = now;
        uint32_t uiT0 = micros();
#if OBD_DISPLAY_TFT
        const auto page=g_ui.page();const uint32_t before=g_ui.frames(page);
#endif
        g_ui.render(local, rpmShown, speedShown, now);
#if OBD_DISPLAY_TFT
        if(g_ui.frames(page)!=before) {
            g_pageFrames[uint8_t(page)].store(g_ui.frames(page));
            g_visiblePage.store(uint8_t(page));
        }
#endif
#if !OBD_DISPLAY_TFT
        g_oled.flushDirty();
#endif
        uint32_t uiMs = (micros() - uiT0) / 1000;
        if (uiMs > g_uiMaxMs) g_uiMaxMs = uiMs;
        ++g_uiFrames;
    }

#if OBD_DISPLAY_TFT
    if(!testActive)g_ui.renderShiftLights(local,rpmShown,millis());
    g_lightFrames.store(g_ui.lightFrames());
    g_lightMilliHz.store(uint32_t(g_ui.lightFrequency()*1000+.5f));
    g_bootFrames.store(g_ui.bootFrames());g_bootRenderUs.store(g_ui.bootRenderUs());g_bootBytes.store(g_ui.bootBytes());g_bootLastMs.store(g_ui.bootLastMs());g_bootActive.store(g_ui.bootAnimating());
#endif
    if (now - lastPrint >= 500) {
        lastPrint = now;
        Serial.printf("显示层 | RPM %.0f->%.0f  SPEED %.0f->%.0f  水温 %.0f  电压 %.2f  报警 0x%02X  链路 %u%%\r\n",
                      local.rpm, rpmShown, local.speed, speedShown,
                      local.coolantC, local.volts,
                      local.alarmFlags, local.linkQuality);
    }

#if OBD_DISPLAY_TFT
    if(g_adapterRestartAt.load() && int32_t(millis()-g_adapterRestartAt.load())>=0){Serial.flush();ESP.restart();}
    delay(1);    // Yield without imposing another 20ms on each rendered frame.
#else
    delay(20);
#endif
}
