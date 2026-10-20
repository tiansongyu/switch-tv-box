# Switch TV Box

把 Nintendo Switch 变成客厅电视盒子：电视直播、手机投屏、AirPlay 音乐与照片，共用硬件加速播放器。

本项目基于 [NX-Cast](https://github.com/Ode1l/NX-Cast) 0.3.2，保留其 DLNA、IPTV、配对镜像与播放器，在此基础上增加传统 AirPlay 1 音频和照片，重新设计电视界面。原项目及第三方许可证完整保留。

![Switch TV Box 首页](docs/screenshots/home.png)

*由实际 ImGui 绘制代码生成的预览，图中 IP、频道和时间为样例数据。*

## 功能

- 深色大字电视界面，默认中文，可切英文；显示时钟、IP、接收状态、配对码和继续播放。
- 保留 NX-Cast 的 M3U/M3U8 直播源、分类、搜索、收藏、最近观看、EPG、手柄及触摸操作。
- DLNA 投送，mpv/FFmpeg/deko3d/nvtegra 硬件加速播放器。
- AirPlay 1 RAOP：RSA/AES、ALAC/AAC/PCM、UDP 音频、音量、FLUSH、重传和实际音频输出链路。
- AirPlay 1 照片：JPEG/PNG、预缓存、缓存显示；在播放器中保持显示至停止或替换。
- 保留 URL/HLS 视频、PIN 配对和带声音的屏幕镜像；独立音乐使用主页音乐卡。

协议、真实编解码、生命周期和界面已有主机测试；Switch 交叉编译与 SD 安装包已通过，[本次验证记录](docs/VALIDATION.md)包含结果和产物校验值。**尚未完成物理 Switch 与各代 iOS/macOS 的互操作验收，不能称为“完美支持所有 AirPlay 1”。** 老式 `/stream` 镜像、服务器幻灯片/转场、歌曲封面显示等仍有明确缺口，详见 [AirPlay 1 能力与验证](docs/AIRPLAY1.md)。

## 安装与使用

构建产物为 `dist/switch-tv-box-sdmc.zip`。解压到 Switch SD 卡根目录，保留以下结构：

```text
switch/switch-tv-box/
  switch-tv-box.nro
  dlna/
  fonts/
  iptv/sources.txt
  airplay/
  licenses/
```

使用 Atmosphère/homebrew 环境，推荐完整应用模式启动。手机与 Switch 连接同一局域网，在支持 AirPlay 的应用中选择 **Switch TV Box**。镜像需要配对时，输入电视上显示的 PIN。

直播源可以放入 `switch/switch-tv-box/iptv/`，也可在频道库的直播源管理中添加。配置目录与 NX-Cast 分开；要迁移直播源，请复制原 `iptv/` 下自己的配置文件。AirPlay 身份与配对记录由本机生成，不随安装包分发。

| 操作 | 按键 |
|---|---|
| 首页选择 / 确认 | 方向键或摇杆 / A |
| 打开频道库 | X 或按下摇杆 |
| 继续观看 | 选择继续播放卡按 A，或 B |
| 音乐暂停 / 继续 | 选择音乐卡按 A |
| 播放画面暂停 / 跳转 / 音量 | A / 左右或 L、R / 上下 |
| 返回首页 / 退出 | B / + |

首页按钮也支持触摸。单 Joy-Con、Pro Controller 和掌机控制沿用上游逻辑。

## 本地开发

优先复用本机 `netease-switch/toolchain:local` Docker 镜像，并建立独立媒体工具链，不改动原镜像。没有该镜像时使用固定摘要的 devkitPro 基础镜像。

```sh
./scripts/build-local.sh image
./scripts/build-local.sh release
```

只跑测试：`./scripts/build-local.sh test`。只编译和打包：`./scripts/build-local.sh build`。VS Code 默认构建任务使用 Docker。完整说明见 [构建指南](docs/BUILD.md)。

生成八种真实 UI 预览：`python3 scripts/preview_home.py`，输出到 `artifacts/ui/`。

## 本地规范与来源

[AirPlay 规范离线入口](docs/reference/airplay-spec/index.html)已包含全部站点章节、打印版、搜索索引、脚本、样式、字体及源码/二进制示例；可离线浏览。下载清单和 SHA-256 见 [归档说明](docs/reference/README.md)。

- 上游基线：NX-Cast `0efdba4281122cdd97de0c3c1a7ebde7485d6d15`。
- 许可证：[GPL-3.0](LICENSE)，第三方来源见 [NOTICE](third_party/NOTICE.md)。
- 上游原始介绍保存在 [docs/upstream](docs/upstream/)。其他继承的历史文档和 plans 可能仍使用 NX-Cast 名称，以本 README、BUILD.md、AIRPLAY1.md 为当前版本说明。
- 本工具不提供 DRM 绕过，不支持 AirPlay 2 多房间或 Apple 认证。
