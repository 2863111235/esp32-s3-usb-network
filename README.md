# ESP32-S3 USB Network

两套使用 Arduino CLI 构建的 ESP32-S3 USB NCM 网络固件。

| 固件 | 网络方向 | 状态 |
| --- | --- | --- |
| `sketches/esp32_usb_hotspot` | 电脑有线网络 → Windows ICS → USB → S3 Wi-Fi 热点 | Windows 11 + S3 N16R8 + 手机 HTTP/HTTPS 已验证 |
| `sketches/esp32_usb_wifi_adapter` | 路由器/手机 Wi-Fi → S3 → USB → 电脑 | 已烧录；USB DHCP、配网、DNS、HTTP/HTTPS 和重启重连已验证 |

## 已验证的热点版本

Wi-Fi 名称 `ESP32-S3-PC`，密码 `S3Net8264`，2.4GHz、WPA2、信道 6。电脑必须联网并保持开机，开发板插原生 USB 口（D+ GPIO20 / D- GPIO19）。使用 Windows ICS 提供 DHCP、DNS 和 NAT；S3 转发原始以太网帧。

已在 ESP32-S3 N16R8（16MB flash、8MB OPI PSRAM）、Arduino-ESP32 3.3.12、Windows 11 build 26200 上验证。手机取得 `192.168.137.28`，关闭移动数据后 HTTP/HTTPS 均能打开；1400 字节 ping 15/15 成功；持续收发超过 20000 帧。属于短时功能验证，未完成长期压力测试。

`firmware/hotspot/` 是实际烧录并通过上述测试的四个二进制文件，SHA256 见目录中的清单。默认 USB MAC 为 `02-A3-53-19-00-01`，只应在同一主机连接一块使用该默认值的板子；跨设备发布请为每块板生成独立标识。源代码没有上游 Wi-Fi 账号密码。

## 编译与烧录

安装 Arduino CLI 及 ESP32 core 3.3.12。独立 USB 栈要求关闭 Arduino CDCOnBoot：

```powershell
$board = 'esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=default,FlashSize=16M,PartitionScheme=default,PSRAM=opi,UploadSpeed=921600'
arduino-cli compile --fqbn $board --libraries ./libraries --build-path "$env:TEMP/esp32-usb-hotspot-build" ./sketches/esp32_usb_hotspot
arduino-cli upload --fqbn $board --port COM7 --input-dir "$env:TEMP/esp32-usb-hotspot-build" ./sketches/esp32_usb_hotspot
```

Windows 编译输出路径需为纯英文。CLI 可额外传 `--config-file` 使用自有配置。开发板当前运行时可向 CDC 串口发送 `b` 进入下载模式；也可按住 BOOT、点按 RST、松开 BOOT。上传后若未自动运行，点按 RST。`s` 查看状态，`r` 重启。串口和网卡名称以当前系统枚举为准。

## Windows 热点共享

管理员 PowerShell 中，先运行 `tools/configure_usb_adapter.ps1` 关闭 S3 USB 网卡 checksum/LSO/RSC 卸载；然后运行 `tools/share_network.ps1`。脚本仅匹配 S3 的 USB MAC，拒绝覆盖其他活动 ICS；保存本机基线 JSON。

`share_network.ps1` 当前默认采用已验证工作站的有线接口 8（Realtek PCIe GbE），其他电脑应按参数指定真实有线接口。`share_network.ps1 -Disable` 关闭本方案共享；`configure_usb_adapter.ps1 -Restore` 恢复卸载设置。脚本需要管理员权限，USB 网卡重启期间连接会短暂中断。

## 复用与来源

2026-10-19 调研结论：复用 [EspUsbDevice 2.6.1](https://github.com/tanakamasayuki/EspUsbDevice/tree/954249a26d12be278d8a7a06828e7495ea316732)（MIT）提供独立 TinyUSB DMA 栈、NCM 描述符和发送队列，固定源码放在 `libraries/`。原 Arduino 预编译 USB 栈曾在 217 帧后停滞；替换后完成手机测试。Windows NCM 参数兼容修复和 DMA 原因见该库 `third_party/tinyusb/PROVENANCE.md` 与 `src/internal/EspUsbTinyUsbConfig.h`。

热点原始帧转发参考 [Espressif tusb_ncm 示例](https://github.com/espressif/esp-idf/tree/master/examples/peripherals/usb/device/tusb_ncm)（CC0-1.0 / Unlicense）。[官方 USB 文档](https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_device.html)确认 S3 原生 USB Full-Speed 及 NCM 使用方式。

网卡方向参考 [MQM-M/esp32s3-usb-ncm-router](https://github.com/MQM-M/esp32s3-usb-ncm-router)（源文件 CC0-1.0 / Unlicense）的 USB DHCP + Wi-Fi STA + NAPT 设计；其完整工程使用 ESP-IDF，因此本项目组合已有 Arduino USB 库实现。另评估 [DrWhax/esp32-usb-wifi](https://github.com/DrWhax/esp32-usb-wifi)，它也是 ESP-IDF 工程，使用原始 L2 转发及串口配网。

## 文件与隐私

仓库保留源码、依赖及其许可、经过校验的固件和工具。不包含原始 Flash 备份、本机共享状态、Wi-Fi 密码、抓包或临时构建产物。原始备份保留在本地原工程，未复制到此项目。Wi-Fi 网卡模式的凭据应运行时配置并存入板端 NVS，不能编译进固件或提交 Git。

本项目自有代码 MIT；第三方文件沿用各自许可证，详见 `THIRD_PARTY.md`。

## Wi-Fi USB 网卡模式

1. 关闭旧热点 ICS（原工作站可用原工程 `share_network.ps1 -Disable`）。
2. 使用 `tools/build.ps1 -Mode wifi` 编译；加 `-Upload -Port COM7` 烧录，下载端口按实际枚举确认。
3. 管理员 PowerShell 运行 `tools/prepare_wifi_adapter.ps1`，将 S3 USB 网卡切回 DHCP。此模式不应开启 Windows ICS。
4. 打开 <http://192.168.7.1/>，扫描/输入 2.4 GHz Wi-Fi 名称及密码，保存并连接。
5. 连接成功后运行 `tools/prepare_wifi_adapter.ps1 -Renew` 刷新 DHCP DNS；也可重新插拔 USB。有线网络仍存在时，Windows 可能继续优先使用有线，测试时需要指定 USB 路由或临时拔掉有线。

电脑识别为 USB 以太网卡，Wi-Fi 通过设备配置页选择，不使用 Windows WLAN 菜单。USB 地址 `192.168.7.1/24`，电脑 DHCP 如 `192.168.7.2`；S3 连接上游后使用 NAPT 转发 IPv4。默认 DHCP DNS `223.5.5.5`，连接后改为广告上游 DNS，客户端需要刷新租约。上游不能与 USB 的 `192.168.7.0/24` 网段冲突。支持常用个人 Wi-Fi 和开放网络，没有企业认证或校园网网页登录流程。

凭据运行时保存在板端 NVS，不回传密码、不写进源码或编译固件。配置服务绑定 USB 地址并检查本地子网，写接口需要每次启动随机生成的页面 token。Wi-Fi 扫描和自动重连使用 Arduino WiFi 库，NAT 使用 core 内置 `esp_netif_napt_enable`。`firmware/wifi/` 不含配网后的 NVS。

实机验证（2026-10-19）：Arduino CLI 编译通过（949870 字节程序、84796 字节全局内存），烧录校验成功；电脑 DHCP 取得 `192.168.7.2`、网关 `192.168.7.1`，续租取得上游 DNS。配置页和扫描正常，缺少 token 返回 403、空 SSID 返回 400，页面脚本语法通过。用户在 USB 页面配置真实 Wi-Fi 后，S3 STA 取得地址、NAPT 启动；通过临时单主机路由将 DNS 和百度网页明确指向 USB 接口 39，DNS 解析成功、HTTP 和 HTTPS 均返回 200（没有借用电脑有线默认出口）。测试路由已移除。S3 重启后自动重连并恢复 NAPT，凭据无需重新输入。尚未做长期压力或吞吐上限测试。

补充实测（2026-10-19）：通过临时主机路由和源地址绑定，确认测试流量经过 S3 USB 网卡。有线默认出口未参与这些测试。百度首页连续 5 次 HTTPS 均返回 200，耗时 0.12～0.16 秒；哔哩哔哩首页返回 200，约 0.58 秒。华为云镜像文件连续 3 次各下载 2 MiB，均返回 206，平均下载速率分别为 719208、820736、705955 字节/秒，约 5.6～6.6 Mbps。公网 1400 字节 ping 15/15 成功，延迟 24～54 ms、平均 34 ms；USB 网关同样 15/15 成功，延迟 3～4 ms。测试期间 USB 网卡报告的收发错误和丢弃增量均为 0，结束时 Wi-Fi 与 NAPT 仍正常。腾讯首页的两个解析地址在 S3 和有线对照中均连接超时。以上属于短时测试结果，不代表吞吐上限或长期稳定性；临时路由已清理。

S3 仅支持 2.4 GHz，原生 USB Full-Speed 理论总线速率 12Mbps，实际转发速率会更低。
