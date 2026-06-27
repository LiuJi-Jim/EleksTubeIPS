# PRD：Live Image 与显示模式扩展（含 Web UI）

- 编号：001
- 优先级：P1
- 需求名称：Live Image 与显示模式扩展
- 目标固件：基于 judge2005/EleksTubeIPS 扩展
- 目标设备：EleksTube IPS V1 / V2，4MB flash 设备优先
- 文档版本：v0.4
- 更新日期：2026-06-27

## 1. 背景

当前 judge2005/EleksTubeIPS 已支持 clock face、slideshow、Web UI、MQTT 等能力，但图片类资源主要以“整套资源包”为单位上传和切换。

现有使用方式的主要限制：

1. Clock face 是一组 `0.bmp` 到 `9.bmp`，可选 `space.bmp`、`colon.bmp`、`am.bmp`、`pm.bmp`。
2. Slideshow 也是一组图片资源。
3. Web UI 主要上传 `.tar.gz` 资源包。
4. 切换表盘或 slideshow 时需要解压整套图片，体感耗时可达十几秒到二十秒。
5. 当前四位时间加 slideshow 的布局接近 `[H] [H] [:] [M] [M] [图]`，无法得到 `[H] [H] [M] [M] [图1] [图2]`。
6. MQTT 当前更像遥控器，只能发控制命令或短 custom 字符串，不能原生上传或替换某块屏幕的图片。

本需求希望在不重构整个显示系统的前提下，增加“单 slot 图片替换能力”，并在此基础上提供两个面向用户场景的显示模式。

## 2. 目标

P1 目标分三层：

### P1-A：Live Image Slots 底层能力

为 6 块 IPS 屏幕定义 6 个独立 live image slot：

```text
slot 0
slot 1
slot 2
slot 3
slot 4
slot 5
```

每个 slot 可以独立上传、替换、清空和重绘，不依赖 clock face / slideshow 的 `.tar.gz` 包。

### P1-B：显示模式扩展

基于 P1-A 增加两个显示模式：

```text
HHMM_WITH_TWO_LIVE_IMAGES
[S0] [S1] [S2] [S3] [S4]    [S5]
 H    H    M    M    图1     图2
```

```text
SIX_LIVE_IMAGES
[S0] [S1] [S2] [S3] [S4] [S5]
 图0  图1  图2  图3  图4  图5
```

### P1-C：Web UI 支持

P1 明确包含 Web UI 修改。

Web UI 不只是可选项，而是本需求的正式交付入口之一。用户应能通过浏览器完成：

1. 上传 / 替换 / 清空 slot 0 到 slot 5 的图片。
2. 预览每个 live image slot 当前状态。
3. 切换到 `HHMM_WITH_TWO_LIVE_IMAGES`。
4. 切换到 `SIX_LIVE_IMAGES`。
5. 回到原有的 Time / Date / Weather / Slideshow 等模式。

HTTP API 仍然是底层能力入口；Web UI 只是调用同一套 HTTP API 的前端。

## 3. 非目标

P1 不做以下内容：

1. 不通过 MQTT 传输 BMP/JPEG 图片。
2. 不支持视频或高帧率动画。
3. 不实现远程公网访问。
4. 不新增用户认证体系，沿用当前固件的局域网使用模型。
5. 不强制支持 JPEG/PNG 直接上传；P1 优先支持 BMP。
6. 不重构整个 judge2005 显示状态机。
7. 不要求兼容所有克隆机型；先保证 EleksTube IPS V1/V2。

## 4. 产品决策

### 4.1 Preset 不是新的底层状态机

文档中的 `preset` 是用户/API 层的友好名称，不应重新定义一套独立显示状态机。

底层应复用并扩展 judge2005 当前的显示配置：

```text
time_or_date
four_digit_display
```

推荐映射：

```text
HHMM_WITH_TWO_LIVE_IMAGES
→ time_or_date = TIME
→ four_digit_display = FOUR_WITH_TWO_LIVE_IMAGES
```

```text
SIX_LIVE_IMAGES
→ time_or_date = LIVE_IMAGES
→ four_digit_display = 保持原值或忽略
```

### 4.2 API 是能力源头，Web UI 是正式使用入口

所有能力必须可通过 HTTP API 调用，Web UI 通过同一 API 实现。

这样可以同时满足：

1. 普通用户通过 Web UI 操作。
2. 高级用户通过 `curl` / 脚本 / 自动化工具操作。
3. 后续如需 MQTT 控制，也可以只作为 API 上层的轻量控制通道。

### 4.3 Live image 独立于 clock face / slideshow

Live image 不应写入当前 clock face 的 `0.bmp` 到 `9.bmp`，也不应依赖 slideshow 资源包。

推荐存储路径：

```text
/ips/live/0.bmp
/ips/live/1.bmp
/ips/live/2.bmp
/ips/live/3.bmp
/ips/live/4.bmp
/ips/live/5.bmp
```

原因：

1. 避免切换 clock face 时覆盖 live image。
2. 避免替换数字字形导致所有位置出现同一数字时都显示错图。
3. 避免每次更新图片都解压整套资源包。
4. 支持单 slot 脏标记和局部重绘。

## 5. 用户场景

### 场景 A：桌面小组件

用户希望设备前四块屏幕显示当前时间 HHMM，后两块屏幕显示动态状态图。

示例：

```text
[2] [1] [3] [7] [天气图标] [服务器状态]
```

要求：

1. HHMM 由设备内部时钟自动更新。
2. 后两张图可以随时通过 Web UI 或 HTTP API 更新。
3. 更新图标不应导致前四位时间停住。
4. 更新图标不应重新上传或解压完整表盘包。

### 场景 B：六屏通知面板

用户希望六块屏幕都显示自定义图片，例如展示六个服务状态、六张通知卡片、六段图形化信息。

示例：

```text
[CPU] [MEM] [NET] [DISK] [TEMP] [ALERT]
```

要求：

1. 可以单独替换任意 slot。
2. 可以一次性进入 six-live-images 模式。
3. 单 slot 更新后只重绘对应屏幕。

### 场景 C：临时推图

用户希望在 Mac 上运行脚本：

```bash
push-elekstube-image --slot 5 ./status.bmp
```

设备应在局域网内立即更新第 6 块屏幕，而不是经历完整表盘包上传、切换和解压。

## 6. 功能需求

## 6.1 Live Image Slot 管理

### FR-LIVE-001：slot 数量

系统必须提供 6 个 live image slot，编号 `0` 到 `5`，对应从左到右的六块屏幕。

### FR-LIVE-002：单 slot 上传

系统必须提供 HTTP API 上传单个 slot 图片。

推荐接口：

```http
PUT /api/live/slots/{slot}/image
Content-Type: image/bmp
```

或：

```http
POST /api/live/slots/{slot}/image
Content-Type: image/bmp
```

其中 `{slot}` 必须为 `0` 到 `5`。

请求 body 为 BMP 文件原始字节。

### FR-LIVE-003：图片格式

P1 要求支持 BMP。

建议约束：

```text
尺寸：135 x 240 优先；小于等于 135 x 240 可选支持
位深：8-bit / 16-bit / 24-bit BMP 优先
方向：与当前 TFTs::drawImage 支持格式一致
```

P1 最小实现可以要求图片必须已经是 `135 x 240 BMP`，由客户端或 Web UI 负责裁切转换。

### FR-LIVE-004：写入策略

上传图片时必须避免半写入文件被显示。

推荐流程：

```text
写入 /ips/live/{slot}.tmp
→ 校验 BMP header 和尺寸
→ 删除旧 /ips/live/{slot}.bmp
→ rename tmp 为 /ips/live/{slot}.bmp
→ 更新 slot version / dirty flag
→ 如果当前显示模式正在引用该 slot，则强制重绘对应屏幕
```

### FR-LIVE-005：清空 slot

系统必须提供清空某个 slot 的接口。

推荐接口：

```http
DELETE /api/live/slots/{slot}/image
```

清空后该 slot 应显示黑屏或默认占位图。

### FR-LIVE-006：查询 slot 状态

系统必须提供查询 live slots 状态的接口。

推荐接口：

```http
GET /api/live/slots
```

返回示例：

```json
{
  "slots": [
    {"slot": 0, "exists": true,  "size": 65334, "mtime": 1780000000},
    {"slot": 1, "exists": false, "size": 0,     "mtime": null},
    {"slot": 2, "exists": true,  "size": 65334, "mtime": 1780000100},
    {"slot": 3, "exists": true,  "size": 65334, "mtime": 1780000200},
    {"slot": 4, "exists": true,  "size": 65334, "mtime": 1780000300},
    {"slot": 5, "exists": true,  "size": 65334, "mtime": 1780000400}
  ]
}
```

如果设备没有可靠 mtime，可以返回 `version` 或 `updated_counter`。

### FR-LIVE-007：持久化

Live image 文件应持久化存储。设备断电重启后，之前上传的 slot 图片仍应可用。

### FR-LIVE-008：启动初始化

设备启动时应确保目录存在：

```text
/ips/live
```

如果目录不存在，应自动创建。

### FR-LIVE-009：缺失图片处理

如果某个 slot 缺失、损坏或无法读取，应显示黑屏或默认占位图，不能导致主循环崩溃或看门狗重启。

## 6.2 显示模式扩展

### FR-MODE-001：HHMM_WITH_TWO_LIVE_IMAGES

新增显示模式：

```text
HHMM_WITH_TWO_LIVE_IMAGES
```

推荐底层实现：

```text
time_or_date = TIME
four_digit_display = FOUR_WITH_TWO_LIVE_IMAGES
```

显示布局：

```text
屏幕位置： 0   1   2   3   4       5
显示内容： H   H   M   M   live4   live5
```

示例：

```text
21:37 + live4/live5
→ [2] [1] [3] [7] [live4] [live5]
```

要求：

1. 不显示冒号。
2. 不占用第三屏作为 `colon.bmp` / `space.bmp`。
3. 前四位由设备内部时钟更新。
4. 后两位分别显示 `/ips/live/4.bmp` 和 `/ips/live/5.bmp`。
5. `live4` 或 `live5` 更新后，只重绘对应屏幕。

### FR-MODE-002：SIX_LIVE_IMAGES

新增显示模式：

```text
SIX_LIVE_IMAGES
```

推荐底层实现：

```text
time_or_date = LIVE_IMAGES
```

显示布局：

```text
屏幕位置： 0      1      2      3      4      5
显示内容： live0  live1  live2  live3  live4  live5
```

要求：

1. 六块屏幕都显示 live image slot。
2. 任意 slot 更新后，只重绘对应屏幕。
3. 如果 slot 缺失，应显示黑屏或占位图。
4. 该模式不依赖内部时间刷新。

### FR-MODE-003：显示模式 API

系统必须提供设置显示 preset 的 API。

推荐接口：

```http
POST /api/display/preset
Content-Type: application/json
```

请求示例：

```json
{"preset": "HHMM_WITH_TWO_LIVE_IMAGES"}
```

```json
{"preset": "SIX_LIVE_IMAGES"}
```

响应示例：

```json
{
  "ok": true,
  "preset": "HHMM_WITH_TWO_LIVE_IMAGES",
  "time_or_date": "TIME",
  "four_digit_display": "FOUR_WITH_TWO_LIVE_IMAGES"
}
```

### FR-MODE-004：显示模式查询

系统必须提供查询当前显示模式的 API。

推荐接口：

```http
GET /api/display/preset
```

响应示例：

```json
{
  "preset": "HHMM_WITH_TWO_LIVE_IMAGES",
  "time_or_date": "TIME",
  "four_digit_display": "FOUR_WITH_TWO_LIVE_IMAGES"
}
```

### FR-MODE-005：配置持久化

用户通过 Web UI 或 API 设置的新显示模式应持久化。设备重启后应恢复到上次选择的模式。

### FR-MODE-006：与原有显示模式兼容

新增模式不应破坏原有模式：

```text
TIME / SIX
TIME / FOUR
TIME / FOUR_WITH_WEATHER
TIME / FOUR_WITH_SLIDESHOW
DATE
WEATHER
SLIDE_SHOW
```

用户应能从新模式切回原有模式。

## 6.3 Web UI 功能需求

P1 明确要求修改 Web UI，并随 LittleFS 一起交付。

### FR-WEB-001：显示模式选择入口

Web UI 必须提供显示模式选择入口。

可选实现位置：

1. Clock 页面现有 display mode 区域。
2. 新增 Display 页面。
3. Files 页面增加 Live Images 分区并带模式切换快捷入口。

推荐用户文案：

```text
Display Mode
- Time: 6 digits
- Time: 4 digits
- Time: 4 digits + weather
- Time: 4 digits + slideshow
- HHMM + 2 live images
- 6 live images
- Date
- Weather
- Slideshow
```

选择后调用：

```http
POST /api/display/preset
```

### FR-WEB-002：Live Images 页面或分区

Web UI 必须提供 live image 管理界面。

推荐布局：

```text
Live Images
┌────────┬────────┬────────┬────────┬────────┬────────┐
│ Slot 0 │ Slot 1 │ Slot 2 │ Slot 3 │ Slot 4 │ Slot 5 │
│ 预览   │ 预览   │ 预览   │ 预览   │ 预览   │ 预览   │
│ Upload │ Upload │ Upload │ Upload │ Upload │ Upload │
│ Clear  │ Clear  │ Clear  │ Clear  │ Clear  │ Clear  │
└────────┴────────┴────────┴────────┴────────┴────────┘
```

每个 slot 应显示：

1. slot 编号。
2. 当前是否有图片。
3. 图片预览或占位状态。
4. 文件大小。
5. 最近更新版本或时间。
6. Upload 按钮。
7. Clear 按钮。

### FR-WEB-003：客户端图片校验

Web UI 上传前应至少做基础校验：

1. 文件类型是否为 BMP。
2. 文件大小是否明显过大。
3. 可选：在浏览器侧读取图片尺寸，提示必须为 `135 x 240`。

服务端仍必须做最终校验，不能只信任前端。

### FR-WEB-004：上传进度和结果反馈

上传过程中 Web UI 应显示：

1. 上传中状态。
2. 成功状态。
3. 失败原因。
4. 上传后自动刷新对应 slot 预览。

### FR-WEB-005：预览 API

Web UI 需要能显示当前 live image。

可选方案：

1. 直接暴露静态文件 URL：

```http
GET /ips/live/{slot}.bmp
```

2. 或新增 API：

```http
GET /api/live/slots/{slot}/image
```

建议 P1 使用新增 API 或保证静态文件路径可以被 Web UI 安全读取。

### FR-WEB-006：快速切换按钮

Live Images 页面应提供两个快捷按钮：

```text
Show HHMM + 2 Live Images
Show 6 Live Images
```

分别调用：

```json
{"preset":"HHMM_WITH_TWO_LIVE_IMAGES"}
```

```json
{"preset":"SIX_LIVE_IMAGES"}
```

### FR-WEB-007：保留脚本化能力

即使 Web UI 已经实现，HTTP API 仍然必须可独立使用。Web UI 不应把能力封装成只能由浏览器调用的私有流程。

### FR-WEB-008：LittleFS 交付

由于 Web UI 属于 LittleFS 内容，本需求交付必须包含新的 LittleFS 镜像：

```text
littlefs.bin
```

刷机说明必须包含同时更新：

```text
firmware.bin → 0x10000
littlefs.bin → 0x180000
```

## 6.4 脚本化使用需求

### FR-CLI-001：curl 上传单 slot 图片

用户应能通过命令行上传：

```bash
curl --fail \
  -H 'Content-Type: image/bmp' \
  --data-binary @slot5.bmp \
  'http://elekstubeips.local/api/live/slots/5/image'
```

### FR-CLI-002：curl 切换显示模式

用户应能通过命令行切换模式：

```bash
curl --fail \
  -H 'Content-Type: application/json' \
  -d '{"preset":"HHMM_WITH_TWO_LIVE_IMAGES"}' \
  'http://elekstubeips.local/api/display/preset'
```

### FR-CLI-003：Mac 图片转换脚本兼容

P1 不强制固件支持 PNG/JPEG，但应允许用户在 Mac 端用脚本转换：

```text
PNG/JPEG → 135x240 BMP → HTTP 上传
```

## 7. 非功能需求

### NFR-001：性能

目标性能：

```text
单 slot 图片上传并显示：目标 < 2 秒
理想情况：< 1 秒
```

不得走完整 `.tar.gz` 解压流程。

### NFR-002：可靠性

1. 单 slot 上传失败不得破坏旧图片。
2. 图片损坏不得导致设备重启。
3. Web UI 上传失败应给出错误反馈。
4. 连续上传同一 slot 时，最终显示最后一次成功上传的图片。

### NFR-003：Flash 写入控制

1. 仅在图片实际上传时写 LittleFS。
2. 切换显示模式不应重写图片文件。
3. 模式配置应复用现有配置持久化机制。

### NFR-004：内存使用

上传处理应采用流式写文件，不应把大文件完整读入 RAM。

### NFR-005：兼容性

1. EleksTube IPS V1 / V2 必须可构建。
2. 4MB flash 分区布局优先。
3. 不应要求用户重新备份原厂固件，但交付文档必须强制建议刷机前备份当前状态。

### NFR-006：回滚

用户应能通过原有 factory backup 回滚到原厂固件，或通过之前的 judge2005 backup 回滚到当前 judge2005 状态。

## 8. API 设计草案

## 8.1 查询 live slots

```http
GET /api/live/slots
```

响应：

```json
{
  "ok": true,
  "slots": [
    {"slot": 0, "exists": true, "size": 65334, "version": 12},
    {"slot": 1, "exists": false, "size": 0, "version": 0},
    {"slot": 2, "exists": true, "size": 65334, "version": 3},
    {"slot": 3, "exists": true, "size": 65334, "version": 7},
    {"slot": 4, "exists": true, "size": 65334, "version": 2},
    {"slot": 5, "exists": true, "size": 65334, "version": 9}
  ]
}
```

## 8.2 上传 slot 图片

```http
PUT /api/live/slots/{slot}/image
Content-Type: image/bmp
```

成功响应：

```json
{
  "ok": true,
  "slot": 5,
  "size": 65334,
  "version": 10
}
```

错误响应示例：

```json
{
  "ok": false,
  "error": "invalid_bmp_dimensions",
  "message": "expected 135x240 BMP"
}
```

## 8.3 清空 slot 图片

```http
DELETE /api/live/slots/{slot}/image
```

响应：

```json
{
  "ok": true,
  "slot": 5,
  "exists": false
}
```

## 8.4 获取 slot 图片

```http
GET /api/live/slots/{slot}/image
```

响应：

```text
Content-Type: image/bmp
```

如果不存在：

```http
404 Not Found
```

## 8.5 设置显示 preset

```http
POST /api/display/preset
Content-Type: application/json
```

请求：

```json
{"preset":"HHMM_WITH_TWO_LIVE_IMAGES"}
```

响应：

```json
{
  "ok": true,
  "preset": "HHMM_WITH_TWO_LIVE_IMAGES",
  "time_or_date": "TIME",
  "four_digit_display": "FOUR_WITH_TWO_LIVE_IMAGES"
}
```

## 8.6 查询显示 preset

```http
GET /api/display/preset
```

响应：

```json
{
  "ok": true,
  "preset": "SIX_LIVE_IMAGES",
  "time_or_date": "LIVE_IMAGES",
  "four_digit_display": "FOUR_WITH_TWO_LIVE_IMAGES"
}
```

## 9. Web UI 设计草案

## 9.1 导航

建议新增一级入口：

```text
Live Images
```

或在现有 `Files` 页面下新增一个 tab：

```text
Files
- Clock Faces
- Weather Icons
- Slide Show
- Live Images
```

## 9.2 Live Images 页面内容

页面内容：

```text
Header: Live Images
Description: Upload one BMP per physical screen. These images are used by Live display modes.

Display preset:
[Dropdown]
  Current mode
  HHMM + 2 live images
  6 live images
[Apply]

Slots:
  Slot 0: preview, upload, clear, status
  Slot 1: preview, upload, clear, status
  Slot 2: preview, upload, clear, status
  Slot 3: preview, upload, clear, status
  Slot 4: preview, upload, clear, status
  Slot 5: preview, upload, clear, status
```

## 9.3 Display Mode 文案

用户可见名称建议：

```text
HHMM + 2 live images
```

内部 preset：

```text
HHMM_WITH_TWO_LIVE_IMAGES
```

用户可见名称：

```text
6 live images
```

内部 preset：

```text
SIX_LIVE_IMAGES
```

## 9.4 Web UI 上传限制提示

Web UI 应提示：

```text
Please upload a 135 x 240 BMP image.
```

可选提示：

```text
For best performance, use 8-bit or 16-bit BMP.
```

## 9.5 Web UI 与已有页面兼容

如果 Clock 页面已有 `four_digit_display` 下拉框，应新增选项：

```text
4 digits + 2 live images
```

如果已有 Display/Files 页面不方便承载，则 Live Images 页面必须提供快捷切换，不依赖旧页面。

## 10. 技术实现建议

## 10.1 可能涉及源码区域

候选文件：

```text
src/IPSClock.h
src/IPSClock.cpp
src/TFTs.h
src/TFTs.cpp
src/main.cpp
Web UI / LittleFS 静态资源目录
platformio.ini
```

具体路径以实际上游 repo 为准。

## 10.2 枚举扩展建议

### IPSClock::Display

新增：

```cpp
FOUR_WITH_TWO_LIVE_IMAGES
```

### IPSClock::TimeOrDate

新增：

```cpp
LIVE_IMAGES
```

### preset alias

新增映射：

```cpp
HHMM_WITH_TWO_LIVE_IMAGES -> TIME + FOUR_WITH_TWO_LIVE_IMAGES
SIX_LIVE_IMAGES          -> LIVE_IMAGES
```

## 10.3 渲染逻辑建议

### HHMM_WITH_TWO_LIVE_IMAGES

伪代码：

```cpp
if (time_or_date == TIME && display == FOUR_WITH_TWO_LIVE_IMAGES) {
    drawDigit(0, hour_tens);
    drawDigit(1, hour_ones);
    drawDigit(2, minute_tens);
    drawDigit(3, minute_ones);
    drawLiveSlot(4);
    drawLiveSlot(5);
}
```

注意：

1. 不使用 colon slot。
2. 不使用 `space.bmp` / `colon.bmp` 闪烁逻辑。
3. 时间变化时只更新前四位发生变化的数字。
4. live slot 文件变化时只更新对应 slot。

### SIX_LIVE_IMAGES

伪代码：

```cpp
if (time_or_date == LIVE_IMAGES) {
    for (int slot = 0; slot < 6; slot++) {
        drawLiveSlot(slot);
    }
}
```

## 10.4 单 slot 重绘建议

可以为每个 slot 维护：

```cpp
uint32_t liveSlotVersion[6];
uint32_t renderedLiveSlotVersion[6];
```

上传成功后：

```cpp
liveSlotVersion[slot]++;
```

渲染时：

```cpp
if (liveSlotVersion[slot] != renderedLiveSlotVersion[slot]) {
    drawLiveSlot(slot, force=true);
    renderedLiveSlotVersion[slot] = liveSlotVersion[slot];
}
```

也可以复用现有 `TFTs::setDigit(..., force)` 机制，但 live image 不能依赖数字名称字符串，否则同名文件覆盖后可能不会触发重绘。

## 10.5 Web UI 构建建议

Web UI 资产随 LittleFS 一起交付。

交付产物应包含：

```text
firmware.bin
littlefs.bin
```

如果上游 Web UI 是静态文件，应直接修改静态资源并 build filesystem image。

如果上游 Web UI 有前端构建流程，应先运行前端构建，再 build filesystem image。

## 11. 验收标准

### AC-001：Web UI 可见入口

刷入交付包后，浏览器打开设备 Web UI，可以看到 Live Images 页面或等价分区。

### AC-002：Web UI 上传 slot 图片

在 Web UI 上传 `slot 5` 图片后：

1. 页面显示成功。
2. 预览刷新。
3. 设备处于 live 相关模式时，第 6 块屏幕更新。
4. 不重新解压 clock face 或 slideshow 包。

### AC-003：HTTP 上传 slot 图片

执行：

```bash
curl --fail \
  -H 'Content-Type: image/bmp' \
  --data-binary @slot5.bmp \
  'http://elekstubeips.local/api/live/slots/5/image'
```

第 6 块屏幕应更新。

### AC-004：HHMM_WITH_TWO_LIVE_IMAGES 显示

切换到该模式后，显示应为：

```text
[H] [H] [M] [M] [live4] [live5]
```

中间不能出现冒号闪烁屏。

### AC-005：HHMM 自动更新时间

设备保持该模式 5 分钟，前四块屏幕应由内部时钟自动更新，不能依赖外部 MQTT 或 HTTP 定时推送时间。

### AC-006：SIX_LIVE_IMAGES 显示

切换到该模式后，六块屏幕应分别显示 slot 0 到 slot 5。

### AC-007：重启持久化

设备断电重启后：

1. 上次选择的显示模式仍保留。
2. 已上传的 live image 仍保留。
3. Web UI 能正确显示 slot 状态。

### AC-008：回退原有模式

从新模式切回 Time / Date / Weather / Slideshow 等原有模式，显示应正常。

### AC-009：错误图片处理

上传非 BMP、尺寸错误、损坏 BMP 时：

1. API 返回明确错误。
2. Web UI 显示失败原因。
3. 旧图片不被破坏。
4. 设备不重启。

### AC-010：交付包可刷机

交付包中的 `firmware.bin` 和 `littlefs.bin` 可使用 esptool 写入：

```text
0x10000 firmware.bin
0x180000 littlefs.bin
```

刷入后 Web UI 和固件功能版本匹配。

## 12. 测试建议

1. `slot 0` 到 `slot 5` 分别上传不同颜色/编号 BMP。
2. 切换到 `SIX_LIVE_IMAGES`，确认 6 个 slot 位置正确。
3. 切换到 `HHMM_WITH_TWO_LIVE_IMAGES`，确认 slot 4/5 位置正确。
4. 每分钟跨越时刻观察 HHMM 更新。
5. 上传 slot 4，确认只影响第 5 块屏幕。
6. 上传 slot 5，确认只影响第 6 块屏幕。
7. 切换普通 clock face，确认 live image 不丢失。
8. 重启设备，确认模式和图片持久化。
9. 写入错误文件，确认失败且旧图保留。
10. Web UI 与 curl 两种方式都测试。

## 13. 开放问题

1. P1 是否要求 Web UI 支持 PNG/JPEG 自动转换为 BMP？
   - 建议：不纳入 P1。先要求用户上传 135x240 BMP。
2. Slot 预览走静态文件路径还是 API？
   - 建议：优先 API，避免暴露内部文件路径假设。
3. 旧 Web UI 页面是否会覆盖新枚举值？
   - 由于本版要求一起更新 Web UI，此风险降低，但仍需测试旧页面兼容。
4. 写 LittleFS 是否会清空用户现有上传的表盘/slideshow？
   - 交付和刷机文档必须按“可能清空”处理，刷机前要求备份整片 flash。
5. 是否需要批量上传 6 个 slot 的 API？
   - P1 可不做；Web UI 可并发调用 6 次单 slot API。

## 14. 优先级拆分

### P1 必须完成

1. `/api/live/slots` 查询。
2. 单 slot BMP 上传。
3. 单 slot 清空。
4. `HHMM_WITH_TWO_LIVE_IMAGES`。
5. `SIX_LIVE_IMAGES`。
6. Web UI live image 管理页。
7. Web UI display mode 切换入口。
8. 构建并交付 `firmware.bin` + `littlefs.bin`。
9. macOS + uv + esptool 刷机说明。

### P2 可选

1. PNG/JPEG 浏览器端自动转换为 BMP。
2. 批量上传 6 个 slot。
3. MQTT 控制 preset 切换。
4. MQTT 触发指定 slot refresh，但不传图片。
5. Home Assistant discovery 增加 live preset 控件。

