# 配置分层

1. `configs/vehicles/standard-obd-auto.json` 是平台通用默认配置说明。标准支持位图决定可尝试项目，未返回数据不会造值。
2. `configs/vehicles/lancer-2011-mt.json` 记录本项目已使用的翼神阈值，不包含私人 VIN、日志、蓝牙地址或猜测齿比。
3. `configs/adapters/connection-examples.json` 记录 BT、BLE、Wi-Fi 的输入字段和常见示例；实际参数以适配器厂商资料为准。BLE 需确认服务与收发特征，不能因名字相近就声称兼容。
4. `configs/reference-index.json` 是历史解包资料的来源与统计。原数据库放在仓库外，未在固件中批量导入 1304 个私有车型协议。

这些 JSON 为人可读配置与工具开发约定，**设备工具当前不能任意直接导入车型 JSON，也没有执行 Car Scanner 私有表达式的解释器**。固件自动读数使用 `firmware/src/obd/auto_discovery.h` 和当前标准 PID 解码。要增加私有信号，应提供可核实的请求、响应、缩放、单位和车型匹配，再开发受限适配器及测试；不要直接执行未知命令或写 ECU。

主题只绑定语义信号：`iat,coolant,maf,load,throttle,rpm,speed,voltage,oil_temp,oil_pressure,afr`。现有 layout-v1 固定横屏 480×320，改变屏幕尺寸需要新的渲染后端/布局引擎。

本机参考资料统一在 `OBD-本地参考资料` 同级目录，包括车辆数据库、适配器配置、已用翼神配置与当前 v30 固件；该目录不应上传公共仓库。
