# 上传 GitHub

公开目录为 `OBD-Platform`，只上传本项目。不要上传平级的“OBD-本地参考资料”、原“工具”的备份/历史版本，或整个电脑项目目录。

## 网页操作

1. 在 GitHub 新建空仓库，例如 `obd-platform`；不需要再生成另一份 LICENSE。
2. 解压交付的 `OBD-Platform-GitHub.zip`，将内部文件上传。保留 `.github`、`.gitignore` 和 `.gitattributes` 隐藏文件。
3. 提交后查看 Actions，确认两种固件在 GitHub 环境也构建通过。本地通过不代表远端 CI 已执行。
4. 发布 Release 可附带固件包、完整源码包、独立社区 ZIP 和校验值。GPL 二进制分发需同时提供对应源码、构建配置与依赖许可说明。

## 使用 Git

在解压后的仓库根目录执行，仓库地址请替换成自己创建的地址：

```sh
git init -b main
git add .
git status
git commit -m "Initial open platform release"
git remote add origin https://github.com/YOUR_ACCOUNT/obd-platform.git
git push -u origin main
```

不要将示例地址原样执行。本次整理没有登录 GitHub、创建远端仓库或推送。

## 发布范围

原 SUKI 人物资源、旧商业字体位图、Car Scanner 解包数据库、真实行车记录和完整设备备份均未进入公开源码包。平台仍有历史 ABI 标识和文件名，已在主题格式说明中解释；不是所有屏幕与车辆的通用兼容承诺。

主题社区需要 Python 后端和持久存储，**不能把完整投稿/审核功能直接部署为 GitHub Pages 静态站**。可运行独立社区 ZIP，或将根目录 Dockerfile 示例交给支持容器的服务器；公网使用反向代理和限速。
