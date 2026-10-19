# ESP32-S3 USB Network

两套使用 Arduino CLI 构建的 ESP32-S3 USB NCM 网络固件。

| 固件 | 网络方向 | 状态 |
| --- | --- | --- |
| `sketches/esp32_usb_hotspot` | 电脑有线网络 → Windows ICS → USB → S3 Wi-Fi 热点 | Windows 11 + S3 N16R8 + 手机 HTTP/HTTPS 已验证 |
| `sketches/esp32_usb_wifi_adapter` | 路由器/手机 Wi-Fi → S3 → USB → 电脑 | 开发中 |

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
