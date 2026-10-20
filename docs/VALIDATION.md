# 本地验证记录

## 0.1.1：实际 CCTV-3 入口的备用线路切换

2026-10-20，复制当前桌面的实际频道列表和播放设置到独立认证的 Xvfb，
运行 Eden 0.2.1。直接选择“全部频道”第三行 **CCTV-3 综艺**，保留其原始
移动线路；没有改写该频道 URL。程序约 5.030 秒后自动使用已经加载的
iptv-org 同台备用线路，随后以 1280×720 连续播放 **300.325 秒**。

- 本次沿用桌面保存的“稳定”缓冲档：30 秒起播、120 秒预读目标；备用线路
  实际约缓冲 30.056 秒出画面。默认 60 秒大缓冲参数保持原有实现。
- 原始 URI 哈希 `f71b45e6` 与备用 URI 哈希 `e50d0c35` 均出现在数字采样中；
  101 个连续播放采样始终使用备用 URI，逻辑频道和最近播放仍为 `aba733f6`。
- 播放期间没有再次缓冲、跳时或新增渲染/解码丢帧；初始渲染丢帧计数为 3，
  后续保持不变，累计进度偏差最大 0.237 秒。
- 缓存为 10.539～36.907 秒；66 个 HTTP 响应正常结束，两次慢分片续传完成；
  验收期间运行错误日志为 0 字节。
- 宿主 ASan/UBSan 覆盖同台匹配、CCTV-3/CCTV-13 区分、未知地区后缀、
  重复 URL、三次备用上限、旧快照、被其他协议接管、命令拒绝、直接 URL
  不继承备用线路，以及收藏和最近播放保持不变。完整宿主回归、Switch
  交叉编译、功能检查和 SD 打包通过，见本地
  `artifacts/validation/iptv-fallback-release-build.log`。

[机器可读结果](validation/cctv3-fallback-20261020.json) 与
[数字采样](validation/cctv3-fallback-20261020.log) 按 SHA-256 对应。
本次程序为代码提交 `00a4013`，以下哈希绑定本次实测构建：

| 文件 | SHA-256 |
|---|---|
| `switch-tv-box.nro` | `41c0489f9a005000ec9bb2344e845e26f9f6b6ba28eb6a44c7469340b00fba4d` |
| `switch-tv-box-sdmc.zip` | `50b78b25d907d8fe52add9d82c53db1535c8d0b336fb51e63a78a37bb6649dc2` |

本结果只验证当前片源、网络和模拟器条件；不代表其他上游线路永久可用。
实体 Switch 和主观音频听感未验收。Eden 退出时已知的非致命映射清理诊断
仍可能出现，不计入上述播放验收窗口。AirPlay/DLNA 的前期验证范围见下文。

## 0.1.0 本地验证记录

验证日期：2026-10-20。结果覆盖宿主回归测试、真实 NRO 在 Eden 中运行、
局域网发现及受控媒体播放。物理 Switch 和手机端完整兼容性仍需单独验收。

### 构建与测试

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
| IPTV 画质、缓冲、配置持久化、保存失败回滚、同 URI 协议交接 | 通过 |
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
| Eden 物理网卡 HTTP 辅助 | 27 项离线测试通过；无外网、只读源码容器复测通过 |
| 完整源码与运行文件归档 | 11 项测试通过；包含 21 个必需资源、CRC、路径、完整性与验收 NRO 哈希校验 |
| 安装包 | ZIP CRC、唯一 NRO、字体、直播源、许可证和敏感文件排除检查通过 |

音频、镜像运行时、照片、远程视频及播放视图的针对性 ASan/UBSan 回归亦通过。
宿主 GCC 12 的 PIE sanitizer 程序曾在 `main` 前随机失败，空程序也能复现；
宿主 sanitizer 测试改用 `-fno-pie -no-pie` 后通过，对照越界和整数溢出仍能检出。
未关闭 sanitizer，未修改宿主 ASLR。详见 [构建指南](BUILD.md)。

Switch 编译器为 devkitA64 GCC 15.2.0。构建确认启用 libmpv、deko3d、
Ed25519、libnx 随机源、PlayFair 和 Matroska muxer，并校验 ALAC/AAC/PCM、
JPEG/PNG 和 H.264 所需 FFmpeg 组件。完整日志见本地
`artifacts/validation/iptv-large-buffer-release.log`。随后最新完整分片起播策略
通过针对性 ASan/UBSan 与 SDK 检查，并再次完整交叉编译、打包；
日志见 `artifacts/validation/iptv-live-edge-build.log`。
完整调试日志、测试截图和二进制不进入源码仓库；下文单独归档了不含 URL
或凭据的 CCTV-3 验收指标和数字采样记录。

### 本次安装包

以下校验值读取自 `artifacts/validation/artifact-sha256.txt`：

| 文件 | SHA-256 |
|---|---|
| `switch-tv-box.nro` | `3b61d66d49a4addac8dfe074eaafb4c9b420ba754b944815e21410c057a7abee` |
| `dist/switch-tv-box-sdmc.zip` | `61749ea3de1d14811cdadaae0ec5e86c08ef502c0a2d9df9881ddd3355980293` |

ZIP 内的 NRO 与根目录构建产物一致。重建可能因时间戳等因素产生不同校验值；
以上标识本次 release 产物。

Eden 衍生 SDK 调整 deko3d 宏代码上传分组，规避 v0.2.1 的容器引用失效；
181 条 GPU 指令、14 个入口地址及各入口完整后缀均与原版比较一致。
本项目标准及 Eden SDK 还修复 mpv HOS 对部分填充音频缓冲区的剩余容量计算。
测试先复现原函数计算错误，再验证修复后的实际 `get_state`/`audio_write`
函数：447 个容量组合通过。证据见
`artifacts/validation/standard-sdk-hos-capacity-verify.log`。

### Eden 中的视频投屏

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
没有清除主页覆盖状态。修复后，本轮前期构建的真实 NRO 已通过上述全部控制
步骤；`artifacts/debug/dlna-controls-20261020-190356/result.json` 的
`passed` 为 `true`。旧失败日志保留用于对照，不作为最终验收结果。

### Eden 中的 IPTV 与收藏

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

### IPTV 画质、缓冲与在线 CCTV-3

画质逻辑在真实 Eden 中用受控 HLS 主播放列表验证：原画请求 720p 版本，
保存“流畅”后重新播放同一频道并请求 360p 版本；日志分别记录
1280×720 与 640×360，播放位置持续增长，收藏数据保持原样。
该前期构建使用较小的缓冲参数；证据见
`artifacts/validation/iptv-quality-and-cctv3-stable.log`、
`iptv-quality-requests.log` 和 `iptv-quality-smooth-playing.png`。
最终缓冲参数另经宿主测试与真实 NRO 验证。

默认“大缓冲”使用 60 秒起播门槛、180 秒预读目标、96 MiB 前向与
4 MiB 回看压缩媒体包缓存，不包含解码及渲染内存。直播窗口、网络与字节
上限可能使缓存达不到目标；时间戳缺口也可能令缓存秒数高于真实可播时长，
因此验收同时检查播放位置增量、状态与掉帧，不能只看缓存数字。

原移动源的 CCTV-3 在当前网络中连接失败；后续实播使用已有 `iptv-org`
中文清单中的 **CCTV-3 (720p)**，通过物理网卡 HTTP 辅助访问。该标题来自
清单；实测来源曾先后提供 1280×720 和 1920×1080，且都是单版本 HLS，
不能用画质设置强制转成更低分辨率。

保留了三个未通过的播放样本：

- 首次大缓冲测试在 267.4 秒连续播放后再次缓冲，随后跳过约一分钟内容。
  期间没有新增解码丢帧。证据：
  `cctv3-largebuffer-first-attempt-iptv-playback.log`。
- 修复辅助程序的保活响应边界后再测，仍出现跳时。逐请求诊断显示一个
  5.36 MB 分片耗时 60.07 秒，其中等待上游 59.84 秒、发送给模拟器约
  7 毫秒；另一分片 4.29 MB 只需 3.93 秒。10 秒 socket 采样确认慢连接
  仅收到约 468 KB，而模拟器及时收完全部字节。证据：
  `eden-iptv-direct-20261020-215651-222937.log` 和
  `cctv3-framing-fix-attempt-iptv-playback.log`。
- 加入带强 ETag 校验的字节续传后，真实慢分片成功续传完毕，但默认从短
  播放列表的较旧分片起播，仍在起播后出现跳片。证据：
  `cctv3-resume-initial-attempt-iptv-playback.log` 和
  `eden-iptv-direct-20261020-220914-245409.log`。

上述失败样本不作为发布通过依据。它们表明个别上游连接的持续低速会耗尽
缓存，并使分片离开约 30 秒的源播放窗口；仅继续增大缓冲不能补回已过期内容。
最终构建对 IPTV 的 HLS URL 从最新完整分片起读，再积累起播缓冲；其他协议、
普通视频 URL 和结束的点播列表不改变起点。模拟器的可选 HTTP 辅助只对符合
条件的慢速媒体响应续传，并校验同一强 ETag、精确字节区间和剩余长度，最多
重连两次，避免把不同版本的内容拼接在一起。

#### 本次发布通过样本

最终 NRO 在独立认证的 Xvfb 桌面运行 Eden 0.2.1，通过上述物理网卡辅助
实际播放 CCTV-3。首次播放约等待 72.1 秒，随后 **连续播放 309.343 秒
（5 分 9 秒）**，104 个连续采样均处于播放状态：

- 画面为 1280×720；起播后没有再次缓冲，也没有播放位置跳跃。
- 播放进度与经过时间的最大累计偏差为 0.275 秒。
- 起播后新增渲染丢帧和解码丢帧均为 0；起播时渲染丢帧计数已有 2。
- 压缩包缓存读数为 52.760～78.677 秒，运行错误日志为 0 字节。
- 77 个 HTTP 响应均正常结束；本次通过样本没有触发字节续传。
- 起播后约一分钟和五分钟截图均确认 CCTV-3 视频画面正常呈现。

机器可读结果见 [验收记录](validation/cctv3-20261020.json)，对应
[数字采样日志](validation/cctv3-20261020.log) 的 SHA-256 与记录一致。
门槛检查每约 3 秒采样、序号连续、日志新鲜度、逐次及累计时间偏差、
状态、掉帧和错误，并核对测试 SD、根目录及发布 ZIP 中 NRO 的哈希一致。
本结果只覆盖这一次片源和网络条件；未进行主观音频听感或物理 Switch 验收，
不代表所有在线源和长期网络条件都能流畅播放。

### AirPlay 发现与音频

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

本轮前期构建在同一个 Eden 进程完成上述 DLNA 视频控制后，再次发送约
5 秒 RAOP PCM：627 包、889,524 字节、5 个
同步包，UDP 错误为 0，RTSP 全部返回 200。记录见
`artifacts/validation/raop-pcm-after-video-final.log`。
`artifacts/validation/eden-final-playback-errors.log` 为 0 字节，未出现线程、
音频写入或 NvMap 残留错误；该最终会话没有主动访问外部直播源。
**手机听感和持续播放流畅度尚未重新验收**，短时协议探测不能代替这项验证。

### 离线规范与界面

规范归档包含 48 个 HTML 页面、294 个已记录 SHA-256 的文件；章节、搜索索引、
CSS 和字体本地链接验证通过。入口见 [离线归档](reference/README.md)。

21 个界面预览直接提取实际 ImGui 绘制代码，覆盖 IPTV/AirPlay/DLNA 三入口首页、
中英文投屏说明、配对、离线、播放状态、频道库、播放中列表和收藏空状态。
以及画质、缓冲和设置保存反馈。
样本数据仅用于界面预览；图片保存在 `artifacts/ui-playback-settings-large-buffer/`，
已目视确认无溢出。
真实 NRO 首页截图另见 `artifacts/validation/eden-three-entry-home.png`。

### 尚需验收

尚未验证物理 Switch 的电视音频输出、各代 iOS/macOS 互操作、长时间播放、
真实 Wi-Fi 丢包、休眠恢复及本次修复后的手机音频流畅度。AirPlay 照片与镜像
有宿主回归，但未完成本次模拟器端到端验收。DLNA 手机 App 兼容性与其他编码、
分辨率需继续测试；本次视频实测使用受控 HTTP 来源。
旧 `/stream` 镜像等未实现项见 [AirPlay 1 能力说明](AIRPLAY1.md)，不宣称完美兼容。
