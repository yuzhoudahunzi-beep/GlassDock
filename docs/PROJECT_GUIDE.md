# GlassDock 项目完整技术交接文档（最新版）

> **现状增补（2026-11-24 晚，P8–P11）**：头部无计数 chip、无刷新按钮（仅齿轮，与文件夹名同中线）；列表实时刷新（`ReadDirectoryChangesW` + 120 ms 去抖，保留展开/滚动）；字体 3x 超采样灰度 AA、13 px；`TitleH=S(30)`、`RowH=S(26)`、`ListTop=TitleH+S(5)`；设置面板 `SET_H=504`、无刷新率行（固定 60 Hz、留白）、无底部提示行；`WM_SNAP` 可导出真 alpha 截图。明细见 `docs/P7_CHANGELOG.md`。
>
> `README.md` 已于本轮重写为**中英双语**版，背景描述与当前真透明架构一致（旧版"壁纸层重建+高斯模糊"描述已不存在）。

> 本文档面向接手本项目的开发者 / 智能体，**事无巨细**地记录 GlassDock 当前（2026-11-24 之后）的真实架构、源码结构、构建方式、配置项、渲染管线、已知坑与历史遗留。
>
> 本文档与 `src/GlassDock.cpp`（2793 行）、`src/crisptext.h`（281 行）、`installer/setup.cpp`（131 行）逐行对齐。**代码是唯一事实来源**，本文档若与代码冲突，以代码为准。
>
> ⚠️ 注意：本文部分章节写于 P7 之前；**P8–P11 的现状以文首增补块与 `docs/P7_CHANGELOG.md` 为准**。当前版本是**真透明（per-pixel alpha）**架构，详见下文「§4 架构演变与死代码」。

---

## 目录

1. 项目是什么
2. 文件清单与职责
3. 当前真实架构（一句话）
4. 架构演变与死代码（重要）
5. 渲染管线（逐层）
6. 文字渲染（crisptext 子模块）
7. 配置系统
8. 主窗口：行为与交互
9. 设置面板：布局与交互
10. 桌面驻留机制
11. 托盘 / 热键 / 单实例
12. 安装器与卸载
13. 构建
14. 已知坑与踩坑记录
15. 关键常量速查表
16. 给接手者的修改建议

---

## 1. 项目是什么

GlassDock 是一个 **Windows 桌面文件夹小组件**：把一个文件夹的内容以列表形式贴在桌面上，双击用系统默认程序打开。核心特征：

- **真·桌面玻璃**：窗口用 `UpdateLayeredWindow` + 32bpp 预乘 ARGB（PARGB）分层表面实现逐像素透明，桌面（含 Wallpaper Engine 动态壁纸）直接透出，**不做屏幕截图、不做高斯模糊**。
- **永远待在桌面层**：在壁纸之上、所有普通应用窗口之下，不抢焦点，`Win+D` 后仍在。
- **文字清晰锐利**：文字用 **GDI 灰度抗锯齿**渲染（不是 GDI+、不是 ClearType），实测实心像素占比从 GDI+ 的 6.8% 提升到 54.7%，观感等同原生 Windows 桌面图标标签。
- **无第三方依赖**：纯 C++17 + Win32 + GDI+，单文件 exe，常驻内存约 20 MB。

---

## 2. 文件清单与职责

| 文件 | 职责 |
| --- | --- |
| `src/GlassDock.cpp` | 主程序全部逻辑（2793 行），单文件 |
| `src/crisptext.h` | 文字渲染子模块（281 行），GDI 灰度抗锯齿 + 覆盖率合成 + 阴影 |
| `src/resource.rc` | 图标资源：`1 ICON "D:\\Doubao\\GlassDock\\app.ico"` |
| `src/app.manifest` | 并行清单：DPI 感知 + Common Controls v6 + 长路径 + asInvoker |
| `installer/setup.cpp` | 安装器（131 行），内嵌 exe+manifest 资源，装到 D 盘 |
| `installer/setup.rc` | 安装器资源脚本 |
| `app.ico` | 应用图标 |
| `build.ps1` | 构建脚本（MinGW-w64） |
| `README.md` | 面向用户的说明（⚠️ 背景描述部分已过时，见 §4） |
| `LICENSE` | MIT |
| `docs/screenshot.png` | 主界面截图（**旧版，11-23 生成，已过时**） |
| `docs/settings.png` | 设置面板截图（**旧版，已过时**） |
| `docs/PROJECT_GUIDE.md` | 本文档 |

> `tools/` 目录下是开发调试探针（`*.cpp`/`*.exe`/`*.bmp`/`*.png`/`*.mp4` 等），`.gitignore` 已整体排除，**不是产品的一部分**。
> `v.png`、`HANDOFF.md`、`.workbuddy/` 同理，已排除。

---

## 3. 当前真实架构（一句话）

> 两个窗口（主组件 + 设置面板）都是 **`WS_EX_LAYERED` 的逐像素透明窗口**：把界面画进一块 `PixelFormat32bppPARGB` 的 GDI+ 位图，`LockBits` 取出已预乘的像素拷贝进 32bpp DIB，再 `UpdateLayeredWindow(ULW_ALPHA)` 提交。背景像素 alpha=0 全透明 → 桌面（含动态壁纸）直接透出。

不涉及：屏幕截图、壁纸文件解析、高斯模糊、颜色键（colour key）、ClearType。

---

## 4. 架构演变（重要，接手前必读）

历史上 GlassDock 走过两套背景方案。**旧方案的代码已于 2026-11-24 整段删除**（净减约 400 行），当前 `GlassDock.cpp` 里不再存在壁纸快照、高斯模糊、屏幕捕获的任何实现。这段历史仍然值得了解——`g_desktopLum` 与 `TextColor()` 的现状就是旧方案留下的唯一痕迹。

### 4.1 旧方案（已废弃，代码已删除）：壁纸快照 + 高斯模糊

思路：截图/解析壁纸文件 → 高斯模糊 → 上色 → 作为窗口背景绘制。2026-11-24 删除的符号（行号为**删除前**的 3182 行版本，仅供对照备份 `build/GlassDock.cpp.bak.2026-11-24`）：

- 函数：`CaptureRegion`（289）、`BlurBitmap`（303）、`ComputeLuminance`（343）、`WallpaperPath`（369）、`WallpaperStamp`（380）、`FreeWallpaperCache`（387）、`WallpaperStyle`（394）、`RenderWallpaperLayer`（408）、`HideOwnWindows`（461）、`RestoreOwnWindows`（468）、`RefreshDesktopSnapshot`（481）、`WidgetObscured`（519）、`CropDesktop`（547）、`MakeBg`（595）、`RebuildMainBg`（645）、`RebuildMainBgLive`（660）
- 全局：`g_bg`、`g_screenShot`、`g_screenShotSmall`、`g_vx/g_vy/g_vw/g_vh`、`g_wallImage`、`g_wallPath`、`g_wallStamp`、`g_bgSource`、`g_ssSmallScale`、`g_ssSmallSrc`、`TRANSPARENT_KEY`
- 退出时对上述三个 Bitmap 的 `delete`

**为什么确认是死代码**：`g_bg` 从未被任何 `DrawImage` 绘制；`RebuildMainBg` / `RebuildMainBgLive` 零调用；`RefreshDesktopSnapshot` 零调用（它才是唯一给 `g_screenShot` 赋值的地方）。`g_desktopLum` 原本只由 `MakeBg` 更新，而 `MakeBg` 无人调用，所以它**恒等于初始值 0.5**，`TextColor()` 因此恒按"中性亮度"返回浅色文字。

**删除后仍保留的两项**：`RenderBackground()`（活代码，函数体只有 `Repaint()`，被启动流程、`WM_SETTINGCHANGE`、托盘/热键呼出、右键刷新调用）和 `g_desktopLum`（`TextColor()` 的入参）。将来若接"文字颜色随壁纸自适应"，`g_desktopLum` 就是那个需要被真正赋值的变量。

> ⚠️ 已知副作用（仍然存在）：`g_desktopLum` 恒为 0.5，文字颜色不会随壁纸明暗切换（`TextColorFor(0.5)` 返回 `(247,247,250)` 浅色）。真透明模式下没有壁纸快照可采样，修复需要新的亮度来源，见 §16。

### 4.2 旧方案里的模糊强度（已删除）

设置面板原有第 4 个滑块「模糊强度」，驱动一个自绘的磨砂颗粒纹理（frosted grain）。**已彻底删除**：
- 删除了 `Config::blur` 字段、`DrawFrost()`、`MakeFrostTexture()`、`g_frostTex`。
- `MakeBg()` 里的 `cfg.blur` 引用改为硬编码 `factor = 1`（因为 `MakeBg` 本身已是死代码，只求编译通过）。
- 删除位置留了注释：`// ---------------- frost overlay (removed) ----------------`（932 行）。

### 4.3 当前活代码路径

- 主窗口渲染：`PaintAll()` → `CommitLayered()` → `UpdateLayeredWindow`。
- 设置面板渲染：`PaintSettings()` → `CommitSetLayered()` → `UpdateLayeredWindow`。
- 文字渲染：`DrawTextCrisp()` → `crisptext.h` 的 `Rasterize`/`Composite`/`DrawWithShadow`。
- 图标：`IconFor()` → `GetShellIcon()` → `AlphaBitmapFromHIcon()`。

---

## 5. 渲染管线（逐层）

### 5.1 主窗口 `PaintAll(Graphics& g, int w, int h)`（937 行）

执行顺序：

1. 设置 Graphics 状态：`SmoothingModeAntiAlias`、`InterpolationModeHighQualityBicubic`、`PixelOffsetModeHighQuality`、`TextRenderingHintAntiAliasGridFit`（**灰度 AA，不是 ClearType**）。
2. **填充全透明背景**：`SolidBrush(Color(0,0,0,0))` + `FillRectangle(0,0,w,h)` —— 这是逐像素透明的关键，背景像素 alpha=0。
3. **可选着色**（`cfg.opacity > 0` 时）：在圆角矩形路径内填充 `Color(a, accent)`，其中 `a = opacity*255/100`。这是"不透明度"滑块的唯一作用——给透明区盖一层 accent 色的半透明 tint。圆角用 `RoundedRect(..., S(12))`，与边框圆角一致。
4. **（已删）** 磨砂颗粒 —— 不再有。
5. 依次绘制：`DrawList()`（列表）→ `DrawScrollbar()`（滚动条）→ `DrawTitle()`（标题）→ `DrawFrame()`（边框）。
6. 右下角画三条斜线作为"可缩放"提示（`ResizeCornerRect`）。

### 5.2 各绘制函数细节

**`DrawTitle(g, w)`（717 行）**：
- 左侧文件夹图标 `\uE8B7`（Segoe Fluent Icons / Segoe MDL2 Assets），用 `g_fontGlyphSmall`。
- 文件夹名（`g_root->name`），`g_fontBold`，左对齐，`StringTrimmingEllipsisCharacter` 截断。
- 名字后**不再跟数量胶囊**（P9 按用户要求移除）；标题可用宽度放宽到只剩齿轮占位。
- 标题下方一条 accent 色渐变细线（`LinearGradientBrush` + `SetBlendTriangularShape`），两端淡出。
- 右侧两个按钮：刷新 `\uE72C`、设置 `\uE713`（`GetButtons` 返回 3 个 RECT，第 3 个是空的——**没有关闭按钮**）。

**`DrawList(g, w, h)`（801 行）**：
- 裁剪到 body 区（标题栏以下）。
- 空文件夹显示「（空文件夹）」。
- 逐行绘制：hover 高亮胶囊 → 目录展开箭头（`\uE70D` 展开 / `\uE76C` 收起）→ 图标（`IconFor`）→ 文字（`it->DisplayName()`，`g_fontUi`）。
- 行高 `RowH() = S(28)`，标题栏高 `TitleH() = S(36)`，左缩进 `LeftPad() = S(12)`，层级缩进 `Indent() = S(16)`。

**`DrawScrollbar(g, w, h)`（884 行）**：内容超出才显示，4px 宽圆角条，accent 色渐变。

**`DrawFrame(g, w, h)`（911 行）**：两条同心圆角矩形描边（外圈 alpha 70 白、内圈 alpha 22 白），圆角 `S(12)`，形成玻璃厚边。

### 5.3 提交到屏幕 `CommitLayered()`（1232 行）

1. 计算目标尺寸（resize 中用的是 `cfg.width/height`，否则用 `GetClientRect`）。
2. `GetLayeredSurface(w,h)`：创建/复用 32bpp top-down DIB section（`CreateDIBSection`，`biHeight = -h`）。
3. `GetPaintBitmap(w,h)`：创建/复用 `PixelFormat32bppPARGB` GDI+ 位图。
4. `Graphics g(paint)` 包住 PARGB 位图，`g.Clear(Color(0,0,0,0))`，设 `g_paintDst = paint`（供 crisp 文字渲染直写），调 `PaintAll`，再 `g_paintDst = NULL`，`Flush`。
5. `CopyPaintToDIB(paint, w, h)`：`LockBits(Read, PARGB)` 取出像素，memcpy 进 DIB。
   - **关键**：PARGB 像素已经是预乘的（premultiplied），**不能再乘一次**。这里只做一件事：把 alpha=0 的像素钳位成 alpha=1（`d[x*4+3] = 1`），用于欺骗鼠标命中测试（见 §14 坑 5）。
6. `UpdateLayeredWindow(hwnd, screen, &dst, &sz, surf, &src, 0, &blend, ULW_ALPHA)`。
   - resize 中 `dst = g_resizePos`（让窗口原子地移动+缩放，防重影）；否则 `dst = GetWindowRect`。

### 5.4 设置面板 `PaintSettings(g, w, h)`（1547 行）

- 固定逻辑尺寸 `SET_W = 400`、`SET_H = 516`（乘以 `S()` 做 DPI 缩放）。
- 背景是**固定** `Color(51, 30, 30, 32)`（约 20% alpha 的深灰 slate），不随组件的不透明度/背景色变化。
- 文字颜色**固定**浅色 `Color(255, 247, 247, 250)`。
- 顶部标题「设置」+ 关闭按钮 `\uE8BB` + 分隔线。
- 内容分区（逻辑 y 坐标，均经 `S()` 缩放）：
  - 文件夹卡片（y=60，高 44）：显示当前文件夹名 + 完整路径 + 「更改…」按钮。
  - 「外观」区（标签 y=112）：**3 个滑块**（宽度 240–1000、高度 240–1200、不透明度 0–100%）。
  - 「背景色」区（标签 y=296）：6 个预设色板 + 16 进制输入框 + 系统取色器按钮。
  - 「选项」区（标签 y=356）：开机自启复选框；**拖动刷新率行已移除**（P10，固定 60 Hz），原位留白。
  - 底部提示行（y=430）+ 分隔线（y=452）+ 恢复默认 / 完成 按钮（y=460）+ 「改动即时生效」提示。
  - 顶部 34% 高光（sheen）+ 底部 30% 阴影 + 边框。

### 5.5 设置面板提交 `CommitSetLayered()`（1333 行）

与主窗口同构，独立的一套 `g_setLs` / `g_setPaint` / `GetSetLayeredSurface` / `GetSetPaintBitmap`。同样有 alpha 0→1 钳位。

---

## 6. 文字渲染（crisptext 子模块，`src/crisptext.h`）

### 6.1 为什么不用 GDI+

GDI+ 在 12–15px 的 CJK 笔画（细于 1 设备像素）会把一根竖笔画劈成两个半覆盖灰像素，导致"发虚"。实测（作者写在 crisptext.h 头注释）：

| 渲染器 | 实心笔画像素占比 | 边缘过渡 |
| --- | --- | --- |
| GDI+ AntiAliasGridFit @15px | 6.8% | 1.98 px |
| GDI 灰度 AA @15px | 54.7% | 1.08 px |

Windows Explorer 画桌面图标标签用的是 GDI ClearType；但本组件是逐像素透明窗口，ClearType 的 RGB 亚像素在预乘后会变成彩色毛边，**不能**用。于是用 GDI 的灰度抗锯齿（`ANTIALIASED_QUALITY`），观感等同原生标签。

### 6.2 渲染流程（Rasterize → Composite → DrawWithShadow）

**`Rasterize(hf, text, boxW, boxH, hAlign, vAlign, ellipsis, cov, ...)`**：
1. 用 GDI `DrawTextW(DT_CALCRECT)` 先测出文本实际宽高（对齐方式、省略号行为都与 Windows 一致）。
2. 按对齐方式算出 offX/offY。
3. 在 `tw+2 × th+2` 的 32bpp 临时 DIB 上，黑底白字（`SetBkColor(0,0,0)`、`SetTextColor(255,255,255)`）用 `DrawTextW` 栅格化。
4. 逐像素取 **R 通道**（黑底上的 R 值 = 覆盖率，0~255）。

**`Composite(bmp, ox, oy, cov, w, h, r, g, b)`**：
- `LockBits((ImageLockMode)3, PixelFormat32bppPARGB)` 直接读写 PARGB 位图。
- 对每个覆盖像素做 source-over 混合：`p = src*coverage/255 + p*(255-coverage)/255`，四个通道（B、G、R、A）都做。**这里写出的就是预乘像素**。

**`DrawWithShadow(bmp, hf, text, ..., sox, soy, sr, sg, sb, shadowAlpha)`**：
- 栅格化一次得到 coverage。
- 先画阴影：`shadowCov = cov * shadowAlpha/255`（阻尼复制），偏移 `(sox, soy)`，用阴影色 `(sr,sg,sb)` 合成。
- 再画主字形：原 coverage、原位置、主色合成。
- 阴影复用同一 coverage，因此**不会**像旧的 8 向描边那样把细笔画内部涂黑。

### 6.3 与主程序的衔接

- `InitFonts()`（222 行）为每个 GDI+ 字体创建对应的 **GDI twin**（`MakeHFont`）：
  - UI 字体：**直接从系统取**（`ReadSystemLabelFont`），顺序 ① `HKCU\Control Panel\Desktop\WindowMetrics\IconFont` —— 资源管理器画桌面图标标签用的就是它（本机 = `Microsoft YaHei`，`lfHeight=-12`）→ ② `SPI_GETICONTITLELOGFONT` → ③ `SPI_GETNONCLIENTMETRICS.lfMessageFont`。px = `|lfHeight|`，**注册表来的要乘 `g_scale`**（`IconFont` 存的是 96dpi 单位，-12 在 125% 下要画成 15px 才等于资源管理器列表的字），SPI 来的已是设备像素；钳 8~40，face 名不可用才退回 `Segoe UI`。用户在「显示设置」里改图标字体后，重启小组件即自动跟随。
  - `g_fontUi`（常规）、`g_fontBold`（粗体）、`g_fontSmall`（`max(px-1.5, 9)`）。
  - 图标字体：`Segoe Fluent Icons`（Win11），不存在则 `Segoe MDL2 Assets`；`g_fontGlyph`(13)、`g_fontGlyphSmall`(11)、`g_fontWatermark`(60，已弃用但仍在创建)。
- `HFontFor(Font*)`（212 行）把 GDI+ 字体映射回 GDI twin。
- `DrawTextCrisp(g, s, f, r, sf, br)`（603 行）是所有文字的统一入口：
  1. 从 brush 取颜色。
  2. `HFontFor` 找 GDI twin；找不到或 `g_paintDst` 为空 → 回退 `g.DrawString`（旧 GDI+ 路径，兜底）。
  3. 解析 `StringFormat` 的对齐/省略号 → 转成 crisp 的 HAlign/VAlign/ellipsis。
  4. 按亮度决定阴影方向：`lum>=128`（浅字）→ 黑阴影(0,0,0)，否则白阴影(255,255,255)；偏移 `(1,1)`，`shadowAlpha=85`（约 33%，对齐桌面标签实测）。

### 6.4 MinGW 兼容性坑（已规避）

- MinGW 只声明 `GdipDrawImageRectRectI` 为 4 个独立 int，与真实 API（`GpRect*`）不符 → 因此**不用** GDI+ 的 DrawImage(Rect) 重载，改用手工像素写。
- MinGW 的 `ImageLockMode` 枚举只暴露 `Read`/`Write`，没有 `ReadWrite` → 用强制转换 `(ImageLockMode)3`。
- `PixelFormat32bppPARGB` 是宏，不能加 `Gdiplus::` 命名空间前缀。
- `Graphics::Clear()` 不会真正产出透明的 PARGB 表面 → 用 `g.Clear(Color(0,0,0,0))` 配合 PARGB 位图（实测有效）。

---

## 7. 配置系统

### 7.1 配置结构 `Config`（39 行）

```cpp
struct Config {
    std::wstring folder;        // 展示的文件夹
    int width = 420, height = 560;
    int x = -1, y = -1;         // 屏幕坐标（-1 = 未设置，首次启动用默认位置）
    int opacity = 36;           // 0..100，着色强度（100 = 纯色）
    COLORREF accent = RGB(128,128,128);  // 背景色（默认中性灰）
    bool topmost = true;        // 字段保留，实际恒写 0
    bool autostart = false;
    bool foldersFirst = true;
    bool showExt = true;
    int refreshHz = 60;         // 拖动刷新率 30..240
};
```

### 7.2 存储位置与键

- 文件：`%APPDATA%\GlassDock\config.ini`，节 `[GlassDock]`。
- 读写：`GetPrivateProfileStringW` / `WritePrivateProfileStringW`（封装在 `GetIniInt/GetIniStr/SetIniInt/SetIniStr`）。
- 键：`folder`、`width`、`height`、`x`、`y`、`opacity`、`accent`（RRGGBB 十六进制）、`topmost`（恒 0）、`autostart`、`foldersFirst`、`showExt`、`refreshHz`。
- ⚠️ 历史遗留：旧的 `blur` 键若存在于老用户的 ini 里，**已不再读取也不再写入**（无害残留）。
- 钳位：`width` 200~1200、`height` 160~1600、`opacity` 0~100、`refreshHz` 30~240。
- `SavePos()`（160 行）在退出/移动后把当前 `GetWindowRect` 的左上角写回 `x/y`。
- `ApplyAutoStart()`（167 行）：在 `HKCU\...\CurrentVersion\Run` 写入/删除 `GlassDock` 键（值为 `"<exe路径>"`）。

---

## 8. 主窗口：行为与交互

### 8.1 窗口创建（wWinMain，3072 行）

- `SetProcessDPIAware()`（系统级 DPI 感知，非 per-monitor）。
- `g_scale = GetDeviceCaps(LOGPIXELSX) / 96.0`（最小 1.0）。
- 窗口样式：`WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED`，类名 `GlassDockWidget`，`WS_POPUP`，`CS_DBLCLKS`。
- 首次启动位置：`x = 工作区右 - width - S(24)`，`y = 工作区顶 + S(60)`（即屏幕右上附近）。
- 圆角：`RoundCorners()` 调 `DwmSetWindowAttribute(DWMWA_WINDOW_CORNER_PREFERENCE=33, DONOTROUND=1)`，禁用 DWM 自带圆角，只保留自绘圆角（避免两套圆角打架）。

### 8.2 命中与交互（WndProc）

| 消息 | 行为 |
| --- | --- |
| `WM_LBUTTONDOWN` | 依次判：resize 边缘 → 标题栏拖动 → 按钮按下 → 目录箭头展开 |
| `WM_MOUSEMOVE` | 拖动/resize/hover 三态；实时 `Repaint()` |
| `WM_LBUTTONUP` | 结束拖动/resize（存位置）、按钮点击动作 |
| `WM_LBUTTONDBLCLK` | 双击行：目录箭头则 toggle，否则 `OpenItem` |
| `WM_MOUSEWHEEL` | 滚动 `g_scroll`（每格 60px） |
| `WM_RBUTTONUP` | 弹右键菜单 |
| `WM_NCHITTEST` | 恒返回 `HTCLIENT`（整窗视为一个实心命中目标） |
| `WM_SIZE` | `SIZE_MINIMIZED` 时恢复显示 + 回桌面层 |
| `WM_SETTINGCHANGE` | 壁纸/主题变化 → `RenderBackground()` |

### 8.3 缩放（仅右/下/右下角）

- `HitResize()`（2234 行）**只允许** `RS_E`（右）、`RS_S`（下）、`RS_SE`（右下）。左/上边及所有与之相邻的角被禁用。
- 原因（代码注释）：左/上缩放会移动窗口原点，逐像素透明窗口原点移动会在指针下留残影、抖动；右/下缩放只改尺寸、原点不动，稳定。
- resize 中：**不调 `SetWindowPos`**，而是设 `g_resizeHasPos = true`、`g_resizePos = (nx,ny)`，让 `CommitLayered` 通过 `UpdateLayeredWindow` 的 `pptDst`/`psize` 原子地移动+缩放，防"双像重影"。
- 最小尺寸 `S(200)×S(160)`，最大受工作区限制。
- `KeepOnScreen()`（1909 行）：放大靠近屏幕边缘时贴回可见区。

### 8.4 按钮

`GetButtons(w, out[3])`：右侧两个按钮 = 刷新(0)、设置(1)；第 3 个(2) 恒为空 RECT（**没有关闭按钮**，避免误点消失）。

- 刷新按钮 **已移除**（P9）：改为 `ReadDirectoryChangesW` 递归监听 + `WM_LIVE`/`TIMER_LIVE` 120 ms 去抖 → `LiveReload()`（保留展开与滚动）。
- 设置按钮 → `OpenSettings()`。

---

## 9. 设置面板：布局与交互

### 9.1 布局常量（1783–1795 行，逻辑坐标，经 `S()` 缩放）

| 元素 | 逻辑坐标 (x, y, w, h) |
| --- | --- |
| 关闭按钮 `R_SetClose` | (340, 10, 28, 28) |
| 文件夹卡片 `R_FolderCard` | (16, 60, 368, 44) |
| 更改按钮 `R_FolderChange` | (306, 66, 68, 32) |
| 滑块轨道 y（3 个）`SliderTrackY` | {162, 214, 266} |
| 滑块标签 y（3 个）`SliderLabelY` | {140, 192, 244} |
| 滑块命中区 y（3 个）`R_SliderHit` | {162, 214, 266} |
| 色板 `R_Preset(i)` | (20 + i*38, 318, 30, 30) |
| 16 进制框 `R_HexBox` | (250, 318, 84, 30) |
| 取色器 `R_CustomColor` | (338, 318, 30, 30) |
| 自启复选框 `R_CheckAuto` | (18, 376, 344, 24) |
| Hz 标签 `R_HzLabel` | (20, 402, 120, 20) |
| Hz 选项 `R_HzOption(i)` | (110 + i*44, 400, 38, 24) |
| 恢复默认 `R_Reset` | (20, 460, 100, 34) |
| 完成 `R_Done` | (284, 460, 84, 34) |

### 9.2 命中枚举 `SH_*`（1414 行）

```
SH_NONE, SH_CLOSE, SH_FOLDER,
SH_SLIDER0(宽), SH_SLIDER1(高), SH_SLIDER2(不透明度),   // 3 个滑块
SH_P0..SH_P5(色板), SH_CUSTOM, SH_HEX, SH_AUTO,
SH_RESET, SH_DONE   // P10 起无 SH_HZ0..5
```

### 9.3 滑块语义（`SetSliderFromX`，2284 行）

- `SH_SLIDER0` → `cfg.width = 240 + t*760`（240~1000）
- `SH_SLIDER1` → `cfg.height = 240 + t*960`（240~1200）
- `SH_SLIDER2` → `cfg.opacity = t*100`（0~100）

### 9.4 16 进制输入（`WM_CHAR`，2514 行）

- 点击 hex 框进入编辑态 `g_hexEditing = true`，`g_hexBuf = "#" + 当前色`。
- 输入 `#RRGGBB`（6 位十六进制，大小写均可，自动转大写），输满 6 位立即 `ApplyHex`。
- 回车提交、ESC 取消、退格删除。
- 点其他任何控件会退出编辑态。

### 9.5 设置面板窗口（`OpenSettings`，2568 行）

- 样式：`WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED`，类名 `GlassDockSettings`。
- 屏幕工作区居中（不叠在组件上）。
- `WM_ACTIVATE` 失焦时 `HWND_NOTOPMOST`（失焦恢复普通窗口行为）。
- 单实例：已存在则置顶+前置+显示。

---

## 10. 桌面驻留机制

核心函数 `SitOnDesktop()`（1045 行）：

1. 窗口扩展样式：去掉 `WS_EX_TOPMOST`，加上 `WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`。
2. 找壁纸宿主窗口 `GetDesktopWorkerW()`（1025 行）：
   - 优先 `FindWindow("Progman")`（Win10/11 桌面图标是 Progman 的子窗口）。
   - 回退：找含 `SHELLDLL_DefView` 的 `WorkerW`，再退任意 `WorkerW`。
3. 目标插入位置 = `GetWindow(host, GW_HWNDPREV)`（宿主上方紧邻的窗口），无则 `HWND_TOP`。
   - ⚠️ `SetWindowPos(hwnd, hInsertAfter)` 是"插到 hInsertAfter **之后**（= 其下方）"。
4. `SetWindowPos(insertAfter, ..., SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW)`。

**为什么不用 `HWND_BOTTOM`**：DWM 可能把窗口停到壁纸**之下**导致不可见（"有时出现有时不出现"）。

**桌面守卫**：`TIMER_GUARD`（1 秒）：
- 若被隐藏/最小化 → `SitOnDesktop()` + `Repaint()`。
- 每 5 tick 重新 `SitOnDesktop()` 一次（除非正在拖动/缩放）。
- 用户主动隐藏（`g_userHidden = true`）后停止干预。

**`WM_SETTINGCHANGE` / `RenderBackground()`**：当前真透明模式下 `RenderBackground()`（288 行）只是 `Repaint()`，没有实际背景可刷新。

---

## 11. 托盘 / 热键 / 单实例

- **单实例**：命名互斥体 `Local\GlassDock_SingleInstance`；二次启动时 `PostMessage(WM_SHOWME)` 给已有窗口，然后退出。
- **托盘**：`AddTrayIcon()`（2313 行）`Shell_NotifyIcon(NIM_ADD)`，图标 = 资源 id 1。左键/双击 → `ToggleWidget()`；右键 → `ShowTrayMenu()`。
- **热键**：`RegisterHotKey(hwnd, 1, MOD_CONTROL|MOD_ALT, 'G')` = `Ctrl+Alt+G`。
- **`ToggleWidget()`**（2355 行）实际上**只显示、不切换**（函数体等价 `ShowWidget()`）：因为被压到壁纸下的窗口 `IsWindowVisible` 仍为 TRUE，基于可见性的 toggle 不可靠。所以热键/托盘/二次启动都是"确保显示并回桌面层"，**不设 `TOPMOST`、不隐藏**（托盘气泡文案里的「显示 / 隐藏」是历史措辞，实际只做显示）。
- **隐藏**：右键菜单「隐藏到托盘」→ `HideWidget()`（`SW_HIDE` + `g_userHidden=true`）+ 托盘气泡提示一次。
- **托盘菜单**：显示/隐藏小组件、打开设置、退出。

---

## 12. 安装器与卸载

### 12.1 安装器 `installer/setup.cpp`

- `GetTargets()`：**优先安装到 `D:\GlassDock`**（用户规则：软件放 D 盘，不占 C 盘）；D 盘不存在才退回 `%LOCALAPPDATA%\Programs\GlassDock`。
- 从资源 `RID_EXE(100)`、`RID_MANIFEST(101)` 解出 `GlassDock.exe` + `GlassDock.exe.manifest`。
- 创建桌面 + 开始菜单快捷方式（`IShellLinkW`）。
- 写卸载项到 `HKCU\...\Uninstall\GlassDock`（DisplayName、DisplayVersion=1.0.0、UninstallString=`"<exe>" --uninstall` 等）。
- 弹 `MB_YESNO` 问是否立即启动。

### 12.2 卸载 `GlassDock.exe --uninstall`（`RunUninstall`，3032 行）

- 删桌面/开始菜单快捷方式。
- 删 `HKCU\...\Run\GlassDock`、`HKCU\...\Uninstall\GlassDock`。
- `MessageBox` 提示完成。
- 通过 `cmd /c ping -n 2 ... & rd /s /q` 延时删除安装目录和 `%APPDATA%\GlassDock`（配置目录）。

---

## 13. 构建

### 13.1 环境

- MinGW-w64（x86_64），g++ 16+（作者用 16.2.0）。
- 链接库：`-lgdiplus -ldwmapi -lshell32 -lcomctl32 -lole32 -luuid -lshlwapi -lgdi32 -luser32 -ladvapi32`。
- 编译参数：`-mwindows -municode -O2 -s -static`。

### 13.2 构建脚本 `build.ps1`

```powershell
.\build.ps1
```

步骤：
1. `windres resource.rc -o resource.o`（图标）。
2. `g++ src/GlassDock.cpp src/resource.o -o GlassDock.exe ...`。
3. 复制 `app.manifest` → `GlassDock.exe.manifest`。
4. `windres setup.rc -o setup_res.o`。
5. `g++ installer/setup.cpp setup_res.o -o installer/setup.exe`。
6. 复制 `installer/setup.exe` → `release/GlassDock-Setup-1.0.0.exe`。

### 13.3 手动编译单文件（快捷）

```bash
g++ -mwindows -municode -O2 -s -static \
    src/GlassDock.cpp src/resource.o -o GlassDock.exe \
    -lgdiplus -ldwmapi -lshell32 -lcomctl32 -lole32 -luuid \
    -lshlwapi -lgdi32 -luser32 -ladvapi32
```

> ⚠️ 编译前先杀掉正在运行的 `GlassDock.exe`（`taskkill //F //IM GlassDock.exe`），否则 exe 被占用，链接器报 `Permission denied`。

---

## 14. 已知坑与踩坑记录

以下是作者在开发中实际踩过的坑，接手者务必注意：

1. **双重预乘**：GDI+ 往 HDC(DIB) 画的 32bpp 像素 RGB 已经是预乘值；若下游走 `UpdateLayeredWindow`（要求预乘）又乘一遍 = 双重预乘，边缘被压暗成灰。**解法**：全程用 `PixelFormat32bppPARGB` 位图渲染，`LockBits(PARGB)` 直接取已预乘数据，`CopyPaintToDIB` 只做 alpha 0→1 钳位、不做颜色乘法。

2. **ClearType 不能用于逐像素透明窗口**：RGB 亚像素在预乘后变彩色毛边。文字用 GDI 灰度 AA（`ANTIALIASED_QUALITY`），Graphics 用 `TextRenderingHintAntiAliasGridFit`。

3. **8 向描边会涂黑细笔画**：描边是"在原位置画偏移副本"，笔画内部像素也被覆盖，2~3px 的细笔画（如字母 P 的竖笔）会被涂成实心黑块。**解法**：用单方向 1px 柔和阴影（`DrawWithShadow`，复用同一 coverage，不会重涂笔画内部）。

4. **图标黑框**：`Bitmap::FromHICON` 会把被 mask 的透明像素压成不透明黑。`SHGFI_ICON|SHGFI_SMALLICON` 是 16×16 老式 DDB 图标，透明度在 1-bit AND mask。**解法**：`SHGetFileInfo(SHGFI_SYSICONINDEX|SHGFI_LARGEICON)` 拿系统图像列表 + `ImageList_GetIcon(ILD_TRANSPARENT)` 物化 32bpp 图标，再 `AlphaBitmapFromHIcon()` 手工重建 ARGB（颜色位图有 alpha 就用 alpha，否则用 AND mask；并对透明像素做"颜色扩散"防缩放黑边）。

5. **逐像素透明的鼠标穿透**：分层窗口 alpha=0 像素会被 OS 判为 `HTTRANSPARENT`，点击穿透到后面（如被组件压住的桌面图标）。`WM_NCHITTEST` 对这类像素不送达。**解法**：`CopyPaintToDIB` 把 alpha=0 钳位成 alpha=1（肉眼不可见），让命中测试把点击路由到本窗口；`WM_NCHITTEST` 恒返 `HTCLIENT` 双保险。

6. **resize 抽搐/重影**：拖动左/上边框会移动窗口原点，client 坐标增量漂移。**解法**：① 只允许右/下/右下 resize；② resize 增量用**屏幕坐标**（`ClientToScreen`）；③ resize 中不 `SetWindowPos`，用 `UpdateLayeredWindow` 的 pptDst/psize 原子移动+缩放。

7. **DWM 圆角打架**：Win11 DWM 自带约 8px 圆角，与自绘 S(12) 圆角叠加产生错位。**解法**：`DwmSetWindowAttribute(DWMWA_WINDOW_CORNER_PREFERENCE, DONOTROUND)`。

8. **`HWND_BOTTOM` 不可靠**：DWM 可能把窗口停到壁纸下面导致"有时不显示"。**解法**：插到壁纸宿主（Progman/WorkerW）上方紧邻处。

9. **MinGW 兼容**：见 §6.4。

10. **`g_desktopLum` 恒为 0.5**：真透明模式下没有壁纸快照可采样，`TextColor()` 恒返回浅色文字，文字颜色不随壁纸明暗切换（见 §4.1 副作用）。

---

## 15. 关键常量速查表

| 常量 | 值 | 位置/含义 |
| --- | --- | --- |
| `TRANSPARENT_KEY` | `RGB(255,0,255)` | 旧颜色键（洋红），已不再使用 |
| 标题栏高 `TitleH()` | `S(36)` | |
| 行高 `RowH()` | `S(28)` | |
| 左缩进 `LeftPad()` | `S(12)` | |
| 层级缩进 `Indent()` | `S(16)` | |
| 圆角半径 | `S(12)` | 主窗口边框/tint |
| 设置面板尺寸 | `400 × 516`（逻辑） | `SET_W/SET_H` |
| 设置面板背景 | `Color(51,30,30,32)` | ~20% 深灰 slate |
| 主文字色（浅） | `Color(255,247,247,250)` | `TextColor()` 恒返此值 |
| 文字阴影 alpha | 85（~33%） | `DrawTextCrisp` 里 `shAlpha` |
| 阴影偏移 | (1, 1) | 右下 1px |
| 预设色板 `g_presets` | 灰 808080 / 深 1E1E1E / 浅 F5F5F5 / 蓝 2B6CB0 / 青 1FA2A6 / 紫 6B5BD2 | |
| 刷新率档位 `g_hzPresets` | {60,90,120,144,165,240} | |
| 宽度范围 | 240~1000 | 滑块 |
| 高度范围 | 240~1200 | 滑块 |
| 不透明度范围 | 0~100 | 滑块 |
| 最小窗口 | `S(200)×S(160)` | resize |
| 热键 | `Ctrl+Alt+G` | `HOTKEY_TOGGLE` |
| 单实例互斥体 | `Local\GlassDock_SingleInstance` | |
| 窗口类名 | `GlassDockWidget` / `GlassDockSettings` | |
| 刷新率→间隔 | `1000/refreshHz`，钳 4~33ms | `g_liveBgInterval` |

---

## 16. 给接手者的修改建议

1. **死代码已清理（2026-11-24 已完成）**：§4.1 列出的整条旧背景链与相关全局已从文件中删除，`GlassDock.cpp` 由 3182 行降到 2793 行。删除前后各编译一次，用 `PrintWindow` 抓同一窗口做逐像素对比，结果**0 像素差异**（文本色像素数 2334，完全一致）。若需回滚，备份在 `build/GlassDock.cpp.bak.2026-11-24`。

2. **文字颜色自适应壁纸**：若要恢复"浅色壁纸→深字，深色壁纸→浅字"，需在真透明模式下另找壁纸亮度来源（例如对 `WallpaperPath()` 的壁纸文件采样亮度，而非截屏——但注意动态壁纸无法这样处理）。这是当前唯一已知的功能缺口。

3. **重新生成截图**：`docs/screenshot.png`、`docs/settings.png` 是旧版（含已删的「模糊强度」滑块）。若在意，可运行最新版后重新截图替换。

4. **README 状态（2026-11-24 复核）**：`README.md` 已于 11-24 19:05 追平真透明架构——「技术要点」第 120/121/123 行的描述**是正确的**，本节原先"第 14、17、18 / 120、121、123 行仍是旧方案"的判断已过时。本次只修正了剩余措辞（第 3 行「毛玻璃 / 亚克力」、第 17 行「文字颜色自动适配」、第 20 行「同一套毛玻璃质感」）与呼出行为描述（第 45 / 54 / 119 行），并补上「拖动刷新率」设置项。

5. **DPI 感知**：当前用 `SetProcessDPIAware()`（系统级），不是 per-monitor v2。多显示器不同缩放比下可能模糊，若要升级需改 manifest + `SetProcessDpiAwarenessContext` 并用 `S()` 缩放（已预留 `g_scale`）。

6. **设置面板留白已填平（2026-11-24 已完成）**：删除「模糊强度」滑块留下的 66px 空档，已按"「背景色」及其下所有区块整体上移 48px + 面板高度 548→516"的方案填平，区块内部相对间距不变。顺带修正了两个旧瑕疵：底部提示行（原 y=470）与分隔线（原 y=478）本来有 2px 重叠，现改为提示 y=430 / 分隔线 y=452 / 按钮 y=460。

---

## 附录：源代码关键符号索引

| 符号 | 行号（GlassDock.cpp，2026-11-24 清理后 2793 行版本） | 说明 |
| --- | --- | --- |
| `Config` / `cfg` | 39 / 51 | 配置 |
| `LoadConfig` / `SaveConfig` / `SavePos` | 112 / 146 / 160 | |
| `InitFonts` / `HFontFor` | 222 / 212 | 字体 + GDI twin |
| `AlphaBitmapFromHIcon` | 396 | 图标透明重建 |
| `GetShellIcon` / `IconFor` | 525 / 541 | 图标获取/缓存 |
| `TextColorFor` / `TextColor` / `AccentSolid` / `AccentHi` | 576 / 582 / 583 / 586 | 颜色 |
| `DrawTextCrisp` | 603 | 文字统一入口 |
| `DrawTitle` / `DrawList` / `DrawScrollbar` / `DrawFrame` | 717 / 801 / 884 / 911 | 绘制 |
| `PaintAll` | 937 | 主窗口绘制总入口 |
| `OpenItem` / `Toggle` / `Reload` | 989 / 993 / 1002 | 动作 |
| `GetDesktopWorkerW` / `SitOnDesktop` | 1025 / 1045 | 桌面驻留 |
| `PickFolder` | 1080 | 文件夹选择器 |
| `GetPaintBitmap` / `CopyPaintToDIB` / `CommitLayered` / `Repaint` | 1150 / 1212 / 1232 / 1279 | 主窗口分层提交 |
| `GetSetPaintBitmap` / `CommitSetLayered` / `RepaintSet` | 1325 / 1333 / 1378 | 设置面板分层提交 |
| `PaintSettings` | 1547 | 设置面板绘制 |
| `SetHit` / `SetSliderFromX` | 1884 / 1898 | 设置面板命中/滑块 |
| `KeepOnScreen` | 1909 | 贴回可见区 |
| `OpenSettings` | 2182 | 打开设置 |
| `HitResize` / `SetCursors` | 2234 / 2267 | 缩放命中/光标 |
| `ShowContextMenu` | 2283 | 右键菜单 |
| `AddTrayIcon` / `HideWidget` / `ToggleWidget` / `ShowTrayMenu` | 2313 / 2344 / 2355 / 2373 | 托盘与显隐 |
| `WndProc` | 2390 | 主窗口消息循环 |
| `RunUninstall` | 2646 | 卸载 |
| `wWinMain` | 2686 | 入口 |
