# 0.1.0 本地验证记录

验证日期：2026-10-20。以下结果来自本机工具链和协议测试，不代表物理 Switch 或 Apple 设备验收。

## 构建与测试

`./scripts/build-local.sh release` 返回 0，完成全部宿主测试、干净的 Switch
交叉编译、功能检查和 SD ZIP 打包。使用独立的
`switch-tv-box/toolchain:local`，基础环境复用 `netease-switch/toolchain:local`。

| 检查 | 结果 |
|---|---|
| 首页、频道库、语言持久化、触摸区域、长标题 | 通过 |
| IPTV 数据和保存失败回滚 | 通过；数据集含 10,041 个频道、81 个源 |
| 播放所有权、协议协调、DLNA 会话、退出顺序 | 通过 |
| RTSP、配对、RSA、SDP、加密音频、时钟、FLUSH、重传 | 通过 |
| ALAC 编码→AES RTP→解密→Matroska→解码样本对比；PCM 样本对比 | 通过 |
| HLS、照片缓存、同会话重入 claim/stop | 通过 |
| 真实 TCP 控制、配对授权、32 个照片缓存短连接轮换 | 通过 |
| mDNS 现代/传统模式的公告、查询、冲突改名、退出通知 | 通过 |
| C 安全检查的 ASan/UBSan | 通过 |
| 开发工作流、FFmpeg 安装脚本 | 9 + 11 项 Python 测试通过 |
| GitHub Actions 配置 | actionlint 通过 |
| 安装包 | ZIP CRC、29 个条目、唯一 NRO、字体、直播源和许可证检查通过 |

另外，音频、镜像运行时、照片和远程视频的针对性 ASan/UBSan 回归通过。
宿主 GCC 12 的 PIE sanitizer 程序曾在 `main` 前随机失败，空程序也能复现；
宿主 sanitizer 测试改用 `-fno-pie -no-pie` 后重复启动通过，故意越界和
整数溢出的对照程序仍被正确检出。未关闭 sanitizer，未修改宿主 ASLR。
详见 [构建指南](BUILD.md)。

Switch 编译器为 devkitA64 GCC 15.2.0；SDK 和依赖版本见
[BUILD.md](BUILD.md)。构建确认启用 libmpv、deko3d、Ed25519、
libnx 随机源、PlayFair 和 Matroska muxer；另校验 ALAC/AAC/PCM、
JPEG/PNG 和 H.264 所需 FFmpeg 组件。

完整运行日志、包版本和校验值保存在本地 `artifacts/validation/`；
它们和生成的二进制不进入源码仓库。

## 本次安装包

| 文件 | SHA-256 |
|---|---|
| `switch-tv-box.nro` | `57dd343c485ddb53deab1438799f88bc28d64c5ec098554b490306e8826bb40a` |
| `dist/switch-tv-box-sdmc.zip` | `8da35da09b177dddab31ca0013f00c692e4d6878cfcc0d6410f424f83bb91986` |

ZIP 内的 NRO 与仓库根目录构建产物逐字节一致。重新构建可能因时间戳等因素
产生不同校验值；以上仅标识本次本地交付产物。

## 离线规范与界面

规范归档包含 48 个 HTML 页面、294 个已记录 SHA-256 的文件，章节、搜索索引、
CSS 和字体的本地链接均验证通过。入口见 [离线归档](reference/README.md)。
八种界面预览来自实际 ImGui 绘制代码，使用样例数据。

## 尚需真机验收

尚未验证物理 Switch 上的电视音频输出、各代 iOS/macOS 互操作、
持续播放、真实 Wi-Fi 丢包以及休眠恢复。旧 `/stream` 镜像等未实现项和
逐项验收步骤见 [AirPlay 1 能力说明](AIRPLAY1.md)，因此不宣称完美兼容。
