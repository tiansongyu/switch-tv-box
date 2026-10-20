# switch-tv-box v0.1.0 Release Notes

Switch TV Box 基于 NX-Cast 0.3.2，首页提供 IPTV、AirPlay、DLNA 三个入口，
支持中文、手柄与触摸操作。

- IPTV 播放中切台、收藏、分类、搜索和 EPG；新增持久化画质与缓冲设置。
- 起播缓冲 6／30／60 秒，默认 60 秒；后台预读目标最多 180 秒，压缩媒体包缓存上限
  100 MiB（含回看，不含解码与渲染内存）。画质按片源提供的 HLS 版本选择，单一码率源不进行转码。
- DLNA 视频地址投屏与播放控制；AirPlay 1 音频、照片、URL/HLS 视频及原有配对镜像。

本次发布程序在隔离虚拟桌面的 Eden 中实播 **CCTV-3（1280×720）5 分 9 秒**：
起播后没有再次缓冲、播放跳时或新增掉帧，运行错误日志为空。首次起播约等待
72 秒。数字采样与验收结果位于 `source/docs/validation/`；安装包内的 NRO
与实测版本按 SHA-256 绑定。

下载 `switch-tv-box-sdmc.zip` 后解压到 Switch SD 卡根目录，以完整应用模式
启动 `switch/switch-tv-box/switch-tv-box.nro`。完整归档
`switch-tv-box-v0.1.0-complete.zip` 同时提供 `sdmc/` 安装文件、`source/`
已提交源码和离线 AirPlay 规范，以及构建标识与校验清单。

较大缓冲会增加首次播放、切台等待和直播延迟。已知部分预设直播源在本次网络中
无法连接，播放器无法修复上游服务；请在“直播源”中选择可用来源。
Eden 测试环境的可选物理网卡 HTTP 辅助仅处理公共 IPv4 的 HTTP 流，
不改变实体 Switch、AirPlay、DLNA 或系统代理设置。

验证范围、实际媒体播放记录和安装包校验值见 `source/docs/VALIDATION.md`。
模拟器验证不等于物理 Switch 及全部 iOS/macOS 设备的互操作验收。
老式 `/stream` 镜像、幻灯片主题、歌曲封面和 AirPlay 2 多房间仍未实现，
详见 `source/docs/AIRPLAY1.md`。
