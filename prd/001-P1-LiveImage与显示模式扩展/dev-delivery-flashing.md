# 开发环境、交付与刷机说明

- 需求编号：001
- 需求名称：Live Image 与显示模式扩展
- 文档版本：v0.4
- 更新日期：2026-06-27
- 目标：基于 judge2005/EleksTubeIPS 修改固件与 Web UI，并交付可刷入的 `firmware.bin` 与 `littlefs.bin`

## 1. 总体交付策略

本需求包含 Web UI 修改，因此不能只刷应用固件。

交付包至少应包含：

```text
firmware.bin        # 应用固件，写入 0x10000
littlefs.bin        # Web UI / 静态资源 / 文件系统镜像，写入 0x180000
SHA256SUMS.txt
RELEASE_NOTES.md
```

如果需要完整安装包，可额外包含：

```text
bootloader.bin      # 写入 0x1000
partitions.bin      # 写入 0x8000
boot_app0.bin       # 写入 0xe000
```

普通用户已经刷过 judge2005/EleksTubeIPS 时，优先使用“增量刷机”：

```text
0x10000 firmware.bin
0x180000 littlefs.bin
```

注意：写入 `littlefs.bin` 会覆盖 LittleFS 分区。应按“可能清空原有 Web UI、上传过的 clock face、slideshow、live image”等文件系统内容处理。刷机前必须保留完整 flash 备份。

## 2. 开发环境

以下以 macOS + uv/uvx 为准。

### 2.1 安装基础工具

```bash
# uv
curl -LsSf https://astral.sh/uv/install.sh | sh
exec $SHELL -l
uv --version

# git / node 可通过 Homebrew 安装
brew install git node
```

Node 仅在 Web UI 有前端构建流程时需要。如果上游 Web UI 是直接放在 LittleFS 数据目录中的静态文件，可以不跑 Node 构建。

### 2.2 克隆代码

```bash
mkdir -p ~/src
cd ~/src

git clone https://github.com/judge2005/EleksTubeIPS.git
cd EleksTubeIPS
```

建议从当前设备正在使用的版本 tag 或 commit 建分支，避免固件与文件系统资产版本漂移。

示例：

```bash
git checkout -b feature/live-image-slots
```

如果明确基于某个 tag：

```bash
git fetch --tags
git checkout v1.9.5
git checkout -b feature/live-image-slots-v1.9.5
```

## 3. 代码修改范围

## 3.1 固件侧

可能涉及：

```text
src/IPSClock.h
src/IPSClock.cpp
src/TFTs.h
src/TFTs.cpp
src/main.cpp
其他配置 / WebSocket / HTTP handler 相关文件
```

需要实现：

1. Live slot 文件路径：`/ips/live/{0..5}.bmp`。
2. 设备启动时创建 `/ips/live`。
3. HTTP API：
   - `GET /api/live/slots`
   - `PUT/POST /api/live/slots/{slot}/image`
   - `DELETE /api/live/slots/{slot}/image`
   - `GET /api/live/slots/{slot}/image`
   - `POST /api/display/preset`
   - `GET /api/display/preset`
4. 显示枚举扩展：
   - `FOUR_WITH_TWO_LIVE_IMAGES`
   - `LIVE_IMAGES`
5. 渲染逻辑：
   - `HHMM_WITH_TWO_LIVE_IMAGES`
   - `SIX_LIVE_IMAGES`
6. 单 slot dirty/version 机制。
7. 上传临时文件 + 校验 + 原子替换。
8. 错误处理和 JSON 响应。

## 3.2 Web UI 侧

必须实现：

1. Live Images 页面或 Files 页面中的 Live Images tab。
2. slot 0 到 slot 5 的预览、上传、清空。
3. 显示模式选择：
   - `HHMM + 2 live images`
   - `6 live images`
4. 上传进度和错误提示。
5. 调用新增 HTTP API。
6. 与原有 Time / Date / Weather / Slideshow 切换兼容。

Web UI 最终必须被打进 `littlefs.bin`。

## 4. 构建

## 4.1 选择硬件环境

EleksTube IPS V1：

```bash
ENV=elekstubev1
```

EleksTube IPS V2：

```bash
ENV=elekstubev2
```

如果不确定 V1/V2，不要刷。先确认设备类型。

## 4.2 构建固件

```bash
cd ~/src/EleksTubeIPS

uvx --from platformio pio run -e "$ENV"
```

成功后预期产物：

```text
.pio/build/$ENV/firmware.bin
```

## 4.3 构建 Web UI / LittleFS

先确认 repo 的 Web UI 资产位置和构建方式。

如果存在前端构建流程，例如 `package.json`：

```bash
# 具体命令以 repo 中 package.json 为准
npm install
npm run build
```

如果 Web UI 是静态文件，直接修改对应 LittleFS 数据目录中的文件。

然后构建文件系统镜像：

```bash
uvx --from platformio pio run -e "$ENV" -t buildfs
```

成功后预期产物通常为：

```text
.pio/build/$ENV/littlefs.bin
```

如果文件名或位置不同，以 PlatformIO 输出为准：

```bash
find .pio/build/$ENV -maxdepth 2 -type f \( -name '*littlefs*.bin' -o -name 'spiffs.bin' \) -print
```

本项目目标是 LittleFS，因此交付物统一命名为：

```text
littlefs.bin
```

## 4.4 可选：完整安装产物

如需完整安装包，还应准备：

```text
bootloader.bin
partitions.bin
boot_app0.bin
```

这些文件的来源可以是项目构建产物或对应 release assets。完整安装地址见刷机章节。

## 5. 本地功能测试

## 5.1 静态检查

```bash
uvx --from platformio pio run -e "$ENV"
uvx --from platformio pio run -e "$ENV" -t buildfs
```

## 5.2 产物校验

```bash
mkdir -p ~/elekstube-delivery/001-P1-live-image
cp ".pio/build/$ENV/firmware.bin" ~/elekstube-delivery/001-P1-live-image/firmware.bin
cp ".pio/build/$ENV/littlefs.bin" ~/elekstube-delivery/001-P1-live-image/littlefs.bin

cd ~/elekstube-delivery/001-P1-live-image
shasum -a 256 firmware.bin littlefs.bin > SHA256SUMS.txt
ls -lh firmware.bin littlefs.bin SHA256SUMS.txt
```

## 6. 刷机前备份

你已经有原厂备份时，仍建议刷定制固件前再备份当前 judge2005 状态。

```bash
mkdir -p ~/elekstube-backup
cd ~/elekstube-backup

export PORT=/dev/cu.usbserial-120

uvx --from esptool esptool \
  --chip esp32 \
  --port "$PORT" \
  --baud 57600 \
  --after no-reset \
  read-flash 0x0 0x400000 elekstube-before-live-image-a.bin

uvx --from esptool esptool \
  --chip esp32 \
  --port "$PORT" \
  --baud 57600 \
  --after no-reset \
  read-flash 0x0 0x400000 elekstube-before-live-image-b.bin

cmp elekstube-before-live-image-a.bin elekstube-before-live-image-b.bin && echo "backup OK"
shasum -a 256 elekstube-before-live-image-a.bin elekstube-before-live-image-b.bin > elekstube-before-live-image.sha256
chmod 444 elekstube-before-live-image-a.bin elekstube-before-live-image-b.bin elekstube-before-live-image.sha256
```

你的设备之前在高波特率下读取不稳定，因此刷机建议继续使用：

```text
--baud 57600
```

## 7. 增量刷机：固件 + Web UI

适用场景：设备已经刷过 judge2005/EleksTubeIPS，分区布局不变，本次只是更新应用固件和 LittleFS。

```bash
cd ~/elekstube-delivery/001-P1-live-image
export PORT=/dev/cu.usbserial-120

uvx --from esptool esptool \
  --chip esp32 \
  --port "$PORT" \
  --baud 57600 \
  --before default-reset \
  --after hard-reset \
  write-flash \
  --flash-mode dio \
  --flash-freq 40m \
  --flash-size 4MB \
  0x10000 firmware.bin \
  0x180000 littlefs.bin
```

刷入后等待设备重启。

如果设备 Wi-Fi 配置保留，可直接访问：

```text
http://elekstubeips.local/
```

或访问路由器中显示的设备 IP。

如果 Web UI 打不开或 Wi-Fi 配置丢失，按设备热点重新配置 Wi-Fi。

## 8. 完整刷机

仅在以下情况使用：

1. 分区布局变化。
2. 从空白设备安装。
3. 增量刷机后异常且需要完整重装 judge2005。
4. 开发者明确要求完整安装。

地址：

```text
0x1000   bootloader.bin
0x8000   partitions.bin
0xe000   boot_app0.bin
0x10000  firmware.bin
0x180000 littlefs.bin
```

命令：

```bash
cd ~/elekstube-delivery/001-P1-live-image
export PORT=/dev/cu.usbserial-120

uvx --from esptool esptool \
  --chip esp32 \
  --port "$PORT" \
  --baud 57600 \
  --before default-reset \
  --after no-reset \
  erase-flash

uvx --from esptool esptool \
  --chip esp32 \
  --port "$PORT" \
  --baud 57600 \
  --before default-reset \
  --after hard-reset \
  write-flash \
  --flash-mode dio \
  --flash-freq 40m \
  --flash-size 4MB \
  0x1000 bootloader.bin \
  0x8000 partitions.bin \
  0xe000 boot_app0.bin \
  0x10000 firmware.bin \
  0x180000 littlefs.bin
```

## 9. 刷后验证

## 9.1 Web UI 验证

浏览器打开：

```text
http://elekstubeips.local/
```

确认：

1. 可以看到 Live Images 页面或 tab。
2. 可以看到 6 个 slot。
3. 可以上传 BMP。
4. 可以清空 slot。
5. 可以切换显示模式。

## 9.2 API 验证

### 查询 slot 状态

```bash
CLOCK=elekstubeips.local

curl --fail "http://$CLOCK/api/live/slots"
```

### 生成测试 BMP

```bash
mkdir -p ~/elekstube-live-test
cd ~/elekstube-live-test

uv run --with pillow python - <<'PY'
from PIL import Image, ImageDraw

W, H = 135, 240
for i in range(6):
    im = Image.new("RGB", (W, H), (0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rectangle((5, 5, W-6, H-6), outline=(255, 255, 255), width=4)
    d.text((45, 100), str(i), fill=(255, 255, 255))
    im.save(f"slot{i}.bmp")
print("created slot0.bmp..slot5.bmp")
PY
```

### 上传 slot

```bash
for i in 0 1 2 3 4 5; do
  curl --fail \
    -H 'Content-Type: image/bmp' \
    --data-binary "@slot${i}.bmp" \
    "http://$CLOCK/api/live/slots/$i/image"
done
```

### 切换到六图模式

```bash
curl --fail \
  -H 'Content-Type: application/json' \
  -d '{"preset":"SIX_LIVE_IMAGES"}' \
  "http://$CLOCK/api/display/preset"
```

预期：六块屏幕从左到右显示 0 到 5。

### 切换到 HHMM + 双图模式

```bash
curl --fail \
  -H 'Content-Type: application/json' \
  -d '{"preset":"HHMM_WITH_TWO_LIVE_IMAGES"}' \
  "http://$CLOCK/api/display/preset"
```

预期：

```text
[H] [H] [M] [M] [slot4] [slot5]
```

中间无冒号屏。

## 9.3 持久化验证

1. 设置为 `HHMM_WITH_TWO_LIVE_IMAGES`。
2. 上传 slot 4 和 slot 5。
3. 等待 60 秒。
4. 断电重启。
5. 确认显示模式和图片仍保留。

## 10. 回滚

## 10.1 回滚到刷机前 judge2005 状态

```bash
cd ~/elekstube-backup
export PORT=/dev/cu.usbserial-120

uvx --from esptool esptool \
  --chip esp32 \
  --port "$PORT" \
  --baud 57600 \
  --before default-reset \
  --after hard-reset \
  write-flash \
  0x0 elekstube-before-live-image-a.bin
```

## 10.2 回滚到原厂状态

如果需要回原厂，使用最早保存的 factory backup：

```bash
cd ~/elekstube-backup
export PORT=/dev/cu.usbserial-120

uvx --from esptool esptool \
  --chip esp32 \
  --port "$PORT" \
  --baud 57600 \
  --before default-reset \
  --after hard-reset \
  write-flash \
  0x0 elekstube-factory-a.bin
```

## 11. 交付包结构建议

```text
001-P1-LiveImage与显示模式扩展-delivery/
├── firmware.bin
├── littlefs.bin
├── SHA256SUMS.txt
├── RELEASE_NOTES.md
├── flash-incremental.zsh
├── flash-full.zsh
└── rollback-example.zsh
```

其中 `flash-incremental.zsh` 应默认使用：

```text
PORT=/dev/cu.usbserial-120
BAUD=57600
```

但允许用户通过环境变量覆盖。

## 12. 发布说明模板

```markdown
# Release Notes：001-P1 Live Image 与显示模式扩展

## 新增

- Live image slot 0~5
- HTTP API：上传、查询、清空 live image
- Web UI：Live Images 页面 / tab
- 显示模式：HHMM + 2 live images
- 显示模式：6 live images

## 刷机

本版本包含 Web UI 修改，需要同时刷入：

- firmware.bin → 0x10000
- littlefs.bin → 0x180000

## 注意

- 写入 littlefs.bin 可能覆盖现有上传的表盘或 slideshow 文件。
- 刷机前请备份完整 flash。
- 建议使用 57600 baud。
```

## 13. 常见问题

### Q：为什么不能只刷 firmware.bin？

因为本需求包含 Web UI 页面、按钮、上传界面和前端调用逻辑。如果只刷 `firmware.bin`，固件 API 可能已经存在，但浏览器 UI 仍然是旧版，用户无法通过 Web UI 使用新功能。

### Q：为什么写 littlefs.bin 有风险？

LittleFS 是文件系统分区。Web UI 静态资源和上传资源通常都位于文件系统内。写入新的文件系统镜像可能覆盖之前的文件内容。因此刷机前必须保留完整 flash 备份。

### Q：能否不写 littlefs，只用 curl？

技术上可以把 P1-Minimal 做成只刷 `firmware.bin`，然后用 curl 调 API。但本轮产品决策是 Web UI 一起交付，所以正式验收要求 `firmware.bin` 和 `littlefs.bin` 一起交付。

### Q：MQTT 还需要吗？

P1 不需要 MQTT。图片上传走 HTTP API。MQTT 可以作为 P2 用于切换 preset 或触发动作，但不承担图片传输。

