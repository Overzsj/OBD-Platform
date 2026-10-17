# 主题 ABI 与贡献

当前 `suki-gauge-v1`（设备侧引擎标识 `suki-layout-v1`）是历史 ABI 名称。为兼容保留；并不要求主题使用 SUKI 图片。主工具通过编码器注册表选择布局编码器。

- 源包 `.sukitheme`：纯 JSON 数据，大小 ≤2 MiB，包含 SHA-256 完整性信息和固定尺寸资源。
- 编译包：`SUKITH29` 二进制资源，包含 CRC、目录和受限槽位，写入独立 theme 分区。
- 当前五个槽位默认 IAT、水温、MAF、负荷、节气门。主题不携带 Python、JS、CAN 命令或可执行插件。
- 主题社区校验源包全部资源和完整性；发布前还需要人工检查资源权利和内容。SHA-256 是完整性校验，不是作者数字签名。

修改基础主题可运行 `python scripts/generate_open_theme.py`，依赖 Pillow 和附带 OFL 字体。用现有编码器生成：

```sh
python firmware/tools/runtime_theme.py firmware/themes/open-instrument-1.0.0.sukitheme --output open.theme.bin
```

增加不同尺寸/布局，应新增独立 ABI 编码器，并同时提供设备渲染后端与边界测试，不能仅修改 JSON 的分辨率字段。
