# 001-P1 Live Image 与显示模式扩展

本目录包含 Live Image 与显示模式扩展的需求、交付和刷机文档。

## 实机硬件结论

当前实机：

```text
ELEKSTUBE IPS TIMEMACHINE
ESP32-PICO-D4 revision v1.0
PlatformIO env: elekstubev2
```

默认构建和刷机：

```bash
export ENV=elekstubev2
export PORT=/dev/cu.usbserial-110
```

不要根据“四个按钮”推断为 `elekstubev1`。judge2005 原版 `elekstubev2` 已实测可正常恢复。

## 文档入口

1. `prd.md`：需求正文。
2. `dev-delivery-flashing.md`：原始开发、交付、刷机说明。
3. `hardware-target-and-flashing-notes.md`：本机硬件判定、`elekstubev2` 刷机命令和擦除注意事项。

如文档存在冲突，以 `hardware-target-and-flashing-notes.md` 的实机结论为准。
