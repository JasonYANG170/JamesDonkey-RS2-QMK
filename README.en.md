# Jamesdonkey RS2 3.0 QMK/VIA

[简体中文](README.md)

QMK firmware with VIA support for the tri-mode Jamesdonkey RS2 3.0 with a built-in display.

| Component | Specification |
| --- | --- |
| Main MCU | Sonix SN32F299F (QMK build target) |
| Wireless | WCH CH582F, with Bluetooth and a 2.4 GHz receiver |
| Wired connection | USB with four VIA layers |
| Display | 240 × 135 color TFT LCD with a GC9107 driver |
| Controls | 98 keys and one clickable encoder |
| Lighting | Per-key RGB backlighting |

## Firmware downloads

Download the firmware and its matching VIA layout from [Releases](https://github.com/JasonYANG170/JamesDonkey-RS2-QMK/releases).

## Build from source

Run these commands from the repository root on Ubuntu 22.04 or WSL2.

```bash
sudo apt-get update
sudo apt-get install -y git make gcc gcc-arm-none-eabi libnewlib-arm-none-eabi libhidapi-hidraw0 python3-venv
bash scripts/build.sh via
```

## Flash on Windows

1. Connect the RS2 3.0 with a USB data cable, switch to wired mode, and back up your VIA configuration.
2. On this QMK firmware, press `Fn+Esc` or power off and hold `Esc` while reconnecting to enter ISP (`0C45:7140`).
3. Keep only the RS2 3.0 in ISP mode and run these commands from the repository root.

   ```powershell
   py -m pip install hidapi
   py scripts/flash.py
   ```

To flash a locally built image, run the following command.

```powershell
py scripts/flash.py dist/RS2-via.bin --checksums dist/RS2-via.bin.sha256
```

Use the factory tool to restore stock firmware. After flashing QMK, connect in wired mode and import [RS2-VIA-0.3.0.json](firmware/RS2-VIA-0.3.0.json) in VIA's Design tab.
