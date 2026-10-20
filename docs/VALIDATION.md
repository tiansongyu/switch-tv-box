# 0.1.0 本地验证记录

验证日期：2026-10-20。结果覆盖宿主回归测试、真实 NRO 在 Eden 中运行、
局域网发现及受控媒体播放。物理 Switch 和手机端完整兼容性仍需单独验收。

## 构建与测试

```sh
TVBOX_TOOLCHAIN_IMAGE=switch-tv-box/toolchain:eden-macros ./scripts/build-local.sh release
```

本次 release 返回 0，完成全部宿主测试、干净的 Switch 交叉编译、功能检查
和 SD ZIP 打包。基础环境复用 `netease-switch/toolchain:local`，本项目使用
独立衍生镜像，未修改网易项目的镜像、账号或 SD 数据。

| 检查 | 结果 |
|---|---|
| 三入口首页、频道库、语言持久化、触摸区域、长标题 | 通过 |
| 播放视图：同 URL 重播、手动返回主页、音频/视频切换、停止画面保留 | 通过 |
| IPTV 数据、收藏重启恢复、取消收藏持久化、保存失败回滚 | 通过；数据集含 10,041 个频道、81 个源 |
| 播放所有权、协议协调、DLNA 会话、退出顺序 | 通过 |
| DLNA 视频 URI、签名网址、播放控制、协议交接、空媒体与 SOAP 错误 | 通过 |
| RTSP、配对、RSA、SDP、加密音频、时钟、FLUSH、重传 | 通过 |
| ALAC 编码→AES RTP→解密→Matroska→解码样本对比；PCM 样本对比 | 通过 |
| HLS、照片缓存、同会话重入 claim/stop | 通过 |
| 真实 TCP 控制、配对授权、32 个照片缓存短连接轮换 | 通过 |
| mDNS 现代/传统模式公告、查询、冲突改名、退出通知 | 通过 |
| SSDP 数据报消费、Eden socket ABI、就绪轮询与等待行为 | 通过 |
| C 安全检查的 ASan/UBSan | 通过 |
| 开发工作流、FFmpeg 安装、发现辅助程序 | 通过 |
| 安装包 | ZIP CRC、唯一 NRO、字体、直播源、许可证和敏感文件排除检查通过 |

音频、镜像运行时、照片、远程视频及播放视图的针对性 ASan/UBSan 回归亦通过。
宿主 GCC 12 的 PIE sanitizer 程序曾在 `main` 前随机失败，空程序也能复现；
宿主 sanitizer 测试改用 `-fno-pie -no-pie` 后通过，对照越界和整数溢出仍能检出。
未关闭 sanitizer，未修改宿主 ASLR。详见 [构建指南](BUILD.md)。

Switch 编译器为 devkitA64 GCC 15.2.0。构建确认启用 libmpv、deko3d、
Ed25519、libnx 随机源、PlayFair 和 Matroska muxer，并校验 ALAC/AAC/PCM、
JPEG/PNG 和 H.264 所需 FFmpeg 组件。完整日志见本地
`artifacts/validation/release.log`；日志、测试截图和二进制不进入源码仓库。

## 本次安装包

以下校验值读取自 `artifacts/validation/artifact-sha256.txt`：

| 文件 | SHA-256 |
|---|---|
| `switch-tv-box.nro` | `e59c01ed5119f1c74d08c561efb719eb3d69c8bf349abcf69290c0d3d292e108` |
| `dist/switch-tv-box-sdmc.zip` | `284ad81ccb5db45f1431a14f1fdb88b64077a6caca8e9db4d396106377744508` |

ZIP 内的 NRO 与根目录构建产物一致。重建可能因时间戳等因素产生不同校验值；
以上标识本次 release 产物。

Eden 衍生 SDK 调整 deko3d 宏代码上传分组，规避 v0.2.1 的容器引用失效；
181 条 GPU 指令、14 个入口地址及各入口完整后缀均与原版比较一致。
本项目标准及 Eden SDK 还修复 mpv HOS 对部分填充音频缓冲区的剩余容量计算。
测试先复现原函数计算错误，再验证修复后的实际 `get_state`/`audio_write`
函数：447 个容量组合通过。证据见
`artifacts/validation/standard-sdk-hos-capacity-verify.log`。

## Eden 中的视频投屏

采用网易项目已有的 Eden v0.2.1，在独立认证的 Xvfb 虚拟桌面运行真实 NRO，
使用显式兼容标记启用 FFmpeg 软件视频解码。deko3d 仍负责界面与画面呈现。
这绕过了该版 Eden 的 nvtegra 绿屏及设备共享内存残留问题；实体 Switch
默认继续使用硬件解码。启动方式见 [模拟器指南](EMULATOR.md)。

DLNA 已验证以下步骤：

- 在实体网卡发起 SSDP 查询，接收器的 LOCATION 为
  `http://10.30.0.44:49152/description.xml`，设备描述 URLBase 同为
  `http://10.30.0.44:49152/`，不再指向代理虚拟网卡。
- `SetAVTransportURI`、`Play` 控制真实 NRO 获取本地 HTTP 媒体，服务记录了
  MP4 Range 请求；播放位置持续增长。
- 使用时长 30.017 秒的 H.264/AAC 测试视频（640×360、30 fps），实际截图可见
  彩条和视频时间文字，确认视频画面已经呈现。
- 暂停后位置保持在 2 秒；暂停期间跳转到 12.5 秒，继续播放后位置增长到 14 秒。
- 快速 `Stop`→`Play`，不重新设置 URI：播放位置从 0 增长到 2 秒，截图可见
  彩条与 2.133 秒的时间文字，最后再次停止后返回 `STOPPED`。

发现证据：`artifacts/validation/dlna-ssdp-fixed-address.log`。
视频与控制证据：`artifacts/validation/dlna-video-software-decoder.log`、
`artifacts/debug/dlna-run-20261020-185412/eden-video.png`、
`artifacts/validation/dlna-video-controls-final.log`、
`artifacts/debug/dlna-controls-20261020-190356/eden-replay-video.png`。

快速停止重播曾暴露两个问题：旧主页请求再次发送 STOP，以及同 URL 新会话
没有清除主页覆盖状态。修复后，最终 release 的真实 NRO 已通过上述全部控制
步骤；`artifacts/debug/dlna-controls-20261020-190356/result.json` 的
`passed` 为 `true`。旧失败日志保留用于对照，不作为最终验收结果。

## Eden 中的 IPTV 与收藏

使用独立的本地 M3U，两个频道 URL 指向受控的同一测试视频，不改变预设在线源。
真实 NRO 已验证：打开 IPTV、选择来源、播放、X 呼出播放中的频道列表、
A 确认切台、Y 收藏、进入收藏栏目、退出重启后恢复收藏，以及取消收藏。
HTTP 请求日志明确依次出现 `channel: 1` 和 `channel: 2`。

证据位于 `artifacts/validation/`：

| 证据 | 说明 |
|---|---|
| `iptv-live-fixture.log` | 两个频道各自的 HTTP 206 请求 |
| `eden-iptv-playing-list.png` | 视频播放时打开频道列表 |
| `eden-iptv-favorite-added.png` | 添加收藏 |
| `eden-iptv-favorite-after-restart.png` | 重启后保留收藏 |
| `eden-iptv-favorite-removed.png` | 取消收藏 |
| `iptv-favorites-after-removal.txt` | 取消后持久化文件为空 |

这些结果验证了受控本地两频道的播放、切台与收藏流程，不代表所有在线源可用。
外部 IPTV 源仍观察到 TLS 握手失败，记录在
`artifacts/validation/eden-software-video-errors.log`。

## AirPlay 发现与音频

真实 NRO 的 `OPTIONS`、`/info` 和 `/server-info` 均返回成功；
Apple-Challenge 的 RSA 签名经公钥还原后，挑战值、接收地址和设备标识一致。
Eden 0.2.1 组播 socket 选项支持不完整，且宿主默认组播路由经过虚拟网卡；
发现助手改用 Avahi，在实体网卡发布真实 NRO 的身份与能力。
本机解析及抓包确认 `_airplay._tcp`、`_raop._tcp` 指向 `10.30.0.44:7000`，
退出时只撤销自己的公告。

用户此前确认 iPhone 音乐面板能发现并播放，但声音严重断续。
修复线程栈映射、BSD 等待及 SDK 音频容量后，真实 NRO 的约 5 秒 RAOP PCM
探测发送 627 个数据包；RTSP 控制全部返回 200，UDP 错误为 0，两个时钟请求均获回复，
该次运行未再出现线程创建或 `ao_hos` 写入错误。初次证据：
`artifacts/validation/raop-pcm-probe-fixed-sdk.log`。

最终安装的 NRO 校验值与本次交付产物一致。在同一个 Eden 进程完成上述
DLNA 视频控制后，再次发送约 5 秒 RAOP PCM：627 包、889,524 字节、5 个
同步包，UDP 错误为 0，RTSP 全部返回 200。记录见
`artifacts/validation/raop-pcm-after-video-final.log`。
`artifacts/validation/eden-final-playback-errors.log` 为 0 字节，未出现线程、
音频写入或 NvMap 残留错误；该最终会话没有主动访问外部直播源。
**手机听感和持续播放流畅度尚未重新验收**，短时协议探测不能代替这项验证。

## 离线规范与界面

规范归档包含 48 个 HTML 页面、294 个已记录 SHA-256 的文件；章节、搜索索引、
CSS 和字体本地链接验证通过。入口见 [离线归档](reference/README.md)。

16 个界面预览直接提取实际 ImGui 绘制代码，覆盖 IPTV/AirPlay/DLNA 三入口首页、
中英文投屏说明、配对、离线、播放状态、频道库、播放中列表和收藏空状态。
样本数据仅用于界面预览；图片保存在 `artifacts/ui/`，已目视确认无溢出。
真实 NRO 首页截图另见 `artifacts/validation/eden-three-entry-home.png`。

## 尚需验收

尚未验证物理 Switch 的电视音频输出、各代 iOS/macOS 互操作、长时间播放、
真实 Wi-Fi 丢包、休眠恢复及本次修复后的手机音频流畅度。AirPlay 照片与镜像
有宿主回归，但未完成本次模拟器端到端验收。DLNA 手机 App 兼容性与其他编码、
分辨率需继续测试；本次视频实测使用受控 HTTP 来源。
旧 `/stream` 镜像等未实现项见 [AirPlay 1 能力说明](AIRPLAY1.md)，不宣称完美兼容。
