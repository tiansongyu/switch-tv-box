# DLNA 视频投屏

Switch TV Box 接收手机应用发送的视频地址，再由 Switch 播放。支持
`SetAVTransportURI`、播放、暂停、停止、状态/进度查询，以及可定位视频的时间跳转。
这属于 DLNA 视频投送；手机屏幕镜像使用 AirPlay。

**当前验收状态：虚拟桌面中的实际 NRO 已通过受控视频播放与控制验证。**
Eden 播放了 30 秒 H.264/AAC MP4，实际画面显示彩条与时钟，进度完整走通。
暂停、跳转、继续、停止后直接重播均已实测。Eden 使用软件解码绕过其绿屏路径；
物理 Switch 的硬解码、电视声音及各手机投屏应用仍需进一步验收。

## 手机怎么投视频

1. 将手机和 Switch 接入同一局域网，打开 Switch TV Box 的 **DLNA** 页面。
2. 在支持 **DLNA/电视投屏** 的手机视频应用中打开视频，点击播放器的投屏按钮。
3. 选择 **Switch TV Box**。视频地址由手机应用发送，播放器随后在电视端取流。
4. 使用手机播放器控制播放、暂停和进度。换视频时重新选择影片并投送；退出时点停止。

接收地址支持 HTTP、HTTPS 和 RTSP。手机本地视频需要投屏应用提供局域网
HTTP 地址；仅发送手机自身的文件路径无法播放。带临时签名的地址会保持原样，
但必须仍在有效期内，且接收端能够直接访问。需要登录 Cookie、专用鉴权或 DRM
许可证的视频，不保证可通过普通 DLNA 播放。

找不到设备时先检查同网段、路由器访客网络/客户端隔离，以及接收程序是否运行。
DLNA 使用 UDP 1900 发现设备、TCP 49152 提供描述与控制；只验证控制接口返回
200，不能证明手机能够发现设备。Eden 的物理网卡配置见
[模拟器指南](EMULATOR.md)。

## 本轮参考和修复

v0.1.2 的发现修复直接位于 NRO 中：使用 NIFM 当前局域网地址生成 LOCATION
和 URLBase，在同一接口加入 `239.255.255.250` 并发送公告；启动后间隔一秒重复
公告，之后每 30 秒刷新，接收其他 SSDP 流量不会阻止刷新。AirPlay 同样保留内置
mDNS，使用主机局域网地址，启动时重复公告。没有新增电脑端组件。

参考本地 `~/github/wiliwili`，版本
[`88e5876bea9502d06f46a8656e3530684d3aaf7d`](https://github.com/xfangfang/wiliwili/tree/88e5876bea9502d06f46a8656e3530684d3aaf7d)：

- `wiliwili/source/activity/dlna_activity.cpp`：接收 CurrentURI 后重置并加载播放器，
  将播放、暂停、跳转连接到 mpv，并用播放器事件更新状态、时长和进度。
- `wiliwili/source/api/dlna.cpp`：发送 M-SEARCH、读取 LOCATION 和设备描述，
  根据 AVTransport 的 controlURL 发送视频地址。
- `library/libpdr` 固定版本
  [`9dd3ab920940adac013a3cf40e3a5805d7e193e1`](https://github.com/xfangfang/libpdr/tree/9dd3ab920940adac013a3cf40e3a5805d7e193e1)：
  `src/protocol.cpp` 的接收动作、`src/soap.cpp` 的服务路径、
  `src/ssdp.cpp` 的发现应答和公告。该子模块源码在独立临时目录读取。

本项目保留现有 DLNA 和播放器架构，修复了以下具体问题：

- 正确读取 CDATA、自闭合空元素和数字 XML 实体，避免破坏签名 URL 或中文元数据。
- DLNA 停止并释放播放器后，再次播放先重新打开保存的视频，避免播放其他协议留下的资源。
- 移除停止后的重复停播通知，防止主界面的延迟请求误停紧接着开始的新视频。
  停止后由已有的播放器视图逻辑自动返回主页。
- 换片清空旧时长与进度；空 URL 清空 DLNA 媒体，不打断其他协议正在播放的内容。
- 保留毫秒跳转精度；拒绝无效 InstanceID、播放速度、URI 和跳转参数。
- Eden 使用所选物理网卡的 NIFM 地址生成 SSDP LOCATION 和设备描述 URLBase，
  避免宣传代理网卡地址。实体 Switch 保留原有路由探测。

## 验证边界

本轮通过的宿主测试：

```sh
python3 scripts/test_dlna_avtransport.py
python3 scripts/test_dlna_address.py
python3 scripts/test_ssdp_receive.py
```

分别覆盖视频控制与资源交接、签名 URL/空媒体/错误响应，Eden 与实体模式的
地址选择，以及真实 UDP 接收和零长包清理。宿主测试使用受控播放器替身，
不能证明实际解码、GPU 画面和音频输出正确。

另在虚拟桌面运行完整 NRO，验证实体网卡的 M-SEARCH 应答、描述地址、
实际彩条视频，以及 Pause → Seek 12.5 秒 → Play → Stop → 同 URL Play。
重播截图显示从头开始的视频，进度再次增长；完整结果与截图保存在本地
`artifacts/validation/` 和 `artifacts/debug/dlna-controls-20261020-190356/`。

接下来仍需在物理 Switch 和手机组合上验证：设备发现、可见视频画面与声音、
长时间播放、暂停恢复、跳转、连续换片，以及 AirPlay/IPTV 与 DLNA 之间切换。
定位能力取决于视频和服务器；直播或不可定位的资源不保证支持 Seek。
