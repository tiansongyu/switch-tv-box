# 在虚拟桌面使用 Eden

本机基于 `~/github/netease-switch` 已安装的 Eden v0.2.1，
运行实际的 `switch-tv-box.nro`。2026-10-20 已通过独立的 deko3d
宏兼容 SDK，在原 Eden 中直接显示 IPTV / AirPlay / DLNA 三入口首页，
并在虚拟桌面验证 DLNA 视频投屏、IPTV 切台与收藏，无需 GDB。
普通 deko3d 构建会触发原始 v0.2.1 的 GPU 崩溃；仅添加下述
服务兼容标记不足以解决它。

模拟器的配置、SD 卡和缓存使用独立目录，不使用网易项目的账号数据。

## Eden 中只显示英文文字界面

`QUICK START`、`PLAYER CONTROLS`、`IPTV`、`DIAGNOSTICS` 是程序保留的文字首页。
看到它们不表示下载了另一份命令行版本：图形首页需要播放器和图形后端完成初始化。
Eden 0.2.1 在缺少兼容标记时会调用尚未实现的系统服务，可能一直停在这一步。

本机推荐在仓库根目录运行启动器，让它安装完整资源并创建兼容标记：

```sh
python3 scripts/run-eden.py --interface enp7s0
```

在其他电脑手动使用 Eden 时：

1. 停止正在运行的 NRO，在 Eden 的 **File → Open Eden Folder** 中找到当前模拟器
   数据目录；如设置了自定义 SD 路径，应使用设置中实际的 `sdmc` 目录。
2. 将 `switch-tv-box-sdmc.zip` 解压到该 `sdmc` 根目录，保留
   `switch/switch-tv-box/switch-tv-box.nro`、`fonts/` 等完整目录。
3. 在 `sdmc/switch/switch-tv-box/` 中新建一个空文件 **`eden-compat.flag`**。
   确认没有被文件管理器自动添加 `.txt` 扩展名。标记必须位于模拟 SD 的上述固定路径；
   如果从下载目录打开 NRO，单独把标记放在下载目录不会生效。
4. 重新启动 NRO。兼容标记只在程序启动时读取，运行中添加不会生效。
   如果提示缺少解密组件，对本项目自制 NRO 选择 **No**。

该标记只用于 Eden，不应复制到实体 Switch。图形界面启动成功也不代表手机发现
已自动配置。v0.1.2 起，NRO 自行发送 AirPlay 和 DLNA 发现公告，
启动器不再自动启动 Avahi 发现助手。

2026-10-21 在独立虚拟桌面对正式 v0.1.1 NRO 做过对照：SHA-256 为
`41c0489f9a005000ec9bb2344e845e26f9f6b6ba28eb6a44c7469340b00fba4d`。
去掉标记后出现上述文字首页，日志记录未实现的
`60(SetMediaPlaybackStateForApplication)`；恢复标记并重启后显示 IPTV / AirPlay /
DLNA 图形首页。同一 NRO 无需替换为其他构建。

## 本机路径

| 内容 | 路径 |
|---|---|
| 模拟器 | `~/.local/bin/eden` |
| 本项目启动器 | `scripts/run-eden.py` |
| 参考启动脚本 | `~/github/netease-switch/scripts/eden-virtual-desktop.sh` |
| 独立状态目录 | `~/.local/share/switch-tv-box/eden/` |
| Qt 配置 | `~/.local/share/switch-tv-box/eden/config/eden/qt-config.ini` |
| 模拟 SD 卡根目录 | `~/.local/share/switch-tv-box/eden/data/eden/sdmc/` |
| 模拟器日志 | `~/.local/share/switch-tv-box/eden/data/eden/log/eden_log.txt` |

当前可见桌面为 `DISPLAY=:1`。`run-eden.py` 直接使用调用者的 `DISPLAY`。
按本次迭代要求，测试改用下述虚拟桌面包装器；它复用网易项目的 Xvfb 方式，
自动申请独立显示编号，不占用其他任务的桌面。

当前 Qt 配置已使用独立的 NAND 和 SD 路径。如果以后复制其他项目的
`qt-config.ini`，需要同时修改 `[Data%20Storage]` 中的 `nand_directory`
和 `sdmc_directory`；它们可能是绝对路径，仅设置 XDG 目录不会覆盖它们。

## 准备并启动

在模拟器停止运行时，从本仓库根目录执行。先生成兼容 SDK 和完整 SD 包：

```sh
./scripts/build_eden_toolchain.sh
TVBOX_TOOLCHAIN_IMAGE=switch-tv-box/toolchain:eden-macros ./scripts/build-local.sh release
```

衍生镜像修正 deko3d 的宏上传方式，并包含 mpv HOS 音频缓冲容量修复。
旧音频输出代码对部分填充的缓冲区计算了错误的可用容量；本次修正从总容量
扣除已经写入的样本数，447 个实际函数容量组合验证通过。该音频修复也包含
在本项目标准 SDK 中，不修改网易项目的工具链。
`scripts/verify_deko_macro_upload.py` 检查 GPU 指令和入口地址等价；
构建来源和验证结果分别保存在镜像的 `/opt/switch-tv-box/eden-macros/`
与 `/opt/switch-tv-box/mpv-hos/`。
实体 Switch 的标准构建方式见 [构建指南](BUILD.md)。

使用已通过构建的宏兼容版本 NRO。启动器不会给普通旧构建自动修补 GPU 宏。

```sh
./scripts/run-eden.py --interface enp7s0 --check
python3 scripts/run-eden-virtual.py --interface enp7s0
```

包装器需要 `Xvfb`、`xauth` 和 `xdpyinfo`，使用临时认证文件并禁用 X11 TCP，
不开放 VNC 端口。启动日志会显示 `DISPLAY` 和 `XAUTHORITY`，可用于该会话的
截图与按键验证。退出时先关闭本次 Eden，再关闭自身的 Xvfb，
不修改或终止其他桌面。需要在现有桌面运行时，直接调用 `run-eden.py`。

`--interface` 必填，选择手机所在局域网的物理网卡。本机为 `enp7s0`；
IPv4 会自动从该网卡读取，也可用 `--address` 明确指定。系统需要现有 Eden、
`ip` 和 `xdpyinfo`。

启动器从 `dist/switch-tv-box-sdmc.zip` 安装中文字体、DLNA 资源和许可证，
并将 `--nro` 指定的文件（默认仓库根目录 `switch-tv-box.nro`）安装到模拟 SD。
已有 IPTV 源、设置、AirPlay 身份和配对文件保留；首次配置只从网易模拟器
配置中读取 Controls 节，不复制账号、网络令牌或原 SD 路径。

已有 Qt 设置保留，SD、NAND、load、dump、tas 和截图目录统一指向独立状态目录，
网络接口设为选定的物理网卡。`--state` 可选择其他独立目录；
`--prepare-only` 仅安装配置，不打开窗口；`--check` 只做预检，不写入状态目录。
启动器会用文件锁和进程环境检查拒绝重复启动，也能识别同一状态目录下的
手动 GDB/Eden 会话。准备配置前必须先关闭该会话。

### IPTV 物理网卡 HTTP 直连（可选）

如果宿主的透明代理 / TUN 影响直播吞吐，可显式启动本项目的 HTTP 辅助：

```sh
python3 scripts/run-eden-virtual.py --interface enp7s0 --iptv-direct-http
```

它只监听宿主 `127.0.0.1:18957`，通过指定物理网卡转发请求，不修改系统代理、
路由或其他程序。启动器等待辅助程序就绪后创建临时 `iptv-direct-http.flag`，
退出时清理；辅助程序异常退出时关闭本次 Eden，避免保留失效的代理配置。
未添加开关时使用原网络路径；`--prepare-only` 不启动后台辅助程序。

辅助程序按 Content-Length 或 chunked 结束标记及时结束响应，并向播放器明确
关闭单次请求连接，避免上游保活导致下一次请求长时间等待。媒体正文、Range
与重定向地址保持原样。可加 `--iptv-direct-http-diagnostics` 记录请求计时、
字节数、响应状态与关闭原因（最多 1,024 次请求），用于区分上游下载慢和
播放器接收慢；日志不包含 URL、令牌或媒体内容。

对于至少 256 KiB、具有强 ETag、identity 编码和有效字节范围支持的完整
媒体响应，辅助程序会在上游低于 128 KiB/s 持续 8 秒或中途断流时尝试
续传，最多增加两次连接。计时排除向模拟器发送时的等待。续传必须返回
206、相同 ETag、准确的剩余 Content-Range 和长度，才会拼接剩余正文；
拒绝 multipart、实体变化和从头重放。小清单、分块响应及无法验证的来源
不使用此恢复路径。它不能保证上游长期不可达或整体带宽不足时仍持续播放。

当前范围仅为 **IPTV 的公共 IPv4 HTTP 地址**，例如本次测试的 CCTV-3 备用源。
初始地址为域名、HTTPS、IPv6 或局域网地址时不应用该辅助。经辅助转发的地址
如果重定向到域名或 HTTPS，会明确失败；它不提供 DNS 绕过或 CONNECT 隧道。
AirPlay、DLNA 和实体 Switch 不启用这条路径。

用于排查缓存与解码时，在该测试 SD 的 `switch/switch-tv-box/` 中创建
`iptv-diagnostics.flag` 后重启。`iptv-playback.log` 每三秒记录播放位置、缓存
秒数、实际视频尺寸和两类掉帧数，最多 240 条，不记录 URL 或令牌。
删除标记并重启即可关闭。该标记与日志不进入安装包。

如果 Eden 提示缺少解密组件，对本项目自制 NRO 选择 **No** 即可。
启动器会等到其 Eden 子进程拥有 7000 端口、实际 `/info` 响应可用后，
启动 `scripts/eden-airplay-advertise.py`，在指定网卡上发布真实接收器发现记录。
音频与控制仍由 NRO 处理。退出时仅清理本次创建的子进程和发现记录。
模拟器日志写入 `artifacts/validation/`；发现服务由 NRO 自身运行。

## 已定位的 Eden GPU 缺陷

本次启动在两个可选服务调用之外，还触发了 Eden v0.2.1 的宏代码映射
引用失效问题：向映射中插入一段从现有宏中间位置开始的代码时，容器重分配
使先前保存的源引用失效，后续复制访问无效位置。这是本次本地调试确认的
模拟器故障。

Eden 官方的
[`54046ac` 宏代码处理修正](https://github.com/eden-emulator/mirror/commit/54046ac60ef5d8876b550be2546b89d48751de7e)
把这条路径改为先从源代码构造独立的 `std::vector`，再插入映射，并统一
后续处理使用的代码范围。对照该差异，本次引用失效路径由此得到规避。

本地诊断使用 `artifacts/debug/eden` 副本，
`artifacts/debug/eden-macro-fix.gdb` 在插入前保存源偏移，在插入后重新取得
源引用。`artifacts/validation/eden-macro-fix.log` 记录了实际执行的引用修正。
这些 GDB 地址只对应本次二进制及调试进程布局，不能直接用于其他版本，
也不构成通用模拟器补丁。

## 键盘操作

以下对应本机 Qt 配置，其他配置中的映射可能不同。

| 键盘 | 模拟 Switch 按键 | 应用操作 |
|---|---|---|
| 方向键 | 十字键 | 移动焦点、选择频道 |
| C | A | 确认、列表中播放选中频道；视频播放时暂停或继续 |
| X | B | 返回 |
| V | X | IPTV 播放时打开频道列表；列表中展开全屏 |
| Z | Y | 频道列表中收藏或取消收藏 |
| M | + | 退出应用 |
| W、A、S、D | 左摇杆 | 移动焦点 |

鼠标点击用于模拟触摸。进入模拟器游戏画面后再使用上述按键。
首页只有 IPTV、AirPlay、DLNA 三张卡，方向键选择后按 C 进入；
投屏说明页按 X 返回首页。IPTV 列表顶部有“全部频道”和“收藏”入口，
收藏保存到模拟 SD 的 `switch/switch-tv-box/iptv/favorites.txt`，重启后恢复。

## 显式兼容开关

程序仅在模拟 SD 卡上存在
`sdmc:/switch/switch-tv-box/eden-compat.flag` 时启用以下兼容处理：

1. 跳过 `appletSetMediaPlaybackState()` 和 `appletSetAutoSleepDisabled()`。
   Eden v0.2.1 对媒体状态 IPC 60 报未实现错误；此模式下由桌面系统管理休眠。
2. 跳过 `audaInitialize()`。Eden 不提供 `aud:a` 服务，请求该服务会阻塞；
   播放器改用已有的 mpv 音量和静音控制路径。
3. 保留 NRO 内置 mDNS 广播；兼容标记不再关闭设备发现。
   Eden 的组播 socket 选项支持不完整，因此模拟器不能替代实体 Switch 的
   手机发现验收。启动器不再通过外部公告掩盖这项模拟器限制。
4. 网络就绪检查使用即时轮询，并在应用线程中短暂休眠，避免阻塞 Eden
   数量有限的 BSD 服务线程。SSDP 直接接收有界 UDP 数据报，不依赖其未实现的
   `FIONREAD`，避免数据报始终未被消费造成高 CPU 占用。
5. 网络库请求的 `SOCK_CLOEXEC` 在本应用的模拟器进程内省略，接收/发送超时
   转为该版本 Eden 所需的 u32 毫秒格式。其他标志和选项保持原值。
6. 直接使用图形界面，不建立或恢复 libnx 文字控制台。其双缓冲区在 Eden
   切换图形界面后仍残留 `DeviceShared` 属性，后续堆复用会导致线程栈映射
   返回 `0xD401`。运行时诊断已确认释放的 `0x3C0000` 缓冲区与失败的栈页重叠。
7. 使用 FFmpeg 软件解码视频，绕过此版本模拟器中出现绿屏和残留设备共享内存的
   nvtegra 帧表面路径。图形界面与视频呈现继续使用 GPU；实体 Switch 保留硬解码。
8. DLNA 与 AirPlay 均使用 NIFM 返回的局域网地址。此行为也用于实体 Switch，
   不再通过访问公网的路由选择发现地址。

首页的 IP 地址也改为每秒刷新，避免每帧重复查询网络服务。

标记文件由本地启动器创建，不随 SD 安装包分发。默认实体 Switch
没有该文件，继续使用原有电源策略、进程音量控制和内置广播。不要把模拟器 SD 中的
标记复制到实体 Switch；程序不会用固件版本或 Atmosphère 状态猜测运行环境。

## 本次验证范围

真实 NRO 已在独立 Xvfb 虚拟桌面完成以下验收：

| 项目 | 已验证行为 |
|---|---|
| 首页 | IPTV / AirPlay / DLNA 三入口；中文、焦点与操作提示正常 |
| DLNA 发现 | 实体网卡 SSDP LOCATION 与设备描述 URLBase 都使用 `10.30.0.44:49152` |
| DLNA 视频 | 本地 HTTP 的 30 秒 H.264/AAC、640×360 测试视频正常显示彩条与时间文字 |
| DLNA 控制 | 暂停保持位置、跳转后继续；快速 Stop→Play 同 URL 重播恢复可见画面；再次停止成功 |
| IPTV | 受控两频道 M3U 播放；播放中打开列表并切到另一频道，HTTP 日志确认换用频道 2 URL |
| 收藏 | 添加收藏、进入收藏栏目、退出重启后恢复、取消收藏均通过 |
| RAOP 音频探测 | 同进程完成 DLNA 视频后，再运行约 5 秒、627 包 PCM 探测；RTSP 全部 200，UDP 错误为 0 |

软件解码解决了本次 Eden nvtegra 路径的绿屏问题；它仅在显式兼容模式生效。
DLNA 的快速停止重播还修复了旧主页请求重复发 STOP，以及同 URL 新会话没有
解除主页覆盖的问题。最终控制探测结果为 `passed: true`，重播截图可见
2.133 秒的视频时间文字。

主要本地证据：

- `artifacts/validation/dlna-ssdp-fixed-address.log`
- `artifacts/validation/dlna-video-software-decoder.log`
- `artifacts/validation/dlna-video-controls-final.log`
- `artifacts/debug/dlna-controls-20261020-190356/eden-replay-video.png`
- `artifacts/validation/iptv-live-fixture.log`
- `artifacts/validation/eden-iptv-playing-list.png`
- `artifacts/validation/eden-iptv-favorite-added.png`
- `artifacts/validation/eden-iptv-favorite-after-restart.png`
- `artifacts/validation/eden-iptv-favorite-removed.png`
- `artifacts/validation/raop-pcm-probe-fixed-sdk.log`
- `artifacts/validation/raop-pcm-after-video-final.log`
- `artifacts/validation/eden-final-playback-errors.log`（0 字节）

上述 IPTV 切台与收藏验收使用受控的本地两频道来源，不代表全部在线源可用。
在线 CCTV-3 的后续大缓冲测试单独记录在 [验证记录](VALIDATION.md)。
21 个宿主界面预览覆盖中英文说明、配对、离线、播放状态、频道库、收藏和
播放设置；预览使用样本数据，与上述真实 NRO 截图分别保存。

用户此前确认 iPhone 已能发现并投送音频，但当时声音严重断续。修复 SDK 音频
容量、线程栈映射和网络等待后，前期构建的同进程视频→音频探测没有再出现线程
创建、音频写入或 NvMap 残留错误。该次会话未主动访问外部直播源。
**手机端听感与持续播放流畅度尚未重新验收**。本次也未完成 AirPlay
照片、镜像及物理 Switch 的端到端验收。

最终构建校验值与各项证据见 [验证记录](VALIDATION.md)，协议范围及实体设备
待验收项见 [AirPlay 1 能力说明](AIRPLAY1.md)。
