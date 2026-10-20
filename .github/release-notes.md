# switch-tv-box v0.1.2 Release Notes

修复 NRO 内置的 AirPlay / DLNA 自动发现。手机与运行 NRO 的 Switch
连接同一局域网后，由 NRO 自行发送发现公告、响应搜索，无需电脑端发现程序。

- AirPlay 的内置 mDNS 不再被 `eden-compat.flag` 关闭。
- AirPlay、DLNA LOCATION 与设备描述 URLBase 使用 NIFM 当前局域网地址，
  不再依赖到公网地址的路由探测；局域网无外网出口时也可选择正确的本机地址。
- DLNA 在公布地址对应的接口加入 SSDP 组播、发送公告，检查关键 socket
  选项的错误；启动后一秒重复公告，持续收到其他设备流量时仍按时刷新公告。
- AirPlay 启动后一秒重复 `_airplay._tcp` 和 `_raop._tcp` 公告。
- 本地调试启动器不再自动启动外部 AirPlay 发现助手。

下载 `switch-tv-box.nro` 替换原 NRO；首次安装建议下载
`switch-tv-box-sdmc.zip` 解压到 SD 卡根目录。完整包
`switch-tv-box-v0.1.2-complete.zip` 包含安装文件、已提交源码、离线协议规范
和构建清单。保留个人频道、收藏及配对文件，使用完整应用模式启动。
实体 Switch 不需要 `eden-compat.flag`。

验证包括完整构建回归、真实 UDP 组播收发测试、mDNS 启动重复公告，
以及隔离虚拟桌面中实际 NRO 的图形界面、AirPlay/RAOP 查询应答、
DLNA 设备描述、六类 SSDP 单播应答、视频播放/暂停/跳转/继续/停止/重播。
记录见 `docs/validation/native-discovery-20261021.json`。

尚未完成实体 Switch 与手机的端到端发现验收。Eden 0.2.1 未完整实现组播
socket 选项，本机测试中 NRO 的 SSDP 单播与视频控制正常，SSDP 组播搜索
没有收到该 NRO 的应答；因此不能把模拟器测试视作实机组播已验证。
这版不新增外部发现组件，也不宣称修复了模拟器内部的网络实现。
原有 IPTV 备用线路、大缓冲与视频能力保留，片源可用性仍取决于源服务。
