# 贱驴 RS2 3.0 QMK/VIA

[English](README.en.md)

| 项目 | 规格 |
| --- | --- |
| 型号 | 贱驴 RS2 3.0 三模带屏版 |
| 主控 | Sonix SN32F299F（QMK 构建目标） |
| 无线芯片 | 沁恒 CH582F，支持蓝牙和 2.4 GHz 接收器 |
| 有线连接 | USB，支持 VIA 四层改键 |
| 屏幕 | GC9107 驱动，240 × 135 彩色 TFT LCD |
| 键数 | 98 键，另有 1 个可按压旋钮 |
| RGB | 逐键 RGB 背光 |

## 构建

在 Ubuntu 22.04 或 WSL2 的仓库根目录运行。

```bash
sudo apt-get update
sudo apt-get install -y git make gcc gcc-arm-none-eabi libnewlib-arm-none-eabi libhidapi-hidraw0 python3-venv
bash scripts/build.sh via
```

## 刷机

1. 用 USB 数据线连接 RS2 3.0，切到有线模式，先备份 VIA 配置。
2. 在本项目 QMK 固件上，按 `Fn+Esc` 或断电后按住 `Esc` 再插线，进入 ISP（`0C45:7140`）。
3. 确认只有 RS2 3.0 处于 ISP 模式，在 Windows 的仓库根目录运行。

```powershell
py -m pip install hidapi
py scripts/flash.py
```

刷入本地构建的镜像时，运行 `py scripts/flash.py dist/RS2-via.bin --checksums dist/RS2-via.bin.sha256`。恢复原厂固件请使用原厂工具。

刷入后，用有线连接在 VIA 的 Design 页导入 [RS2-VIA-0.3.0.json](firmware/RS2-VIA-0.3.0.json)。
