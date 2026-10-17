# OBD Platform

在线主题社区：[shuangoggsccb.icu](https://shuangoggsccb.icu) · 项目源码：[GitHub](https://github.com/Overzsj/OBD-Platform) · 作者：[Overzsj](https://github.com/Overzsj)

面向经典 ESP32 的开源 OBD 仪表平台：通用读取固件、USB 设备工具和独立运行的主题社区。

本次整理基于已经使用的 v30 通用固件配置和 v32 设备工具。公开默认主题为原创几何样式 Open Instrument，原有 SUKI 图片与私人车辆记录不包含在仓库中。

## 复制给 Agent，开始使用

把下面整段发送给能访问项目文件和 USB 设备的 Agent。此文本是任务提示词，不是终端命令。Agent 的设备操作能力取决于所用工具；浏览器设备选择和授权仍由用户完成。

```text
请读取并实际使用这个项目帮助我配置 OBD 仪表：
https://github.com/Overzsj/OBD-Platform

先读取仓库当前 README、docs/agent-user-prompt.md 和 docs/hardware.md，
按当前代码与工具实际支持的能力执行，不要只复述说明。

在线社区：https://shuangoggsccb.icu
网页烧录入口：https://shuangoggsccb.icu/flash

我是设备使用者，不需要搭建网站或服务器。先检查电脑与 USB 设备，
再集中询问缺少的板型、Flash 容量、屏幕型号与分辨率、接线和车辆信息。
协助我选择兼容固件、主题和接线方式；能直接完成的下载、校验、
工具准备和参数配置请实际执行。

优先使用仓库中已有的桌面设备工具；如果我选择网页烧录，先核对在线页面
当前功能，并引导我本人完成 USB 设备选择和备份保存。不要绕过浏览器授权。
烧录前识别实际芯片与分区、保存完整备份、核对文件校验值和写入边界。
固件与主题分别处理，空白板使用完整固件包，不把社区应用 BIN 当作整板固件。
不兼容或备份失败时停止写入，说明具体原因。

先完成模拟测试，再在我确认已连接车辆后进行只读验证。
分别报告软件校验、烧录校验、实屏和实车结果；没有验证的不要声称通过。
```

[完整 Agent 使用指令](docs/agent-user-prompt.md) · [小白使用指南](docs/beginner-guide.md) · [在线社区](https://shuangoggsccb.icu)

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
