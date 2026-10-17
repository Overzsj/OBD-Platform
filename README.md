# OBD Platform

面向经典 ESP32 的开源 OBD 仪表平台：通用读取固件、USB 设备工具和独立运行的主题社区。

本次整理基于已经使用的 v30 通用固件配置和 v32 设备工具。公开默认主题为原创几何样式 Open Instrument，原有 SUKI 图片与私人车辆记录不包含在仓库中。

## 从哪里开始

| 你想做什么 | 入口 |
|---|---|
| 让 Agent 帮我安装与使用 | [复制用户使用提示词](docs/agent-user-prompt.md) |
| 第一次使用，从零开始 | [小白使用指南](docs/beginner-guide.md) |
| 连接设备、烧录固件或主题 | `device-tool/启动工具.cmd` |
| 编译实车 / 模拟固件 | `firmware/`，环境 `vehicle` / `simulation` |
| 打开主题社区 | Windows 双击 `theme-community/启动社区.cmd`；或 `python theme-community/server.py` |
| 查看车辆与适配器配置 | `configs/` 与 [配置说明](docs/configuration.md) |
| 部署社区、备份主题数据 | [社区部署说明](theme-community/README.md) |
| 查看引脚与硬件范围 | [硬件说明](docs/hardware.md) |

## 快速启动

需要 Python 3.10+。Windows 安装 Python 时勾选加入 PATH；设备工具需要 Python 的 Tkinter。

```sh
python -m pip install -r requirements.txt
python device-tool/app/device_tool.py
```

社区服务只用 Python 标准库，**无需安装以上依赖，也不调用 Agent、云模型或外部账户**：

```sh
python theme-community/server.py
```

浏览器访问 http://127.0.0.1:5190 。初始只有一个真实可下载的 Open Instrument 主题。上传后进入待审核，不伪造作者、下载量或社区内容。

## 编译与打包

```sh
pio run -d firmware -e simulation
pio run -d firmware -e vehicle
python scripts/package_release.py
```

打包后 `device-tool/固件/{simulation,vehicle}/firmware-package.json` 可被设备工具直接选择。从源码检出时不依赖预编译二进制。

所有固件/主题写入前，设备工具先完整备份 4 MiB；分别校验映像、分区和主题资源。支持经典 ESP32 4 MiB 单应用分区，暂不支持 S3、C3、其他 Flash 容量和双应用 OTA。

## 数据适配范围

自动适配读取 ELM327 支持的标准 OBD PID、支持位图、协议和可用 VIN。没有返回、超时、未支持和数据过期会分别处理。不会自动猜测私有 CAN 解码、齿比、红线或不存在的油温/油压/实际空燃比。

默认底部五项：进气温度、水温、空气流量、发动机负荷、节气门。默认高转 5000、红线提示 6000 rpm 继承本项目的实车设置；其他车型使用前需要调整，不能由此推断车辆断油点。

实车日志延续两个 60 秒片段循环保留，按 1 秒采样/事件记录，约 5 秒落盘。模拟不写实车日志。默认关闭页面轮播。

## 测试

```sh
python -m unittest discover -s theme-community/tests -v
python scripts/check_repository.py
python firmware/tests/run_host.py --compiler /path/to/zig
```

主机测试默认可用 g++/clang++，也可显式传入 Zig。CI 运行固件策略测试、社区接口测试和两种固件构建。构建、主机测试与接口验证不等同于实车或物理屏幕验收。

## 许可与来源

项目代码采用 GPL-3.0-only，保留 [上游参考项目](https://github.com/steveEcode/obd_brz_gauge) 的 GPL-3.0 许可和来源说明。字体单独采用 OFL-1.1，见 `assets/fonts/OFL.txt`。[第三方说明](THIRD_PARTY_NOTICES.md) 记录范围。

Car Scanner 解包数据库的再分发许可未确认，公开仓库不携带该数据库；本机参考资料与配置索引已另行整理。主题投稿者需要声明资源许可，维护者审核后再公开。
