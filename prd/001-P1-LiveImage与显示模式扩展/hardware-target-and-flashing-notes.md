# 硬件目标与刷机备忘

- 需求编号：001
- 需求名称：Live Image 与显示模式扩展
- 文档版本：v0.1
- 更新日期：2026-06-29

## 1. 已确认硬件结论

当前实机目标为：

```text
设备外观：ELEKSTUBE IPS TIMEMACHINE
芯片：ESP32-PICO-D4 revision v1.0
PlatformIO 环境：elekstubev2
串口示例：/dev/cu.usbserial-110
```

结论：

1. 不要根据“四个按钮”推断为 `elekstubev1`。
2. 本设备应使用 `elekstubev2` 构建和刷机。
3. `elekstubev1` 固件在该设备上可能导致启动后反复 watchdog reset。
4. judge2005 原版 `elekstubev2` 已实测可刷回并正常启动。
5. 后续本需求的默认构建环境应为 `elekstubev2`。

推荐默认环境：

```bash
export ENV=elekstubev2
export PORT=/dev/cu.usbserial-110
```

如串口不同，先执行：

```bash
ls /dev/cu.*
```

## 2. 构建命令

```bash
cd ~/src/EleksTubeIPS
git checkout 001-live-image
git pull

export ENV=elekstubev2

uvx --from platformio pio run -e "$ENV"
uvx --from platformio pio run -e "$ENV" -t buildfs
```

预期产物：

```text
.pio/build/elekstubev2/firmware.bin
.pio/build/elekstubev2/littlefs.bin
```

## 3. 增量刷机命令

本需求包含 Web UI 修改，因此要同时刷：

```text
0x10000 firmware.bin
0x180000 littlefs.bin
```

本机实测 `write-flash` 使用 stub 时可能在 0% 写入阶段断响应；`--no-stub` 较慢但可稳定写入并通过 hash 校验。因此推荐：

```bash
export ENV=elekstubev2
export PORT=/dev/cu.usbserial-110

uvx --from esptool esptool \
  --chip esp32 \
  --port "$PORT" \
  --baud 57600 \
  --before default-reset \
  --after hard-reset \
  --no-stub \
  write-flash \
  --flash-mode dio \
  --flash-freq 40m \
  --flash-size 4MB \
  0x10000 ".pio/build/$ENV/firmware.bin" \
  0x180000 ".pio/build/$ENV/littlefs.bin"
```

成功标志：

```text
Hash of data verified.
Hash of data verified.
```

## 4. 擦除注意事项

不要执行：

```bash
uvx --from esptool esptool ... --no-stub erase-flash
```

原因：ESP32 ROM bootloader 不支持整片 `erase-flash`，会报：

```text
ESP32 ROM does not support function erase_flash
```

可选方案：

### 方案 A：使用 stub 整片擦除

```bash
uvx --from esptool esptool \
  --chip esp32 \
  --port "$PORT" \
  --baud 57600 \
  --before default-reset \
  --after no-reset \
  erase-flash
```

### 方案 B：使用 ROM 支持的 region 擦除 4MB

```bash
uvx --from esptool esptool \
  --chip esp32 \
  --port "$PORT" \
  --baud 57600 \
  --before default-reset \
  --after no-reset \
  --no-stub \
  erase-region 0x0 0x400000
```

## 5. 完整刷机命令

仅在需要完整恢复或分区变化时使用。

```bash
cd ~/src/EleksTubeIPS
export ENV=elekstubev2
export PORT=/dev/cu.usbserial-110

BOOT_APP0="$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"

uvx --from esptool esptool \
  --chip esp32 \
  --port "$PORT" \
  --baud 57600 \
  --before default-reset \
  --after hard-reset \
  --no-stub \
  write-flash \
  --flash-mode dio \
  --flash-freq 40m \
  --flash-size 4MB \
  0x1000 ".pio/build/$ENV/bootloader.bin" \
  0x8000 ".pio/build/$ENV/partitions.bin" \
  0xe000 "$BOOT_APP0" \
  0x10000 ".pio/build/$ENV/firmware.bin" \
  0x180000 ".pio/build/$ENV/littlefs.bin"
```

## 6. Watchdog 重启判断

如果串口反复出现：

```text
rst:0x8 (TG1WDT_SYS_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
```

说明设备能从 flash 启动，但 app 启动阶段卡住后被 watchdog 重启。

当前设备上，如果出现这种情况，第一优先检查：

```text
是否误刷了 elekstubev1 构建产物。
```

## 7. 回滚到 judge2005 原版

使用 judge2005 原版时也应选择：

```bash
export ENV=elekstubev2
```

不要使用 `elekstubev1`。

## 8. 当前定制固件边界

为给 Live Image 功能和后续扩展保留空间，当前分支已裁剪：

移除：

1. Weather / OpenWeatherMap。
2. MQTT / Home Assistant 状态发布。
3. Matrix screen saver 页面和运行入口。

保留：

1. Wi-Fi 配网。
2. NTP / RTC 时间。
3. Web UI。
4. Clock / Date / Slideshow。
5. Clock face / slideshow 文件上传。
6. Live Images API + UI。
7. LED 背光配置。

因此当前交付物不是 judge2005 全功能固件，而是 Live Image 定制固件。
