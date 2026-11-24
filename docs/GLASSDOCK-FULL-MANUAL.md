# GlassDock 项目全文档（架构 / 运维 / 开发手册）

> **现状增补（2026-11-24 晚，P8–P11）**：本文主体写于 P7 交付时；以下为本轮视觉/交互迭代后的**当前事实**，与主体冲突处以本块为准：
> - 字体：系统图标字体 + **3x 超采样灰度抗锯齿**（`crisp::Rasterize`），边缘连续 alpha 渐变；字号 13 px（系统值 ×0.87）。
> - 布局：`TitleH()=S(30)`、`RowH()=S(26)`、`ListTop()=TitleH()+S(5)`；默认窗口 420×611。
> - 头部：**无计数 chip、无刷新按钮**；仅设置齿轮，与文件夹名同水平中线（`GetButtons` y=(TitleH−s)/2）。
> - 刷新：**实时**——`ReadDirectoryChangesW` 递归监听 + `WM_LIVE`(WM_APP+97)/`TIMER_LIVE`(4) 120 ms 去抖 → `LiveReload()`（保留展开目录与滚动）。
> - 设置面板：`SET_H=504`；**无"拖动刷新率"行**（固定 60 Hz，原位留白）；底部两行提示已删；分隔线 y=S(436)、按钮 y=S(448)。
> - 质感：`DrawFrame` 圆角裁剪的顶部 sheen + 底部环境阴影、边框 α100、内环 α26、内顶 catch-light / 内底 shade。
> - 截图：`WM_SNAP`(WM_APP+96) 导出真 alpha PNG（`%APPDATA%\GlassDock\snap_*.png`），README 截图由此与壁纸合成。
> - 逐轮明细见 `docs/P7_CHANGELOG.md` 的 P8 / P8.1 / P9 / P10 章节。

> **版本**：v2.0 · 2026-11-24
> **适用代码**：`src/GlassDock.cpp` **2839 行**、`src/crisptext.h` 281 行、`installer/setup.cpp` 131 行
> **事实来源**：本文所有行号、常量、默认值均从当前源码**重新提取**（不是从旧文档抄的）；与代码冲突时以代码为准
> **与其它文档的关系**：
> - `README.md` —— 面向**最终用户**的功能介绍（133 行）
> - `docs/PROJECT_GUIDE.md` —— 上一版技术交接（533 行，**行号与部分常量已过时**，以本文为准）
> - `docs/PLAN-2026-11-24.md` —— P0~P6 的**执行记录**（做了什么、怎么验证、偏差在哪）
> - `docs/DESKTOP-EMBED-ASSESSMENT-2026-11-24.md` —— 「窗口嵌入壁纸层（WorkerW）」的可行性**评估**
> - **本文** —— 把以上全部合并、校正、补全后的**单一总文档**

---

## 目录

| 章节 | 内容 |
|---|---|
| §0 | 文档约定与阅读路径 |
| §1 | 项目概览（是什么 / 不是什么 / 技术栈） |
| §2 | 用户手册（安装、操作、设置项、故障自救） |
| §3 | 文件与目录清单 |
| §4 | 系统架构（两个窗口 + 两条提交链） |
| §5 | 渲染管线（逐像素真透明） |
| §6 | 文字渲染（crisptext 子模块） |
| §7 | 图标系统 |
| §8 | 配置系统 |
| §9 | 主窗口：创建、消息、交互 |
| §10 | 设置面板：布局、命中、交互 |
| §11 | 桌面驻留机制 |
| §12 | 托盘 / 热键 / 单实例 |
| §13 | 安装器与卸载 |
| §14 | 构建系统 |
| §15 | 测试与验证工具链 |
| §16 | 已知问题与踩坑全表 |
| §17 | 常量速查 + 符号行号索引 |
| §18 | DPI 现状与升级路径 |
| §19 | 未完成路线图 |
| §20 | 版本与决策记录 |
| §21 | 附录（配置样例 / 构建产物 / 回滚点 / FAQ / 术语 / 修改指引） |

---

## §0 文档约定

- **行号**：全部指 `src/GlassDock.cpp` 当前 **2839 行**版本；`crisptext.h` 与 `setup.cpp` 单独标注。
- **两套坐标**：面板内部布局数值（`516`、`y=318`…）是**逻辑坐标**，实际像素 = `S(值) = 值 * g_scale`，`g_scale = GetDeviceCaps(LOGPIXELSX)/96`（本机 125% → 1.25）；但窗口尺寸 `cfg.width/height` 是**例外**，直接当设备像素（详见 §16.3）。
- **两个窗口**：主组件 `GlassDockWidget`、设置面板 `GlassDockSettings`，互相独立，各自一条 ULW 提交链。
- **"活代码 / 死代码"**：文中明确标注；死代码是历史残留，**不要因为看不懂就删**（有的留着是为了编译期兼容或未来接回）。

**推荐阅读路径**

| 你是 | 先读 |
|---|---|
| 只想用 | §2 |
| 要改 UI / 布局 | §5.4 §10.2 §17 |
| 要改渲染 / 加效果 | §5 §6 §16 |
| 要发版 / 打包 | §13 §14 §15 |
| 要排查"窗口不见了" | §11 §12 §16 |
| 要接手全部 | §0 → §21 顺序读 |

**症状速查（先看这张再翻全文）**

| 症状 | 去哪 |
|---|---|
| 组件不见了 / 被埋到壁纸后 | §2.4 §11.1 §11.3 §12.2 |
| 字太小 / 字发虚 / 和资源管理器不一致 | §6.1 §6.5 §16.2 |
| 浅色壁纸上文字看不清 | §16.4 §19.2 |
| 点不到组件底下的桌面图标 | §16.1 |
| 设置面板控件重叠 / 底部穿字 | §10.2 §15.9 |
| 编译报错 / 运行 0xC0000xxx | §14.6 |
| 安装、卸载、注册表残留 | §13 |
| 高 DPI 比例不对 / 多屏变形 | §18 §16.3 |
| 想嵌到"图标之下" | §11.2 §11.4 §19.4 §19.5 |
| 想改配色 / 边框 / 圆角 | §9.9 §10.7 |

---

## §1 项目概览

### 1.1 一句话

GlassDock 是一个 **Windows 桌面文件夹小组件**：把任意文件夹的内容以可展开列表贴在桌面上，双击用系统默认程序打开；面板是**逐像素真透明的玻璃**，桌面壁纸（含动态壁纸）直接透出。

### 1.2 它解决的问题

传统桌面组件要么"假透明"（截一张壁纸再模糊，壁纸一动就穿帮），要么"浮在应用之上"（挡工作窗口），要么"用颜色键抠图"（边缘洋红毛边）。GlassDock 走的是第三条路：**系统级逐像素 alpha**。

### 1.3 能力清单（用户视角）

| 能力 | 说明 | 实现位置 |
|---|---|---|
| 文件夹列表 | 指向任意文件夹；文件夹优先排序；可展开子目录 | `LoadRoot` 406 / `LoadChildren` 364 / `DrawList` 847 |
| 双击打开 | `ShellExecuteW("open")`，与资源管理器一致 | `OpenItem` 1035 |
| 真透明玻璃 | 32bpp 预乘 ARGB + `UpdateLayeredWindow(ULW_ALPHA)` | `CommitLayered` 1278 |
| 只在桌面层 | 不置顶、不抢焦点、`Win+D` 后仍在 | `SitOnDesktop` 1091 / `WNDPROC` 2436 |
| 文字不发虚 | GDI 灰度抗锯齿 + 覆盖率合成 + 1px 柔和阴影 | `DrawTextCrisp` 649 / `crisptext.h` |
| 全参数可调 | 宽 240–1000、高 240–1200、不透明度 0–100%、6 预设色 + 取色器 + 十六进制 | `PaintSettings` 1593 / `SetSliderFromX` 1944 |
| 实时预览 | 改滑块立刻生效，无需保存 | `SetApplyLive` 1971 |
| 独立设置窗口 | 同一套玻璃面板，可拖动、失焦降级 | `OpenSettings` 2228 |
| 托盘 / 热键 / 单实例 | `Ctrl+Alt+G` 唤回；二次启动不新开进程 | §12 |
| 一键安装 / 卸载 | 装到 `D:\GlassDock`，写卸载项 | `installer/setup.cpp` |

### 1.4 非目标（明确不做）

- 不是壁纸引擎（不接管壁纸、不做视频壁纸）
- 不是文件管理器（无复制/删除/重命名/搜索）
- 不是 Dock 栏替代品（不管理运行中的应用）
- 不做云同步、不做账号体系

### 1.5 技术栈与体量

| 项 | 值 |
|---|---|
| 语言 / 标准 | C++17（MinGW-w64 g++ 16.2.0） |
| 图形 | GDI+（绘制）+ GDI（文字） |
| 窗口 | Win32（无 MFC/Qt/.NET） |
| 运行时依赖 | **无**（`-static`，单文件 exe） |
| 主程序体积 | 532,480 B（约 520 KB） |
| 安装包体积 | 972,288 B（约 950 KB） |
| 常驻内存 | 约 20 MB（README 实测口径） |
| 目标系统 | Win10 / Win11（x64） |

### 1.6 标识信息

| 项 | 值 |
|---|---|
| 窗口类名 | `GlassDockWidget` / `GlassDockSettings` |
| 互斥体 | `Local\GlassDock_SingleInstance` |
| 配置目录 | `%APPDATA%\GlassDock\config.ini` |
| 安装目录 | `D:\GlassDock`（无 D 盘时退 `%LOCALAPPDATA%\Programs\GlassDock`） |
| 卸载注册表 | `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\GlassDock` |
| 自启注册表 | `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\GlassDock` |
| 资源 | 图标 id `1`；安装器内嵌 `100`(exe) / `101`(manifest) / `1`(icon) |

---

## §2 用户手册

### 2.1 安装

1. 运行 `release/GlassDock-Setup-1.0.0.exe`
2. 安装器解出 `GlassDock.exe` + `GlassDock.exe.manifest` 到 `D:\GlassDock`
3. 创建桌面与开始菜单快捷方式，写卸载项
4. 弹窗询问是否立即启动

**卸载**：设置 → 应用 → GlassDock → 卸载（等价 `GlassDock.exe --uninstall`）。

### 2.2 操作表

| 操作 | 效果 |
|---|---|
| 拖动标题栏 | 移动（松手写回 `x/y`） |
| 拖动右下角 | 缩放（仅右/下/右下；超屏自动贴回） |
| 双击文件/文件夹 | 默认程序打开（文件夹为"打开资源管理器"） |
| 点文件夹左侧三角 | 展开 / 收起 |
| 滚轮 | 列表滚动（每格 `S(60)`，即 125% 下 75px） |
| 右键 | 菜单：选择文件夹 / 设置 / 刷新背景 / 开机自启 / 隐藏到托盘 / 退出 |
| 标题栏图标 | 仅设置齿轮（与文件夹名同一水平中线）；**无刷新按钮**（P9 起改为目录实时监听）、**无关闭按钮** |
| `Ctrl+Alt+G` | 全局热键：确保显示并锚回桌面层（不置顶、不隐藏） |
| 托盘左键/双击 | 显示（措辞历史上写"显示/隐藏"，实际只显示） |
| 托盘右键 | 菜单：显示 / 设置 / 退出 |

### 2.3 设置项

| 分区 | 控件 | 范围 / 选项 | 默认 |
|---|---|---|---|
| 文件夹 | 卡片 + 「更改…」 | 任意目录 | 桌面 |
| 外观 | 宽度滑块 | 240–1000（**设备像素**） | 420 |
| | 高度滑块 | 240–1200（**设备像素**） | 560 |
| | 不透明度滑块 | 0–100% | 36 |
| 背景色 | 6 预设 | `808080` `1E1E1E` `F5F5F5` `2B6CB0` `1FA2A6` `6B5BD2` | `808080` |
| | 十六进制框 | `#RRGGBB`，输满即应用 | — |
| | 取色器 | 系统 `ChooseColorW` | — |
| 选项 | 开机自启 | 勾选写 `Run` 键 | 关 |
| | 拖动刷新率 | **固定 60 Hz**（P10 起选项移除，面板原位留白） | 60 |
| 底部 | 恢复默认 / 完成 | — | — |

### 2.4 找不到组件时的自救顺序

1. `Ctrl+Alt+G`
2. 托盘图标（Win11 在 `^` 溢出面板里）
3. 再双击一次快捷方式（单实例会 `WM_SHOWME` 叫回已有窗口）
4. 托盘右键 → 退出 → 重新启动（配置与位置保留）

> 说明：`Win+D` 会把组件一起最小化，程序收到 `SIZE_MINIMIZED` 会立刻 `ShowWindow` + `SitOnDesktop` 复原（2457–2464）。

---

## §3 文件与目录清单

### 3.1 源码与资源

| 文件 | 行数 | 字节 | 职责 |
|---|---|---|---|
| `src/GlassDock.cpp` | **2839** | 121,154 | 主程序全部逻辑（单文件） |
| `src/crisptext.h` | 281 | 12,191 | 文字栅格化 / 合成 / 阴影 |
| `src/resource.rc` | 1 | 40 | `1 ICON "D:\Doubao\GlassDock\app.ico"` |
| `src/app.manifest` | 29 | 1,357 | DPI + Common Controls v6 + longPath + asInvoker |
| `installer/setup.cpp` | 131 | 5,138 | 自解压安装器 |
| `installer/setup.rc` | — | — | 内嵌 exe/manifest/icon 资源 |
| `build.ps1` | 51 | 2,051 | 六步构建脚本 |
| `app.ico` | — | 111,881 | 应用图标（多尺寸 ico） |
| `LICENSE` | 21 | 1,066 | MIT |
| `README.md` | 133 | 10,267 | 用户向说明 |
| `docs/PROJECT_GUIDE.md` | 533 | 34,228 | 旧技术交接（行号已过时） |
| `docs/PLAN-2026-11-24.md` | 287 | 20,976 | P0–P6 执行记录 |
| `docs/DESKTOP-EMBED-ASSESSMENT-2026-11-24.md` | 507 | 27,464 | 壁纸层嵌入评估 |
| `docs/GLASSDOCK-FULL-MANUAL.md` | ≈1,650 | ≈82 KB | **本文** |
| `docs/screenshot.png` / `docs/settings.png` | — | — | README 用截图（**旧版**，含已删的模糊滑块） |

### 3.2 构建产物

| 路径 | 字节 | 说明 |
|---|---|---|
| `GlassDock.exe` | 532,480 | 主程序（`-O2 -s -static`） |
| `GlassDock.exe.manifest` | — | 构建时从 `src/app.manifest` 复制 |
| `installer/setup.exe` | 972,288 | 安装器（内嵌上面两个文件） |
| `release/GlassDock-Setup-1.0.0.exe` | 972,288 | 对外分发件（`setup.exe` 的副本） |

### 3.3 `tools/`（开发探针，**不属于产品**）

历史调试用的小程序与截图：`pwcap.cpp`/`pwcap2.cpp`（按类名 PrintWindow 抓图）、`textprobe*.cpp`（文字渲染对比）、`iconprobe.cpp`、`crisptest.cpp`、`simulate.cpp`、`zhit*.cpp`（命中测试）、`zprobe*.cpp`/`zacrylic*.cpp`（z 序与亚克力实验）、`makeicon.cpp`、大量 `*.bmp/png/mp4/bin`。

`.gitignore` 已整体排除 `tools/`、`build/`、`release/`、`v.png`、`HANDOFF.md`、`.workbuddy/`。

### 3.4 本文新增的验证工具（在 `build/`）

| 文件 | 用途 |
|---|---|
| `build/shot_settings.cpp` / `shot.exe` | 离屏渲染真源码 + 无头命中断言 |
| `build/compare.ps1` | 两图逐像素比对 + 文本色像素统计 |
| `build/fonttest.cpp` / `fonttest.exe` | 三种字体同尺寸对比（Segoe UI / YaHei UI / YaHei） |
| `build/embedprobe.cpp` / `embedprobe.exe` | WorkerW / Progman 嵌入可行性探针 |
| `tools/pwcap2.exe` | 按窗口类名抓真实窗口 |

---

## §4 系统架构

### 4.1 总体：两个窗口，两条提交链

```
                    ┌──────────────────────────────┐
       用户输入 ──▶ │ GlassDockWidget  (WndProc 2436)│
                    │  WM_LBUTTONDOWN / MOUSEMOVE …  │
                    └──────────────┬───────────────┘
                                   │ 状态改变 (cfg / scroll / rows)
                                   ▼
                    ┌──────────────────────────────┐
                    │ Repaint 1325 → CommitLayered  │
                    │ GetLayeredSurface 1222        │
                    │ GetPaintBitmap  1196          │
                    │ PaintAll         983          │
                    │ CopyPaintToDIB   1258         │
                    │ UpdateLayeredWindow(ULW_ALPHA)│
                    └──────────────────────────────┘

                    ┌──────────────────────────────┐
       设置窗口 ──▶ │ GlassDockSettings (SettingsProc│
                    │  2040)                        │
                    └──────────────┬───────────────┘
                                   ▼
                    │ RepaintSet 1424 → CommitSetLayered 1379
                    │ PaintSettings 1593 → UpdateLayeredWindow
```

两条链**完全同构但互不影响**：主窗口的 surface 是 `g_ls`/`g_paint`，设置面板是 `g_setLs`/`g_setPaint`（各自一套 DIB + Bitmap）。

### 4.2 为什么选"逐像素透明"，而不是别的

| 方案 | 原理 | 致命问题 | 本项目是否采用 |
|---|---|---|---|
| 截屏 + 高斯模糊 | 抓桌面图像 → 模糊 → 当背景画 | 壁纸一动就穿帮；动态壁纸是黑的；每次都截屏吃 CPU | ❌（旧方案，已删） |
| 颜色键抠图 | `SetLayeredWindowAttributes(colourKey)` | 边缘必然有洋红/锯齿毛边，半透明做不到 | ❌ |
| DWM 亚克力 / Mica | `DwmSetWindowAttribute(SYSTEMBACKDROP_TYPE)` | 只能跟随系统材质，无法自定义浓度/颜色，且 Win10 不支持 | ❌ |
| **逐像素 alpha** | `WS_EX_LAYERED` + PARGB 位图 + `ULW_ALPHA` | 需要自己处理预乘、命中测试、repaint | ✅ **当前** |

### 4.3 进程生命周期（`wWinMain` 2732）

| 行 | 动作 | 说明 |
|---|---|---|
| 2734 | `SetProcessDPIAware()` | 系统级 DPI 感知（非 per-monitor，见 §18） |
| 2741 | `LoadConfig()` | 读 `%APPDATA%\GlassDock\config.ini`，钳位 |
| 2750 | `g_scale = GetDeviceCaps(LOGPIXELSX)/96` | 最小 1.0 |
| 2762 | `CreateMutexW(Local\GlassDock_SingleInstance)` | 已存在 → `FindWindow(GlassDockWidget)` + `PostMessage(WM_SHOWME)` + `return 0` |
| 2769 | `InitFonts()` | 解析系统字体 + 建 GDI twin |
| 2770 | `LoadRoot()` | 载入文件夹树 |
| 2796 | `CreateWindowExW(...)` | `WS_EX_TOOLWINDOW\|NOACTIVATE\|LAYERED`，`WS_POPUP`，尺寸 = `cfg.width × cfg.height`（**设备像素**） |
| 2805 | `RegisterHotKey(Ctrl+Alt+G)` | 失败无害（被占用时） |
| 2807 | `AddTrayIcon()` | `Shell_NotifyIcon(NIM_ADD)` |
| 2808 | `SetTimer(TIMER_BG, 900ms)` | 真透明模式下**空转**（历史遗留） |
| 2809 | `SetTimer(TIMER_GUARD, 1000ms)` | 桌面守卫 |
| 2812 | `SitOnDesktop()` | 锚到桌面层 |
| — | `GetMessage` 循环 | 无焦点也在跑 |

### 4.4 线程模型

**单线程**。所有绘制、消息、定时器都在 UI 线程。没有工作线程，因此：

- 好处：无锁、无竞态，`g_paintDst` 这类全局状态安全；
- 代价：`LoadChildren`（`FindFirstFileW`）在 UI 线程同步执行，超大目录（>5000 项）会造成一次卡顿；
- 定时器回调里**不做重活**（`TIMER_BG` 空转、`TIMER_GUARD` 只做 `SitOnDesktop` + `Repaint`）。

---

## §5 渲染管线（逐像素真透明）

### 5.1 数据流

```
GDI+ Bitmap (PixelFormat32bppPARGB)
        │  Graphics g(paint); g.Clear(0,0,0,0)
        ▼
   PaintAll(g, w, h)           ← 画透明底、tint、列表、滚动条、标题、边框
        │  （文字不在这里画：DrawTextCrisp 直接写像素）
        ▼
   Bitmap 已预乘（PARGB）
        │  LockBits(Read, PARGB) → memcpy
        ▼
   32bpp top-down DIB section (CreateDIBSection, biHeight = -h)
        │  alpha==0 → 钳成 1（仅此一步，不做颜色乘法）
        ▼
   UpdateLayeredWindow(hwnd, hdcScreen, &ptDst, &size, hdcSrc, &ptSrc, 0, &BLENDFUNCTION{AC_SRC_OVER,0,255,AC_SRC_ALPHA}, ULW_ALPHA)
```

### 5.2 `PaintAll`（983）逐层

| 步 | 代码 | 说明 |
|---|---|---|
| 1 | 984–991 | `SmoothingModeAntiAlias` / `InterpolationModeHighQualityBicubic` / `PixelOffsetModeHighQuality` / `TextRenderingHintAntiAliasGridFit`（**灰度 AA，不是 ClearType**） |
| 2 | 998–999 | `SolidBrush(Color(0,0,0,0))` 铺满 → **背景 alpha=0**，这是"真透明"的根 |
| 3 | 1007–1015 | 若 `cfg.opacity > 0`：圆角路径（`S(12)`）填 `Color(opacity*255/100, accent)` 半透明 tint |
| 4 | 1017 | 磨砂颗粒已删除（原「模糊强度」） |
| 5 | 1019–1022 | `DrawList` 847 → `DrawScrollbar` 930 → `DrawTitle` 763 → `DrawFrame` 957 |
| 6 | 1025–1031 | 右下角三条斜线，提示"可缩放" |

### 5.3 `CommitLayered`（1278）六步

| 步 | 函数 | 行 | 细节 |
|---|---|---|---|
| 1 | 定尺寸 | 1284 | resize 中用 `cfg.width/height`，否则 `GetClientRect` |
| 2 | `GetLayeredSurface(w,h)` | 1222 | 复用 32bpp top-down DIB；尺寸变化才重建（`FreeLayeredSurface` 1210） |
| 3 | `GetPaintBitmap(w,h)` | 1196 | 复用 PARGB GDI+ 位图 |
| 4 | 绘制 | 1288–1300 | `Graphics g(paint)` → `Clear(0,0,0,0)` → `g_paintDst = paint` → `PaintAll` → `g_paintDst = NULL` → `Flush` |
| 5 | `CopyPaintToDIB` | 1258 | `LockBits(Read, PARGB)` + memcpy；**alpha 0 → 1**（见 §16 坑 5） |
| 6 | `UpdateLayeredWindow` | 1317（主）/ 1418（设置） | resize 中 `ptDst = g_resizePos`（原子移动+缩放防重影），否则 `GetWindowRect` |

> `g_paintDst` 是全局指针：crisptext 直接往这块 PARGB 位图**写像素**，所以必须在 `PaintAll` 期间有效、结束后立刻置空。

### 5.4 死代码：旧 GDI 双缓冲

- `struct BackBuffer` + `g_bbMain, g_bbSet`（1148）
- `GetBackBuffer(BackBuffer&, HDC, w, h)`（1151）—— **零调用**，`-Wall` 会报 `-Wunused-function`
- 只在退出时清理

保留原因：无（属于 P1 清理时"新发现但未处理"的残留，列在路线图里）。

### 5.5 设置面板的同构实现

| 主窗口 | 设置面板 |
|---|---|
| `g_ls` / `g_paint` | `g_setLs` / `g_setPaint` |
| `GetLayeredSurface` 1222 | `GetSetLayeredSurface` 1347 |
| `GetPaintBitmap` 1196 | `GetSetPaintBitmap` 1371 |
| `CommitLayered` 1278 | `CommitSetLayered` 1379 |
| `Repaint` 1325 | `RepaintSet` 1424 |
| `FreeLayeredSurface` 1210 | `FreeSetLayeredSurface` 1336 |
| `PaintAll` 983 | `PaintSettings` 1593 |

> 设置面板的**背景是固定色** `Color(51,30,30,32)`（约 20% 深灰），不跟随主窗口的 accent / opacity。

### 5.6 一个必须知道的坑：双重预乘

- GDI+ 往 32bpp 位图绘制时，**输出的 RGB 已经是预乘值**（`PixelFormat32bppPARGB`）。
- `UpdateLayeredWindow` 要求的也正是**预乘**数据。
- 所以下游**绝不能再乘一次 alpha**，否则边缘会被压暗成灰边。
- 本项目的做法：全程 PARGB 渲染 + `LockBits(PARGB)` 直接取 + 只做 alpha 钳位，不做任何颜色乘法。

---

## §6 文字渲染（crisptext 子模块）

### 6.1 为什么不用 GDI+ / 不用 ClearType

| 渲染器 | 实心笔画像素占比 | 边缘过渡宽度 |
|---|---|---|
| GDI+ AntiAliasGridFit @15px | **6.8%** | 1.98 px |
| GDI 灰度 AA @15px | **54.7%** | 1.08 px |

GDI+ 在 12–15px 的中文笔画（细于 1 设备像素）会把一根竖笔劈成两半灰像素 —— 这就是"发虚"。

ClearType 也不能用：RGB 亚像素一旦预乘进逐像素 alpha 表面，会变成**彩色毛边**（红蓝镶边），在深色壁纸上尤其明显。

结论：**GDI 灰度抗锯齿**（`ANTIALIASED_QUALITY`），配合自建覆盖率合成，观感等同系统桌面标签。

### 6.2 API 一览（`src/crisptext.h`）

| 符号 | 行 | 作用 |
|---|---|---|
| `namespace crisp` | 39 | 全部实现 |
| `HAlign` / `VAlign` | 42 / 43 | 三档对齐 |
| `Scratch` / `EnsureScratch` / `FreeScratch` | 46 / 58 / 52 | 可复用临时 DIB（避免每次分配） |
| `MakeHFont(family, px, bold, italic)` | 90 | 建带 `ANTIALIASED_QUALITY` 的 HFONT |
| `Rasterize(hf, text, boxW, boxH, hAlign, vAlign, ellipsis, cov, &w, &h, &ox, &oy)` | 112 | 黑底白字栅格化 → 逐像素取 **R 通道 = 覆盖率**，同时回传实际宽高与偏移 |
| `Composite(bmp, ox, oy, cov, w, h, r,g,b)` | 176 | 用覆盖率做 source-over，直接写 PARGB |
| `MeasureWidth(hf, text, boxW, ellipsis)` | 217 | 用 `DT_CALCRECT` 量宽（与 `DrawTextW` 一致） |
| `Draw(bmp, hf, text, boxX, boxY, boxW, boxH, hAlign, vAlign, ellipsis, r,g,b)` | 238 | 单色一次性绘制（栅格化 + 合成） |
| `DrawWithShadow(...)` | 256 | 阴影 + 主字形，共用同一 coverage |

### 6.3 阴影策略（关键设计）

- **不用 8 向描边**：描边是"在原位置画偏移副本"，会把 2–3px 细笔画的**内部**也涂黑（字母 P 的竖笔会糊成实心块）。
- **用单方向 1px 柔和阴影**：`DrawWithShadow` 复用同一份 coverage，按 `shadowAlpha` 阻尼后偏移绘制，细笔画内部不会被二次覆盖。
- 参数（`DrawTextCrisp` 649）：偏移 `(1,1)`、`shadowAlpha = 85`（≈33%）、颜色按主字亮度取反（浅字配黑阴影，深字配白阴影）。

### 6.4 统一入口 `DrawTextCrisp`（649 / 697 重载）

流程：

1. 从 `Brush` 取颜色；
2. `HFontFor(Font*)`（212）找 GDI twin；**找不到或 `g_paintDst` 为空** → 回退 `g.DrawString`（GDI+ 兜底，保证设置面板初始化等场景不崩）；
3. 把 `StringFormat` 的 align/trimming 映射成 `crisp::HAlign/VAlign/ellipsis`；
4. 调 `crisp::DrawWithShadow`。

### 6.5 字体来源（`ReadSystemLabelFont` 227）

优先级：

1. `HKCU\Control Panel\Desktop\WindowMetrics\IconFont`（`REG_BINARY`，92 字节 LOGFONTW）
2. `SPI_GETICONTITLELOGFONT`
3. `SPI_GETNONCLIENTMETRICS.lfMessageFont`
4. 全失败 → `px = 12`、face = `Segoe UI`

**单位陷阱**（踩过一次）：

| 来源 | 单位 | 处理 |
|---|---|---|
| 注册表 `IconFont` | **96dpi 逻辑单位**（本机 `-12`） | 必须 `px *= g_scale` → 125% 下 15px |
| SPI 系列 | **已是设备像素** | 不再乘 |

`TrimFace`（258）会剥掉 face 名尾部空格 —— 本机注册表值确实是 `"Microsoft YaHei "`（带空格），不剥会导致 `FontFamily` 构造失败并**静默退回 Segoe UI**。

### 6.6 本机字体实测（2026-11-24）

| 来源 | face | lfHeight | 折合设备像素 |
|---|---|---|---|
| `IconFont`（桌面标签） | Microsoft YaHei | -12 | 15 |
| `lfMessageFont`（消息框/UI） | Microsoft YaHei UI | -15 | 15 |
| `Segoe UI`（24H2 的 Explorer 列表西文） | Segoe UI | — | 9pt→15 |

三种方式在本机**视觉高度一致**（15px），差异只在字面：`Segoe UI` 无 CJK 字形（`GetGlyphIndicesW` 返回 `0xFFFF`，靠 GDI 字体链接兜底），`YaHei` 与 `YaHei UI` 度量相同、笔画略有差别。

实测数据：换字体前后同一窗口抓图，**235200 像素中仅 77 个不同、最大差 1/255**，说明"换 face"本身几乎不可见，真正会被用户看见的是**字号算错**（曾把 -12 当像素用，小了一号）。

### 6.7 MinGW 兼容性（四条）

1. `GdipDrawImageRectRectI` 在 MinGW 里被声明成 4 个独立 `int`（与真实 `GpRect*` 不符）→ **不用** GDI+ 的 DrawImage(Rect) 重载，改手写像素。
2. `ImageLockMode` 只暴露 `Read`/`Write`，没有 `ReadWrite` → 用 `(ImageLockMode)3` 强转。
3. `PixelFormat32bppPARGB` 是宏，不能加 `Gdiplus::` 前缀。
4. `Graphics::Clear()` 在部分驱动上不产出真正透明的 PARGB → 必须 `Clear(Color(0,0,0,0))` 配合 PARGB 位图（已实测有效）。

---

## §7 图标系统

### 7.1 为什么不能直接 `Bitmap::FromHICON`

`FromHICON` 会把 1-bit AND mask 里的"透明"像素压成**不透明黑** → 图标带黑框。

### 7.2 正确链路（`AlphaBitmapFromHIcon` 442）

```
SHGetFileInfo(path, 0, &sfi, sizeof, SHGFI_SYSICONINDEX | SHGFI_LARGEICON)
        → sfi.iIcon（系统图像列表索引）
ImageList_GetIcon(himl, iIcon, ILD_TRANSPARENT)   → 32bpp HICON
        → GetIconInfo → 取 hbmColor / hbmMask
        → 若 hbmColor 自带 alpha 通道：直接用
        → 否则用 AND mask 重建 alpha
        → 对完全透明的像素做"颜色扩散"（把邻近不透明像素的颜色填进去）
        → 生成 PixelFormat32bppPARGB 位图
```

> 颜色扩散这一步是给**缩放**用的：双线性插值时若不扩散，透明像素的黑色会渗进边缘形成黑边。

### 7.3 缓存（按**扩展名**，不是按路径）

| 函数 | 行 | 说明 |
|---|---|---|
| `GetShellIcon(path, isDir)` | 571 | `SHGetFileInfoW(..., SHGFI_SYSICONINDEX\|SHGFI_LARGEICON)` → `ImageList_GetIcon` → `AlphaBitmapFromHIcon`；失败则 `Bitmap::FromHICON` 兜底 |
| `IconFor(Item*)` | 587 | 缓存 key = **`<dir>`**（文件夹）或**小写扩展名**（文件），所以 `.md`/`.py`/`.txt` 各占一个条目、同扩展名共用一个位图 |
| `FreeIcons()` | 596 | 退出时统一释放 |

> 因为是按扩展名缓存，**同名不同扩展名、或者系统里同图标的不同扩展名**会各缓存一份（内存换简单）；目录统一用同一个文件夹图标，不区分空/非空。

---

## §8 配置系统

### 8.1 `Config` 结构（39–50）

| 字段 | 类型 | 默认（结构体） | 实际加载默认 | 范围 / 说明 |
|---|---|---|---|---|
| `folder` | wstring | 空 | 桌面（`CSIDL_DESKTOPDIRECTORY`） | 展示目录 |
| `width` | int | 420 | 420 | 200–1200（加载钳位）；滑块 240–1000 |
| `height` | int | 560 | 560 | 160–1600（加载钳位）；滑块 240–1200 |
| `x` / `y` | int | -1 / -1 | -1 | -1 = 首次启动用默认位置 |
| `opacity` | int | 36 | 36 | 0–100，tint alpha = `opacity*255/100` |
| `accent` | COLORREF | `RGB(128,128,128)` | `808080` | 背景色调 |
| `topmost` | bool | `true` | **恒 `false`**（128 行强制） | 历史字段，`SaveConfig` 写 0 |
| `autostart` | bool | false | 0 | 写 `Run` 键 |
| `foldersFirst` | bool | true | 1 | 排序：文件夹优先 |
| `showExt` | bool | true | 1 | 是否显示扩展名（`DisplayName` 处理） |
| `refreshHz` | int | 60 | 60 | 30–240，六档 UI |

### 8.2 存储

- 路径：`%APPDATA%\GlassDock\config.ini`（`AppDataDir` 69 用 `GetEnvironmentVariableW("APPDATA")`）
- 节：`[GlassDock]`
- API：`GetPrivateProfileStringW` / `WritePrivateProfileStringW`（封装见 75–96）
- 键：`folder width height x y opacity accent topmost autostart foldersFirst showExt refreshHz`
- 历史残留键 `blur`：**不再读写**，老用户 ini 里存在也无害

### 8.3 读写时机

| 时机 | 调用 | 行 |
|---|---|---|
| 启动 | `LoadConfig` | 2741 |
| 任意设置改动 | `SaveConfig`（带动 `SavePos`） | 146 / 160 |
| 退出 / 移动结束 | `SavePos` | 160 |
| 勾选自启 | `ApplyAutoStart` | 167 |

### 8.4 自启动实现（167–181）

```
cfg.autostart == true  → RegSetValueExW(HKCU\...\Run, "GlassDock", REG_SZ, "\"<exe绝对路径>\"")
cfg.autostart == false → RegDeleteValueW(HKCU\...\Run, "GlassDock")
```

> 注意：**启动时一定会调用** `ApplyAutoStart()`，因此当该键不存在且 `autostart=false` 时，这是一次"删除一个不存在的值"的**非操作**——这也是隔离测试可以放心跑的原因。

### 8.5 测试隔离配方（重要）

`AppDataDir()` 读环境变量，因此可以完全隔离：

```powershell
$env:APPDATA = 'D:\Doubao\GlassDock\build\appdata_xxx'
Start-Process 'D:\Doubao\GlassDock\GlassDock.exe' -WorkingDirectory 'D:\Doubao\GlassDock'
```

配合"先杀掉所有 `GlassDock*.exe`"（单实例互斥体是 `Local\` 级，跨进程唯一），即可在不碰真实配置、不碰注册表、不碰生产 exe 的前提下做完整验证。

---

## §9 主窗口：创建、消息、交互

### 9.1 创建（2796）

```cpp
g_hwnd = CreateWindowExW(
    WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
    L"GlassDockWidget", L"GlassDock", WS_POPUP,
    x, y, cfg.width, cfg.height, NULL, NULL, hInst, NULL);
```

| 项 | 值 | 理由 |
|---|---|---|
| `WS_POPUP` | 无边框、无标题栏 | 自绘标题栏 |
| `WS_EX_TOOLWINDOW` | 不进 Alt-Tab、不进任务栏 | 桌面组件不该出现在切换列表 |
| `WS_EX_NOACTIVATE` | 点它不抢焦点 | 点完桌面仍可继续操作其它图标 |
| `WS_EX_LAYERED` | 逐像素 alpha 前提 | 配合 `UpdateLayeredWindow` |
| 尺寸 | **`cfg.width × cfg.height`，直接当设备像素用**（**不乘 `S()`**） | 与设置里的数值 1:1 对应，见 §16 坑 21 |
| 首次位置 | `cfg.x/y == -1` 时 `x = 工作区.right - cfg.width - S(24)`、`y = 工作区.top + S(60)`（本机 2560×1440@125% → 2110,75） | 不挡桌面图标主区 |
| 圆角 | `RoundCorners` 322 → `DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_DONOTROUND` | 面板自己画 `S(12)` 圆角；让 DWM 再切一次会错位 |

### 9.2 几何常量

| 函数 | 行 | 值（逻辑） | 说明 |
|---|---|---|---|
| `TitleH()` | 602 | 36 | 标题栏高 |
| `RowH()` | 603 | 28 | 每行高 |
| `LeftPad()` | 604 | 12 | 列表左内边距 |
| `Indent()` | 605 | 16 | 每级缩进 |
| `ResizeCornerRect` | 616 | 18×18 | 右下热区 |
| 最小尺寸 | — | 200×160 | resize 下限 |

### 9.3 消息表（`WndProc` 2436）

| 消息 | 行 | 行为 |
|---|---|---|
| `WM_ERASEBKGND` | 2438 | `return 1`（禁止擦背景，防闪烁） |
| `WM_TRAYICON` | 2440 | 左键/双击 → `ToggleWidget`；右键 → `ShowTrayMenu` |
| `WM_SHOWME` | 2444 | 第二个实例发来的唤回 → `ShowWidget` |
| `WM_HOTKEY` | 2447 | `Ctrl+Alt+G` → `ToggleWidget` |
| `WM_SETTINGCHANGE` | 2450 | 主题/壁纸变了 → `RenderBackground()` + 后续重绘 |
| `WM_ACTIVATE` | 2454 | 吞掉，永不激活 |
| `WM_SIZE` | 2457 | 若被最小化 → `ShowWindow(SW_SHOW)` + `SitOnDesktop` + `Repaint` |
| `WM_TIMER` | 2465 | `TIMER_BG`(1) 空转；`TIMER_GUARD`(3) 桌面守卫（§11.3） |
| `WM_NCHITTEST` | 2486 | **恒 `HTCLIENT`**（关键，见 §16 坑 4） |
| `WM_PAINT` | 2493 | `Repaint()` → 提交分层表面 |
| `WM_LBUTTONDOWN` | 2502 | 判定：关闭/按钮/缩放角/标题拖动/行点击 |
| `WM_MOUSEMOVE` | 2543 | 悬停高亮 + 拖动/缩放 + `TrackMouseEvent` |
| `WM_MOUSELEAVE` | 2599 | 清空 hover |
| `WM_LBUTTONUP` | 2605 | 结束拖动/缩放 → `SavePos` |
| `WM_LBUTTONDBLCLK` | 2633 | `OpenItem` |
| `WM_MOUSEWHEEL` | 2649 | `g_scroll` 步进 + `ClampScroll` |
| `WM_RBUTTONUP` | 2657 | `ShowContextMenu` |
| `WM_COMMAND` | 2663 | 菜单分支 2665–2675 |
| `WM_CLOSE` | 2678 | `DestroyWindow` |
| `WM_DESTROY` | 2681 | 收尾（见 §12.5） |

### 9.4 命中判定顺序（`WM_LBUTTONDOWN` 2502）

1. `HitResize`（2280）→ 只在 `RS_E / RS_S / RS_SE` 生效，进缩放态（记 `SetCapture` + **屏幕坐标**基准，避免边拖边移时抖动）；
2. `HitButton`（2307）→ 0=刷新、1=设置（按下只记 `g_pressedBtn`，抬起且仍在按钮上才执行）；
3. `y < TitleH()` → 进拖动态（`g_dragging = true`，记 `g_dragOff` 屏幕偏移）；
4. `HitRow`（2300）→ 若命中行是文件夹且落在三角热区（宽 `S(16)`）→ **按下即** `Toggle` 展开/收起；
5. 文件/普通行的"打开"发生在 `WM_LBUTTONDBLCLK`（2633）→ `OpenItem`；**单击不打开、也不选中**。

### 9.5 标题栏按钮（`GetButtons` 607）

```
s = S(22), gap = S(4), rm = S(8)
[设置] x = w - rm - s          → {x, 7, x+s, 7+s}
[刷新] x -= (s + gap)          → {x, 7, x+s, 7+s}
[关闭] 恒 {0,0,0,0}            ← 故意不存在
```

> 第三个槽位保留但永远为空：`GetButtons(int w, RECT out[3])` 的签名是历史遗留，`HitButton` 只遍历前两个。

### 9.6 拖动与缩放

- 拖动：`g_dragging`（2260）+ `g_dragOffset`，`WM_MOUSEMOVE` 里 `SetWindowPos`，抬起 `SavePos`。
- 缩放：`g_resizing` + `g_resizePos`（1208）/`g_resizeHasPos`（1207）；**用 `UpdateLayeredWindow` 的 `pptDst` 同步搬位置**，避免"窗口先动、内容后动"的重影。
- 方向：`HitResize` 2280 计算 8 向，但**只允许 `E / S / SE`**（左上不可拉，防止标题栏被拖出屏幕）。
- 贴边：`KeepOnScreen` 1955 保证窗口至少有 `S(80)` 可见。

### 9.7 刷新率的真实现状（重要）

`cfg.refreshHz`（30–240）→ `g_liveBgInterval`（4–33ms），代码里**只有赋值、没有任何读取**（29 / 2097 / 2744 三处赋值；`g_liveBgInterval` 无消费点）。原因：真透明模式不再需要"重建背景快照"，这个节流器失去了作用对象。

**结论**：该设置项目前是**惰性项** —— 能保存、能显示当前档位、不影响任何渲染。保留它的意义是 P4（真高斯模糊）回归时可直接接回。

---

### 9.8 数据模型与排序规则

`struct Item`（342–358）：

| 字段 | 类型 | 含义 |
|---|---|---|
| `path` | wstring | 完整路径（`OpenItem` 直接用它） |
| `name` | wstring | 文件名（含扩展名） |
| `isDir` | bool | 是否目录（`FILE_ATTRIBUTE_DIRECTORY`） |
| `expanded` | bool | 是否展开 |
| `childrenLoaded` | bool | 子项是否已枚举（懒加载标志） |
| `children` | `vector<Item*>` | 子项；析构递归 `delete` |
| `DisplayName()` | 方法 | `cfg.showExt == false` 且是文件 → 去掉最后一个 `.` 之后 |

**枚举（`LoadChildren` 364）**

```
FindFirstFileW(path + "\*")
只跳过 "." 和 ".."，其它全部收（包括 .gitignore、隐藏属性文件）
按 (foldersFirst ? isDir 优先 : 不管) → ciCompare(name) 升序
```

**`ciCompare`（59）不是本地化排序**：逐字符 `towlower` 比较（序号序），前缀短的在前。所以：

- 中文按 **Unicode 码点**排，不是拼音；
- `A` 与 `a` 等价；
- 数字按字符排（`10` 会排在 `2` 前面）。

如果要做"跟资源管理器一样"的排序，得换 `StrCmpLogicalW`（自然排序）+ `CompareStringEx`（本地化），**当前没做**。

**展开（`AddRows` 389）**：只有 `isDir && expanded` 才递归；没展开过的目录**不会**读盘。`RebuildRows` 399 从 `g_root->children` 起、depth 0 —— 根目录自身不占一行。

> 风险提示：目录联接（junction）/符号链接可以形成环，理论上能无限展开。当前**没有深度上限**，只能靠用户手动收起；正常使用不会触发。

### 9.9 视觉规格（主窗口）

| 元素 | 规格 | 来源 |
|---|---|---|
| 背景 | `Color(0,0,0,0)` 全透明 | `PaintAll` 998 |
| 底色 tint | accent + `opacity*255/100`，圆角 `S(12)`，与边框同路径 | 1007–1015 |
| 外边框 | 1px `Color(70,255,255,255)`（约 27% 白） | `DrawFrame` 957 |
| 内高光 | 1px `Color(22,255,255,255)`，`radius-1`，做"厚玻璃"倒角 | 同上 |
| 渐变/光泽带 | **故意没有** —— 历史上的上下亮度渐变会在窗口变高时露出一条横向色差线 | 注释 961–964 |
| 标题栏 | 高 `S(30)`；文件夹字形 `\uE8B7`（宽 `S(20)`，色 `AccentHi` α200）；名称 bold；**无计数 chip**（P9 移除）；列表起点 `ListTop()=TitleH()+S(5)`，行高 `S(26)` | `DrawTitle` / `DrawList` |
| 标题分隔线 | accent 着色、**两端渐隐**的发丝线 | 806+ |
| 行高 | `S(28)`，左内边距 `S(12)`，每级缩进 `S(16)`，三角热区宽 `S(16)` | 602–605 |
| 缩放提示 | 右下三条斜线，颜色 = 文字色 α105 | 1025–1031 |
| 文字 | `TextColorFor`（§16.4）+ 1px 阴影（§6.3） | 622 / 649 |
| 最小尺寸 | `S(200) × S(160)`；只允许 E/S/SE 拉 | `HitResize` 2280 |

## §10 设置面板

### 10.1 窗口（`OpenSettings` 2228 / 创建 2248）

| 项 | 值 |
|---|---|
| 类名 | `GlassDockSettings` |
| 样式 | `WS_EX_TOOLWINDOW \| WS_EX_TOPMOST \| WS_EX_LAYERED` + `WS_POPUP` |
| 尺寸 | `S(SET_W=400) × S(SET_H=516)`（125% → 500×645） |
| 位置 | **屏幕工作区正中央**（不是贴着组件）—— 两块都是半透明，叠在一起会分不清图层 |
| 置顶策略 | 打开时 TOPMOST 方便操作；`WM_ACTIVATE`（2044）失焦后取消置顶 |
| 生命周期 | 单例：`g_setHwnd` 已存在 → `SetWindowPos(HWND_TOPMOST)` + `ShowWindow` + `SetForegroundWindow` |

### 10.2 完整布局（逻辑坐标，`SET_W=400 / SET_H=516`）

| 元素 | 矩形 / y | 定义行 |
|---|---|---|
| 关闭按钮 | `(340, 10, 28, 28)` | `R_SetClose` 1442 |
| 文件夹卡片 | `(16, 60, 368, 44)` | `R_FolderCard` 1443 |
| 「更改…」 | `(306, 66, 68, 32)` | `R_FolderChange` 1444 |
| 宽滑块轨道 | `y = 162`，轨道 x = `20 → 380`（`DrawSlider` 1522），粗细 `S(6)`，滑块半径 `S(9)/S(10)` | `SliderTrackY(0)` 1445 |
| 高滑块轨道 | `y = 214`（同上） | 同上 |
| 不透明度轨道 | `y = 266`（同上） | 同上 |
| 滑块文字标签 | `y = 140 / 192 / 244` | `SliderLabelY` 1447 |
| 滑块命中区 | `(18, y-12, 364, 24)` —— 比视觉轨道更高，好点 | `R_SliderHit` 1446 |
| 预设色块 i | `(20 + i*38, 318, 30, 30)`，i=0..5 | `R_Preset` 1448 |
| 十六进制框 | `(250, 318, 84, 30)` | `R_HexBox` 1449 |
| 取色器按钮 | `(338, 318, 30, 30)` | `R_CustomColor` 1450 |
| 开机自启复选框 | `(18, 376, 344, 24)` | `R_CheckAuto` 1451 |
| 「拖动刷新率」标签 | **已移除**（P10）：原 `(20, 402, 120, 20)` 位置留白 | — |
| 刷新率档 i | **已移除**（P10）：原 `(110 + i*44, 400, 38, 24)` 位置留白 | — |
| 「恢复默认」 | `(20, 460, 100, 34)` | `R_Reset` 1454 |
| 「完成」 | `(284, 460, 84, 34)` | `R_Done` 1455 |

> **为什么 `SET_H = 516` 而不是 500**：P2 填平空档时先按 500 做，实测底部提示行（430）与分隔线（452）、按钮（460）之间会**穿字**（旧版就有 2px 重叠的历史瑕疵）。最终把提示行放 430、分隔线 452、按钮 460，高度取 516 才留够底部留白。

### 10.3 绘制（`PaintSettings` 1593）

| 步 | 内容 | 行 |
|---|---|---|
| 1 | 固定深色底 `Color(51,30,30,32)` + `S(12)` 圆角 | 1593–1610 |
| 2 | 标题「设置」+ 关闭 `\uE8BB` | 1630–1650 |
| 3 | 文件夹卡片：`\uE8B7` 图标 + 路径（`…` 中间省略）+「更改…」 | 1655–1710 |
| 4 | 三段滑块（标签 / 轨道 / 数值气泡跟随滑块） | 1714–1766 |
| 5 | 「背景色」区：6 预设 + hex 框 + 取色按钮 | 1770–1795 |
| 6 | 「选项」区：自启复选框、刷新率 6 档 | 1800–1850 |
| 7 | 底部提示「桌面小组件 · 只在桌面显示，不会盖住其他窗口」 | 1846–1856 |
| 8 | 分隔线 452 + 恢复默认/完成按钮 | 1860–1900 |
| 9 | 右下角「改动即时生效」 | 1902 |

颜色辅助：`IsDarkText` 1473 / `LumColor` 1474 / `HsvToRgb` 1477 / `Vivid` 1491 / `ControlColor` 1508 / `CardFill` 1515 / `CardBorder` 1516 / `TextOn` 1517。

### 10.4 命中枚举 `SH_*`（1460）

```
SH_NONE=0, SH_CLOSE, SH_FOLDER, SH_SLIDER0, SH_SLIDER1, SH_SLIDER2,
SH_PRESET0..5, SH_HEX, SH_COLOR, SH_CHECK, SH_HZ0..5, SH_RESET, SH_DONE
```

`SetHit(POINT)` 1930 按固定顺序返回；滑块额外支持"整条横带"命中（`R_SliderHit` 有 ±12 容差），所以点在轨道上方 12px 也能拖。

### 10.5 交互细节

| 操作 | 实现 | 行 |
|---|---|---|
| 点滑块任意处 | `SetSliderFromX` 1944 直接把值跳到该位置（轨道映射 240–1000 / 240–1200 / 0–100） | 1944 |
| 按住拖动 | `WM_MOUSEMOVE` 带 `g_setSlider` 持续更新 | 2119 |
| 选预设 | `ApplyAccent(hex)` 1993 → 写 `cfg.accent` | 1993 |
| 输 hex | `WM_CHAR` 2174 只接受 `0-9a-fA-F#`；满 6 位 `ApplyHex` 1999 校验并应用；`Backspace` 删字符 | 1999 |
| 取色器 | `SettingsPickColor` 2013 → `ChooseColorW`（需 `comdlg32`） | 2013 |
| 换文件夹 | `SettingsPickFolder` 1981 → `PickFolder` 1126（`SHBrowseForFolder`） | 1981 |
| 勾自启 | `WM_LBUTTONDOWN` 分支 → `cfg.autostart` + `ApplyAutoStart` | 2090 |
| 选刷新率 | `cfg.refreshHz = g_hzPresets[hit - SH_HZ0]` | 2096 |
| 恢复默认 | `SettingsReset` 2027：宽 420 / 高 560 / 不透明度 36 / 色 `808080` / 自启关 / 60Hz | 2027 |
| 完成 | `WM_CLOSE` → 隐藏（不销毁，下次秒开） | 2214 |

### 10.6 实时预览（`SetApplyLive` 1971）

任何数值变化都走同一条路：

```
SetApplyLive()  →  SaveConfig()      // 立刻落盘
                →  Resize 主窗口（若宽高变了，带 SWP_NOMOVE）
                →  Repaint()          // 主窗口立刻重画
                →  RepaintSet()       // 设置面板自身重画（气泡、当前色块）
```

所以**没有"保存/取消"按钮**，关掉面板就是保存完成。

---

### 10.7 视觉规格（设置面板，`PaintSettings` 1593）

| 元素 | 规格 | 来源 |
|---|---|---|
| 底色 | **固定** `Color(51,30,30,32)`（约 20% 黑蓝），圆角 `S(12)`；**不跟随**主窗口 accent/opacity | 1606 |
| 文字色 | **固定** `Color(255,247,247,250)` | 1614 |
| 卡片填充 | 深底时 `Color(26,0,0,0)`；浅底时 `Color(44,255,255,255)` | `CardFill` 1515 |
| 卡片描边 | `Color(42,0,0,0)` / `Color(70,255,255,255)` | `CardBorder` 1516 |
| 控件主色 | 深底 → 提亮的 accent（`Vivid`）；浅底 → accent ×55%，若仍太亮则 `(42,46,54)` | `ControlColor` 1508 |
| 控件上的字 | 背景亮度 >0.62 → `(24,24,28)`，否则白 | `TextOn` 1517 |
| 滑块轨道 | `Color(46,255,255,255)` 圆角 + 1px `Color(34,0,0,0)` 描边 | `DrawSlider` 1522 |
| 滑块填充 | accent 垂直渐变（α 255→225 激活 / 235→195 常态） | 同上 |
| 滑块圆点 | 底 `Color(46,0,0,0)` 偏移 1px + 白 `Color(253,255,255,255)` + accent 描边 2px/1.2px | 同上 |
| 勾选框 | 边长 `S(18)`、圆角 `S(5)`；未选：白 α30 填充 + α80 描边（hover α125）；已选：accent 填充 + accent 实描边 + `\uE73E`；标签距框 `S(10)`，常态 α200、hover 全亮 | `DrawCheckbox` 1559 |
| 分区标题 | 小号 + `·` 前缀风格（`DrawSectionLabel` 1584） | — |

> 为什么设置面板**不跟随**主窗口的透明度与配色：它是操作界面，需要稳定的对比度。固定 20% 深底保证在任何壁纸上控件都看得清；主窗口才是"展示面"，才需要可调。

## §11 桌面驻留机制

### 11.1 `SitOnDesktop`（1091）

```cpp
// 1) 去掉置顶、补上"不激活 + 工具窗"
SetWindowLongPtr(hwnd, GWL_EXSTYLE, (ex|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW) & ~WS_EX_TOPMOST);
// 2) 找宿主（Progman / WorkerW）
HWND host = GetDesktopWorkerW();
// 3) 插到"宿主上面那一个窗口"之后 —— 关键
HWND insertAfter = HWND_BOTTOM;
if (host){
    insertAfter = GetWindow(host, GW_HWNDPREV);   // 宿主上方那个窗口
    if (!insertAfter) insertAfter = HWND_TOP;      // 宿主已在自己这一层的最顶
}
// 4) 原子地显示 + 重定位 + 不激活
SetWindowPos(hwnd, insertAfter, 0,0,0,0,
             SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
```

**为什么不用 `HWND_BOTTOM`**：`HWND_BOTTOM` 会把窗口压到 z 序最底 —— 在新 shell 里 Progman 之下就是壁纸层，窗口会**直接消失到壁纸后面**（点不到、看不见）。"插到宿主的前一个兄弟"才是"壁纸之上、图标层附近"的稳定位置。

> 注意：代码里 `insertAfter` 的初值就是 `HWND_BOTTOM`，**只有 `host == NULL`**（连任意 WorkerW 都没找到）时才会真的用到它 —— 这是最后兜底，也是最可能"消失到壁纸后面"的路径。

**为什么用 `SWP_SHOWWINDOW` 而不是先 `ShowWindow` 再 `SetWindowPos`**：两步之间有一段"已显示但还没锚好"的窗口期，会出现"有时出现、有时不出现"的竞态（源码注释里明确记录了这个坑）。

### 11.2 宿主查找（`GetDesktopWorkerW` 1071）的真实缺陷

```cpp
HWND p = FindWindowW(L"Progman", NULL);
if (p) { 发 0x052C; 枚举找挂 SHELLDLL_DefView 的 WorkerW; }
// 找不到 → 兜底"任意一个 WorkerW"
```

**本机实测（2026-11-24，build 26300.9974 / 26H2）**：

| 事实 | 值 |
|---|---|
| `FindWindowW(L"Progman", NULL)` | **返回 0**（`GetLastError` = "操作成功完成"） |
| `FindWindowExW` 各种组合 | 同样返回 0 |
| `EnumWindows` 能找到 Progman 吗 | **能**，hwnd `0x1014a` = 65866，标题 `Program Manager`，全屏 `(0,0)-(2560,1440)`，exstyle `0x200080`（`NOREDIRECTIONBITMAP \| TOOLWINDOW`），**子窗口数 0** |
| 顶层 WorkerW 数量 | **13 个，全部 `visible=false`，尺寸多为 170×47（一个 0×0）**，**没有任何一个带 `SHELLDLL_DefView` 或 `SysListView32`** |
| 全树唯一的 `SHELLDLL_DefView` | hwnd 789438，父链 `CtrlNotifySink ← DirectUIHWND ← DUIViewWndClassName ← ShellTabWindowClass ← CabinetWClass(327814)` —— 属于**文件资源管理器窗口**，不是桌面 |

**后果**：本机会落到"任意 WorkerW"兜底，锚到一个 170×47 的不可见小窗。目前之所以看起来正常，是因为 `insertAfter = GetWindow(host, GW_HWNDPREV)` 恰好仍落在 Progman 之上（实测 z 序：`EnumWindows` 序号 199 = `GlassDockWidget`，200 = `Progman`）。

**建议修复（未实施，低风险）**：把查找顺序改成

1. `EnumWindows` 里找 **有 `SHELLDLL_DefView` 子窗**的窗口（旧 shell 的 WorkerW）；
2. 找不到 → 找标题为 `Program Manager` 的窗口（新 shell 的 Progman）；
3. 都没有 → **保持当前 z 序不动**（不要锚到任意 WorkerW）。

### 11.3 守卫定时器（`TIMER_GUARD`，1s，2809）

```
if (!g_userHidden) {
    if (!IsWindowVisible || IsIconic) { SitOnDesktop(); Repaint(); }
    if (++g_guardTick >= 5) { g_guardTick = 0; if (!dragging && !resizing) SitOnDesktop(); }
}
```

- 1 秒一次"活着吗"检查；
- 每 5 秒主动**重置 z 序锚点**（防止别的程序把它挤下去）；
- `g_userHidden`（2353）为真时完全停手 —— 这是"用户亲手隐藏"与"被系统藏了"的区分开关。

### 11.4 一个必须承认的边界

新 shell 把**壁纸和桌面图标都画在 Progman 的 DirectComposition 表面里**。所以：

- "壁纸之上" ✅ 已达成（组件在 Progman 之上）；
- "图标之下" ❌ **结构上不存在这一层** —— 没有独立的图标宿主窗口可供插入。

详细论证与探针原始输出见 `docs/DESKTOP-EMBED-ASSESSMENT-2026-11-24.md`。

---

## §12 托盘 / 热键 / 单实例

### 12.1 托盘

| 项 | 值 |
|---|---|
| `WM_TRAYICON` | `WM_APP + 1`（2349） |
| 图标 | `LoadIcon(hInst, MAKEINTRESOURCE(1))` |
| 提示文字 | `GlassDock 桌面小组件（Ctrl+Alt+G 显示 / 隐藏）`（2368） |
| 左键 / 双击 | `ToggleWidget()`（= 显示） |
| 右键 | `ShowTrayMenu()` 2419 → `SetForegroundWindow` + `TrackPopupMenu`（必须前置，否则菜单点外面不消失） |
| 菜单项 | `MENU_PICK 2001 / MENU_SETTINGS / MENU_REFRESH / MENU_AUTO / MENU_QUIT / MENU_SHOW / MENU_HIDE`（2327） |
| 移除 | `RemoveTrayIcon` 2372（`NIM_DELETE`），在 `WM_DESTROY` 里调 |

### 12.2 显隐语义（容易被文档写错的地方）

| 函数 | 行 | 真正做了什么 |
|---|---|---|
| `ShowWidget` | 2382 | `RenderBackground()` → 记 `wasVis` → `g_userHidden=false` → `SitOnDesktop()`（内部 `SWP_SHOWWINDOW`）→ 若原来不可见再 `Repaint()` |
| `HideWidget` | 2390 | **只做两件事**：`g_userHidden=true` + `ShowWindow(SW_HIDE)`；气泡由调用方补 `NotifyHidden()`（菜单分支 2674、按钮分支 2627） |
| `ToggleWidget` | 2401 | **等价于 `ShowWidget()`** —— 它**不**隐藏、**不**置顶。注释写明：被壁纸埋住的窗口 `IsWindowVisible` 仍为 TRUE，用可见性做 toggle 会"双击变隐藏" |

> 也就是说：热键与托盘左键都只是"确保它在桌面层且可见"，**不会**把正在显示的组件藏起来。

### 12.3 热键

```
RegisterHotKey(g_hwnd, HOTKEY_TOGGLE=1, MOD_CONTROL|MOD_ALT, 'G')   // 2805
```

失败（被别的程序占用）不报错、不影响其它功能。

### 12.4 单实例（2762）

```
CreateMutexW(NULL, FALSE, L"Local\GlassDock_SingleInstance")
if (ERROR_ALREADY_EXISTS) {
    HWND other = FindWindowW(L"GlassDockWidget", NULL);
    if (other) PostMessageW(other, WM_SHOWME, 0, 0);
    return 0;
}
```

注意：`Local\` 前缀 → **只在当前登录会话唯一**，多用户/多 RDP 会话可以各开一个。

### 12.5 退出与收尾（`WM_DESTROY` 2681）

```
KillTimer(TIMER_BG) / KillTimer(TIMER_GUARD)
UnregisterHotKey(hwnd, HOTKEY_TOGGLE)
RemoveTrayIcon()
FreeIcons()               // 图标缓存
FreeLayeredSurface()      // 主窗口 DIB / Bitmap
FreeSetLayeredSurface()   // 设置面板 DIB / Bitmap
FreeScratch()             // crisptext 临时缓冲（crisptext.h 52）
PostQuitMessage(0)
```

> 顺序很重要：所有 GDI+ 对象必须在 `GdiplusShutdown` 之前析构，否则退出时 0xC0000005（见 §16 坑 11）。

---

## §13 安装器与卸载

### 13.1 安装流程（`installer/setup.cpp`，131 行）

| 步 | 动作 |
|---|---|
| 1 | 选目录：`GetFileAttributesW("D:\\")` 存在 → `D:\GlassDock`；否则 `%LOCALAPPDATA%\Programs\GlassDock`（14–26 行） |
| 2 | `SHCreateDirectoryExW` 建目录（98 行） |
| 3 | 从自身资源解出 **2 个文件**：`RID_EXE=100` → `GlassDock.exe`、`RID_MANIFEST=101` → `GlassDock.exe.manifest`；`RID_ICON=1` 是**安装器自己的图标**（写在 setup.rc 里），不落地成文件 |
| 4 | 建桌面快捷方式（`CSIDL_DESKTOPDIRECTORY\GlassDock.lnk`）+ 开始菜单（`CSIDL_PROGRAMS\GlassDock.lnk`）：`SetPath(exe)` / `SetWorkingDirectory(installDir)` / `SetIconLocation(exe,0)` / `SetDescription(...)` / `IPersistFile::Save` |
| 5 | 写卸载注册表项（68–86 行） |
| 6 | `MessageBoxW` 问"是否立即启动" → `ShellExecuteW`（90 / 125 行） |

> 小细节：注释说"不可写也回退"，但代码实际**只判断 D 盘是否存在**（18–24 行）。若 D 盘存在但只读，第 3 步写入失败会弹"安装文件写入失败"而不是自动换目录 —— 这算一个未实现的兜底，不影响正常机器。

### 13.2 卸载注册表项

`HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\GlassDock`

| 值 | 内容 |
|---|---|
| `DisplayName` | GlassDock 桌面文件夹小组件 |
| `DisplayVersion` | 1.0.0 |
| `Publisher` | GlassDock |
| `InstallLocation` | 安装目录 |
| `UninstallString` | `"<安装目录>\GlassDock.exe" --uninstall` |
| `NoModify` / `NoRepair` | 1 |
| `EstimatedSize` | 1（KB 级，仅占位） |
| `SystemComponent` | 0 |

> **没有写 `DisplayIcon`** —— 应用列表里的图标取自 `DisplayName` 同目录的 exe（Windows 的默认行为），所以条目显示正常；如果想强制指定，加一条 `DisplayIcon` 即可（代码未实现）。

### 13.3 卸载（`RunUninstall` 2692）

1. 删桌面与开始菜单的 `GlassDock.lnk`（`CSIDL_DESKTOPDIRECTORY` / `CSIDL_PROGRAMS`）；
2. 删自启动值 `HKCU\...\Run\GlassDock`；
3. 删卸载项整棵子树 `Uninstall\GlassDock`（`RegDeleteTreeW`）；
4. `MessageBoxW`「GlassDock 已从系统中卸载」；
5. 用 `cmd /c ping -n 2 127.0.0.1 >nul & rd /s /q "<安装目录>" & rd /s /q "%APPDATA%\GlassDock"` **延时 2 秒删除**（`SW_HIDE`）。

> 延时的原因：卸载动作发生在 exe **自己还在运行**的时候，Windows 不允许删除正在执行的映像；`ping -n 2` 当 `sleep` 用（不依赖 `timeout.exe` 在无控制台时的怪异行为）。
> 删除范围**严格限定**在这两个目录，不做任何通配符删除。

---

## §14 构建系统

### 14.1 工具链

| 项 | 路径 / 值 |
|---|---|
| g++ | `D:\Doubao\toolchain\mingw\mingw64\bin\g++.exe` |
| windres | 同目录 `windres.exe`（2.47） |
| 版本 | MinGW-W64 x86_64-ucrt-posix-seh **16.2.0** |
| Shell | PowerShell（本机 **无 git CLI**，回滚靠文件备份） |

### 14.2 `build.ps1`（51 行）六步

| 步 | 命令要点 | 产物 |
|---|---|---|
| 1 | `Push-Location src` → `windres resource.rc -o resource.o` | `src\resource.o` |
| 2 | `g++ src\GlassDock.cpp src\resource.o -o GlassDock.exe` + 10 个库 + `-mwindows -municode -O2 -s -static` | 主程序 |
| 3 | `Copy-Item src\app.manifest GlassDock.exe.manifest` | 外部 manifest |
| 4 | `Push-Location installer` → `windres setup.rc -o setup_res.o`（setup.rc 内嵌 `..\GlassDock.exe` 等） | `installer\setup_res.o` |
| 5 | `g++ installer\setup.cpp installer\setup_res.o -o installer\setup.exe` + 5 个库 | 安装器 |
| 6 | `Copy-Item installer\setup.exe release\GlassDock-Setup-1.0.0.exe` | 分发件 |

> 顺序不能反：第 4 步的 `setup.rc` 要打包**已经生成好的** `GlassDock.exe` 与 manifest，所以必须在第 2、3 步之后。
### 14.3 链接库

| 目标 | 库 |
|---|---|
| `GlassDock.exe` | `-lgdiplus -ldwmapi -lshell32 -lcomctl32 -lole32 -luuid -lshlwapi -lgdi32 -luser32 -ladvapi32` |
| `setup.exe` | `-lshell32 -lole32 -luuid -ladvapi32 -lshlwapi` |
| 离屏 harness | 追加 `-lcomdlg32`（`ChooseColorW` 在 `Comdlg32`） |

### 14.4 手动编译（照抄可用）

```powershell
Push-Location 'D:\Doubao\GlassDock\src'
& 'D:\Doubao\toolchain\mingw\mingw64\bin\windres.exe' 'resource.rc' -o 'resource.o'
Pop-Location
& 'D:\Doubao\toolchain\mingw\mingw64\bin\g++.exe' 'src\GlassDock.cpp' 'src\resource.o' `
  -o 'build\GlassDock.test.exe' -mwindows -municode -O2 -s -static `
  -lgdiplus -ldwmapi -lshell32 -lcomctl32 -lole32 -luuid -lshlwapi -lgdi32 -luser32 -ladvapi32
```

### 14.5 构建前必须做的事

**先杀掉正在运行的实例**，否则链接器报 "cannot open output file ... Permission denied"：

```powershell
Get-Process GlassDock -ErrorAction SilentlyContinue | Stop-Process -Force
```

### 14.6 构建期常见错误

| 现象 | 原因 | 处理 |
|---|---|---|
| `undefined reference to ChooseColorW` | 缺 `-lcomdlg32` | 追加该库 |
| 运行即 `0xC0000135`（找不到 DLL） | 漏 `-static` | 加 `-static` |
| 退出时 `0xC0000005` | GDI+ 对象在 `GdiplusShutdown` 之后析构 | 用内层作用域先释放 |
| 中文编译报错 | 缺 `-municode` / 源文件编码 | 保持 UTF-8 + BOM，`-municode` |
| 177 条 warning（167 结构体初始化噪声 + 10 真警告） | `-Wall -Wextra` 对 `{0}` 聚合初始化的正常噪声 | 忽略噪声；真警告为 `GetBackBuffer` 未使用、unused-parameter ×3、unused-variable ×2、sign-compare ×4（P7 实测，见 §14.6 附注） |

---

## §15 测试与验证工具链

### 15.1 三层验证策略

| 层 | 工具 | 能证明什么 |
|---|---|---|
| L1 编译 | `build.ps1` | 无 error、警告清单稳定 |
| L2 离屏像素 | `build\shot.exe` | 布局正确、命中正确、**不需要桌面** |
| L3 真进程 | 隔离 `APPDATA` 启动 + `pwcap2.exe` 抓图 | 真实渲染与预期一致 |

### 15.2 `build\shot.exe`（离屏渲染 harness）

源码 `build\shot_settings.cpp`，做法是：

```cpp
#define wWinMain wWinMain_original
#include "../src/GlassDock.cpp"     // 直接编译真源码，不是复制
#undef wWinMain
```

然后手工把 PARGB 位图 **source-over 合成到不透明底**（因为离屏没有壁纸可透）。

| 用法 | 输出 |
|---|---|
| `shot.exe settings <png>` | 设置面板 `400×516`（放大后 500×645） |
| `shot.exe main <png>` | 主窗口 `420×560` |
| `shot.exe hit` | 用**设计表坐标**跑 24 个命中断言 |

命中测试结果：**24/24 通过**（关闭、卡片、更改、三条滑块、6 预设、hex、取色、复选、6 档刷新率、重置、完成）。

### 15.3 `build\compare.ps1`（像素比对）

```powershell
.\build\compare.ps1 -P1 build\a.png -P2 build\b.png
```

输出：不同像素数、最大通道差、**文本色 `(247,247,250,255)` 的像素计数**。

> 最后这个计数是最稳的回归指标：同一版式下它恒为 **2334**，比"总差异像素数"可靠得多。

### 15.4 `tools\pwcap2.exe`（真窗口抓图）

```powershell
.\tools\pwcap2.exe out.png GlassDockWidget     # 或 GlassDockSettings
```

原理：`FindWindowW(类名)` → `PrintWindow(PW_RENDERFULLCONTENT)` → 存 PNG。
**必须**用 `-municode -static -O2` 编译，否则 0xC0000135。

**已知测量噪声**：

- 同一进程连续两次抓图 → **0 差异**；
- 不同进程抓同一版本 → 最多 345 px 差、最大 2/255（GDI+ 抗锯齿的舍入差异）。

所以判定"代码改动是否影响像素"时，**必须同进程对比**，或用文本色计数。

### 15.5 `build\fonttest.exe`（字体对照）

三种候选字体同串同尺寸并排渲染 + `GetGlyphIndicesW` 缺失字形探测，输出 `fonttest.png` / `font_zoom.png`（4× 放大）。

结论（15px）：

| 字体 | 同串宽度 | CJK |
|---|---|---|
| Segoe UI | 323 | `GetGlyphIndicesW` 报 missing，但 GDI 字体链接仍能画 |
| Microsoft YaHei UI | 328 | 原生 |
| Microsoft YaHei | 328 | 原生 |

### 15.6 `build\embedprobe.exe`（壁纸层嵌入探针）

创建洋红测试窗 → `BitBlt` 数像素 → `SetParent` 到 Progman / WorkerW → 读 `DwmGetWindowAttribute(14)`（cloaked）。

| 结果 | 值 |
|---|---|
| `SetParent(Progman)` 返回值 | `0x1000c`（= 原父窗口），`err=0` → **调用不报错** |
| `GetParent` 返回值 | 0 —— **语义陷阱**：非 `WS_CHILD` 窗口的 `GetParent` 返回 owner |
| `cloaked` | 0 |
| `visible_magenta` | **0**（像素列未闭环：执行时用户在全屏播视频，屏幕被盖住） |

结论：**调用成功 ≠ 真的嵌进去了**。像素可见性必须在"桌面未被任何窗口覆盖"时验证。

### 15.7 隔离测试标准配方

```powershell
Get-Process GlassDock -ErrorAction SilentlyContinue | Stop-Process -Force
$env:APPDATA = 'D:\Doubao\GlassDock\build\appdata_test'
New-Item -ItemType Directory -Force -Path $env:APPDATA | Out-Null
Start-Process 'D:\Doubao\GlassDock\GlassDock.exe' -WorkingDirectory 'D:\Doubao\GlassDock'
Start-Sleep -Milliseconds 800
& '.\tools\pwcap2.exe' '.\build\shot.png' GlassDockWidget
```

安全性：`HKCU\...\Run\GlassDock` 若不存在，`ApplyAutoStart()` 的删除是**非操作**；配置完全落在临时目录；不动生产 exe。

### 15.8 回归基线

| 文件 | 是什么 | 用途 |
|---|---|---|
| `build\base.png` | 清理前基线 | 与 `base2` 对比：345 px / max 2 |
| `build\base2.png` | 清理后同进程 | 与 `dev` **0 差异** |
| `build\dev2_run2.png` | P2 完成后主窗口 | 后续所有版本的对照基准 |
| `build\final_main.png` | 最终交付版 | 与 `dev2_run2` 0 差异 |
| `build\v15_widget.png` | 15px 字体版 | 与 `dev2_run2` 差 77 px / max 1 |
| `build\set_v2.png` | 最终设置面板 | 布局验收图 |

---

### 15.9 发版前验收清单（照着打勾）

| # | 检查 | 通过标准 |
|---|---|---|
| 1 | `build.ps1` 全流程 | 0 error；warning 只有历史噪声 |
| 2 | `shot.exe hit` | 24/24（改布局后按新表数量） |
| 3 | `shot.exe settings` 出图 | 无重叠、无穿字、底部留白 ≥8 逻辑 px |
| 4 | `shot.exe main` 出图 | 标题栏/行距/滚动条正常 |
| 5 | 隔离 `APPDATA` 真进程启动 | 进程存活、托盘出现 |
| 6 | `pwcap2.exe` 抓图 vs `dev2_run2.png` | 差异 <100 px（跨进程噪声内）或文本色计数 = 2334 |
| 7 | z 序 | `EnumWindows` 中 `GlassDockWidget` 紧邻 `Progman` 之前 |
| 8 | 热键 / 托盘 / 单实例 | `Ctrl+Alt+G` 唤回；二次启动不新开进程 |
| 9 | 拖动 / 缩放 / 双击打开 / 滚轮 | 四项均正常，`SavePos` 生效 |
| 10 | 安装包 | `setup.exe` 与 `release\...exe` 同字节；装到临时目录能卸载干净 |

## §16 已知问题与踩坑全表

> 标注 `[已修]` 的条目是历史踩坑记录，写在这里是为了**别再踩第二次**。

| # | 问题 | 影响 | 现状 / 处理 |
|---|---|---|---|
| 1 | `FindWindowW("Progman")` 在本机返回 0 | 宿主查找落到"任意 WorkerW"（170×47 不可见） | **未修**，§11.2 有建议 |
| 2 | 新 shell 无独立图标层 | 无法实现"图标之下" | 结构限制，不可修 |
| 3 | 文字颜色**几乎**恒浅色 `(247,247,250)` | 浅色壁纸上对比度低 | 精确公式见 §16.4；`g_desktopLum` 从未更新，P3 待做 |
| 4 | `WM_NCHITTEST` 恒 `HTCLIENT` | 组件矩形内**盖住的桌面图标点不到** | 有意设计（否则 alpha=0 区域点击穿透，组件"点一下就消失"） |
| 5 | alpha 0 → 钳成 1 | 全透明像素仍接收点击 | 与 #4 配套，缺一不可 |
| 6 | 跨进程抓图有噪声 | 误判"代码改了像素" | 345 px / max 2；用同进程对比或文本色计数 |
| 7 | 我们用灰度 AA，Explorer 用 ClearType | 放大看字形边缘有差别 | 不改：预乘后会出彩色毛边 |
| 8 | `refreshHz` 是惰性设置 | 用户调了没反应 | 见 §9.7；P4 回归时接回 |
| 9 | `GetBackBuffer` + `g_bbMain/g_bbSet` 是死代码 | `-Wall` 报 `-Wunused-function` | 保留未删（P1 遗留） |
| 10 | `docs/screenshot.png`、`docs/settings.png` 是旧版 | README 图里还有已删的"模糊强度"滑块 | 未重拍 |
| 11 | 退出 0xC0000005 | GDI+ 对象晚于 `GdiplusShutdown` 析构 | `[已修]` 用内层作用域 |
| 12 | `ImageLockModeReadWrite` 在 MinGW 不存在 | 编译失败 | `[已修]` 强转 `(ImageLockMode)3` |
| 13 | `IconFont` 注册表值尾部带空格 | `FontFamily` 静默失败 → 退回 Segoe UI | `[已修]` `TrimFace` 258 |
| 14 | `IconFont` 的 `lfHeight` 是 96dpi 单位 | 125% 下字号小 3px（用户当场发现） | `[已修]` `px *= g_scale` |
| 15 | `SetProcessDPIAware` 是系统级 | 多显示器不同缩放时，副屏比例可能不准 | 未升级（P5：per-monitor v2） |
| 16 | 没有 git | 无法 `git diff` / 回滚 | 靠 `build\*.bak.2026-11-24` |
| 17 | 设置面板底色固定 | 不跟随 accent / opacity | 有意（保证控件对比度） |
| 18 | `Config::topmost` 恒为 false | 字段形同虚设 | `LoadConfig` 128 强制、`SaveConfig` 写 0 |
| 19 | `g_fontWatermark` / `g_hfWatermark` 建了不画 | 多几个 GDI 对象 | 历史残留 |
| 20 | 大目录在 UI 线程同步枚举 | 首次展开卡顿 | 单线程架构代价，未做异步 |
| 21 | **两套坐标混用**：`cfg.width/height` 是设备像素，内部度量（`TitleH/RowH/S()`）是逻辑→设备 | 高 DPI 下"同数值的窗口更小、内容更大"，行数变少 | 见 §16.3；改之前必须想清楚按哪套算 |
| 22 | 目录联接（junction）可成环 | 极端情况下可无限展开、内存涨 | 无深度上限；需用户手动操作才触发，未修 |
| 23 | `ciCompare` 是码点序，不是资源管理器序 | 中文/数字排序与 Explorer 不同 | 要一致需换 `StrCmpLogicalW`，未做 |

### 16.1 坑 4 + 5 为什么必须成对存在

逐像素 alpha 窗口的默认命中规则是：**alpha=0 的像素返回 `HTTRANSPARENT`**，点击会穿透到下面的桌面图标。

但 GlassDock 的整个面板几乎全是 alpha=0（只有文字、图标、边框不透明）。如果不做处理：

- 点在面板空白处 → 穿透 → 点到桌面图标 → 组件"看起来点一下就没了"（其实是点到后面的图标了）。

解决办法是两件事同时做：

1. `CopyPaintToDIB` 把 alpha 0 钳成 **1**（几乎不可见，但不等于 0）；
2. `WM_NCHITTEST` 恒返 `HTCLIENT`（连 alpha=1 的判断都绕过）。

代价：**组件矩形覆盖的桌面图标点不到**。这是产品层面的取舍 —— 组件本身就是"盖在桌面上的一块面板"，用户理解它是一块实心玻璃。

### 16.2 坑 14 的完整时间线（值得记住）

1. 用户说"字体用系统字体"，读到 `IconFont = Microsoft YaHei, lfHeight = -12`。
2. 直接把 `-12` 取绝对值当**设备像素** → 文字只有 12px。
3. 用户发截图问"你觉得一样吗" → 量出 Explorer 列表行文字带 16–20px、桌面标签 13px，我们只有 11–13px。
4. 定位：注册表里的 `lfHeight` 是 **96dpi 逻辑单位**，125% 缩放必须乘 `g_scale`。
5. 改 `px = 12 * 1.25 = 15` → 与 Explorer 一致。
6. 回归：改前/改后 235200 像素中仅 **77** 个不同（max 1/255），文本色像素数同为 **2334** —— 证明"换 face"几乎不可见，用户看到的差异**全部来自字号**。

### 16.3 坑 21：两套坐标系的真实关系

| 坐标 | 例子 | 缩放 |
|---|---|---|
| 窗口尺寸 / 配置 | `cfg.width = 420`、`cfg.height = 560` | **不缩放**，直接是设备像素 |
| 面板内部度量 | `TitleH() = S(36)`、`RowH() = S(28)`、圆角 `S(12)` | 乘 `g_scale` |
| 设置面板整体 | `SetWindowPos(..., S(SET_W), S(SET_H))` | 乘 `g_scale` |

所以在本机 125% 下：主窗口物理 420×560，而标题栏占 45px、每行 35px —— 可见行数比 100% 缩放下**少**。这不是崩溃级 bug，但确实会造成"高分屏上组件显得偏小、行距偏大"的观感不一致。

**如果要统一（未做）**，两种改法二选一：

1. 让 `cfg.width/height` 存**逻辑**值：创建/`SetWindowPos`/`CommitLayered` 全部改 `S(cfg.width)`，同时 `LoadConfig` 要把老 ini 的值做一次换算（**有数据迁移风险**）；
2. 让内部度量改设备像素：去掉 `S()`，改为按 `cfg.width` 反推比例（**改动面更大**）。

> 现状下更安全的做法：**不要动**，只在文档里写清；真要改按方案 1，并先备份 `config.ini`。

### 16.4 文字颜色到底怎么算（`TextColorFor` 622）

```cpp
float accL = ColorLum(cfg.accent);              // 0..1
float o    = cfg.opacity / 100.0f;              // 0..1
float eff  = accL * o + g_desktopLum * (1-o);   // g_desktopLum 恒 0.5
return eff > 0.58 ? 深字(45,45,48) : 浅字(247,247,250);
```

| 场景 | eff | 结果 |
|---|---|---|
| 默认（accent 灰 808080 ≈0.5、opacity 36） | 0.5 | **浅字**（恒浅的真相） |
| accent 纯白 F5F5F5、opacity 100 | ≈0.96 | **深字**（但此时面板已被底色盖满，壁纸透不出来） |
| accent 纯黑 1E1E1E、opacity 100 | ≈0.12 | 浅字 |
| 任何 accent、opacity 0 | 0.5 | 浅字 |

> 所以"文字恒浅"只在默认/低不透明度配置下成立。真正缺的是 `g_desktopLum` 从来没有被更新过（313–317 行注释写明），P3 要补的就是它。

---

## §17 常量速查 + 符号行号索引

### 17.1 常量总表

| 常量 | 值 | 行 |
|---|---|---|
| `SET_W` | 400 | 1433 |
| `SET_H` | **516** | 1434 |
| `g_presets[6]` | `808080 1E1E1E F5F5F5 2B6CB0 1FA2A6 6B5BD2` | 1427 |
| `g_hzPresets[6]` | `60 90 120 144 165 240` | 1458 |
| `WM_TRAYICON` | `WM_APP + 1` | 2349 |
| `WM_SHOWME` | `WM_APP + 99` | 2350 |
| `TIMER_BG` | 1（900 ms） | 2351 / 2808 |
| `TIMER_GUARD` | 3（1000 ms） | 2352 / 2809 |
| `HOTKEY_TOGGLE` | 1（`Ctrl+Alt+G`） | 2357 |
| `MENU_PICK` | 2001（后续 +1 递增） | 2327 |
| `RS_*` | `RS_NONE/E/W/S/N/NE/NW/SE/SW` | 2269 |
| `SH_*` | 见 §10.4 | 1460 |
| 滑块范围 | 宽 240–1000 / 高 240–1200 / 透明 0–100 | 1944 |
| 面板圆角 | `S(12)` | 1011 / 1593 |
| 最小窗口 | `S(200) × S(160)` | resize 分支 |
| tint alpha | `opacity * 255 / 100` | 1008 |
| 阴影 | 偏移 `(1,1)`、alpha 85 | 649 |

### 17.2 主文件符号索引（按功能域）

**基础 / 配置**

| 符号 | 行 |
|---|---|
| `ColorLum` | 55 |
| `ciCompare` | 59 |
| `AppDataDir` | 69 |
| `GetIniInt` / `GetIniStr` / `SetIniInt` / `SetIniStr` | 75 / 82 / 88 / 94 |
| `ParseHex` / `HexColor` | 98 / 106 |
| `LoadConfig` / `SaveConfig` / `SavePos` | 112 / 146 / 160 |
| `ApplyAutoStart` | 167 |

**字体**

| 符号 | 行 |
|---|---|
| `HFontFor` | 212 |
| `ReadSystemLabelFont` | 227 |
| `TrimFace` | 258 |
| `InitFonts` | 262 |
| `RoundCorners` | 322 |

**数据模型**

| 符号 | 行 |
|---|---|
| `struct Item` | 342 |
| `g_root` / `g_rows` | 359 |
| `LoadChildren` / `AddRows` / `RebuildRows` / `LoadRoot` | 364 / 389 / 399 / 406 |
| `Lower` | 424 |

**图标**

| 符号 | 行 |
|---|---|
| `AlphaBitmapFromHIcon` | 442 |
| `GetShellIcon` / `IconFor` / `FreeIcons` | 571 / 587 / 596 |

**绘制**

| 符号 | 行 |
|---|---|
| `TitleH` / `RowH` / `LeftPad` / `Indent` | 602 / 603 / 604 / 605 |
| `GetButtons` | 607 |
| `ResizeCornerRect` | 616 |
| `TextColorFor` / `TextColor` / `AccentSolid` / `AccentHi` | 622 / 628 / 629 / 632 |
| `DrawTextCrisp`（`wchar_t*`） | 649 |
| `DrawTextCrisp`（`wstring` 重载） | 697 |
| `MeasureTextWidth` | 724 |
| `RoundedRect` | 738 |
| `ClampScroll` | 754 |
| `DrawTitle` / `DrawList` / `DrawScrollbar` / `DrawFrame` | 763 / 847 / 930 / 957 |
| `PaintAll` | 983 |

**动作 / 桌面**

| 符号 | 行 |
|---|---|
| `OpenItem` / `Toggle` / `Reload` | 1035 / 1039 / 1048 |
| `GetDesktopWorkerW` / `SitOnDesktop` | 1071 / 1091 |
| `SetNoActivate` | 1118 |
| `PickFolder` | 1126 |

**分层表面**

| 符号 | 行 |
|---|---|
| `g_bbMain, g_bbSet`（死） | 1148 |
| `GetBackBuffer`（死） | 1151 |
| `GetPaintBitmap` | 1196 |
| `FreeLayeredSurface` / `GetLayeredSurface` | 1210 / 1222 |
| `CopyPaintToDIB` / `CommitLayered` / `Repaint` | 1258 / 1278 / 1325 |
| `FreeSetLayeredSurface` / `GetSetLayeredSurface` | 1336 / 1347 |
| `GetSetPaintBitmap` / `CommitSetLayered` / `RepaintSet` | 1371 / 1379 / 1424 |

**设置面板绘制**

| 符号 | 行 |
|---|---|
| `LR` / `InRect` | 1436 / 1438 |
| `R_*` 布局函数 | 1442–1455 |
| `IsDarkText` / `LumColor` / `HsvToRgb` / `Vivid` | 1473 / 1474 / 1477 / 1491 |
| `ControlColor` / `CardFill` / `CardBorder` / `TextOn` | 1508 / 1515 / 1516 / 1517 |
| `DrawSlider` / `DrawCheckbox` / `DrawSectionLabel` | 1522 / 1559 / 1584 |
| `PaintSettings` | 1593 |

**设置面板逻辑**

| 符号 | 行 |
|---|---|
| `SetHit` / `SetSliderFromX` | 1930 / 1944 |
| `KeepOnScreen` / `SetApplyLive` | 1955 / 1971 |
| `SettingsPickFolder` | 1981 |
| `ApplyAccent` / `ApplyHex` | 1993 / 1999 |
| `SettingsPickColor` / `SettingsReset` | 2013 / 2027 |
| `SettingsProc` | 2040 |
| `OpenSettings` | 2228 |

**主窗口逻辑**

| 符号 | 行 |
|---|---|
| `HitResize` / `CursorForMode` | 2280 / 2290 |
| `HitRow` / `HitButton` / `SetCursors` | 2300 / 2307 / 2313 |
| `ShowContextMenu` | 2329 |
| `AddTrayIcon` / `RemoveTrayIcon` | 2359 / 2372 |
| `ShowWidget` / `HideWidget` / `ToggleWidget` / `NotifyHidden` | 2382 / 2390 / 2401 / 2405 |
| `ShowTrayMenu` | 2419 |
| `WndProc` | 2436 |
| `RunUninstall` / `wWinMain` | 2692 / 2732 |

### 17.3 `crisptext.h` 符号

| 符号 | 行 |
|---|---|
| `namespace crisp` | 39 |
| `HAlign` / `VAlign` | 42 / 43 |
| `Scratch` / `g_scratch` / `FreeScratch` / `EnsureScratch` | 46 / 50 / 52 / 58 |
| `MakeHFont` | 90 |
| `Rasterize` | 112 |
| `Composite` | 176 |
| `MeasureWidth` | 217 |
| `Draw` / `DrawWithShadow` | 238 / 256 |

---

## §18 DPI 现状与升级路径

### 18.1 现在的做法

```
wWinMain 2734  SetProcessDPIAware();                  // 系统级 DPI 感知
wWinMain 2750  g_scale = GetDeviceCaps(LOGPIXELSX)/96 // 全局缩放因子
```

所有 UI 尺寸都过 `S(x) = (int)(x * g_scale + 0.5)`，所以 125% 下：

- 逻辑 420 × 560 → 设备 525 × 700
- 字号 15px（`IconFont -12` × 1.25）
- 圆角 12 → 15

### 18.2 局限

| 场景 | 表现 |
|---|---|
| 单显示器、单一缩放 | ✅ 完美 |
| 同一显示器改缩放后**重启**程序 | ✅ 跟随（`g_scale` 在启动时取） |
| 程序运行中改缩放 | ⚠️ 需要重启才重新取（未监听 `WM_DPICHANGED`） |
| 多显示器、不同缩放 | ⚠️ 按主屏比例渲染，拖到副屏可能偏大/偏小 |
| 跨屏拖动 | ⚠️ 同上 |
| 高 DPI 下的"窗口尺寸" | ⚠️ `cfg.width/height` 是设备像素、内部度量是逻辑→设备，两者基准不同（§16.3） |

### 18.3 P5 升级方案（未实施）

1. manifest 改 `dpiAwareness = PerMonitorV2`（`<dpiAwareness>PerMonitorV2</dpiAwareness>` 需要 `SMI/2016/WindowsSettings` 命名空间）；
2. 处理 `WM_DPICHANGED`：取 `HIWORD(wParam)` 为新 DPI → 重算 `g_scale` → `InitFonts` → 重建 surface → `Repaint`；
3. `SitOnDesktop` / `KeepOnScreen` 改用 `MonitorFromWindow` + `GetMonitorInfo`，不再依赖主屏；
4. 风险：per-monitor v2 下 GDI 文字会被系统按 DPI 拉伸，需要额外验证 `DrawTextCrisp` 的实测像素。

---

## §19 未完成路线图

### 19.1 立即可做（低风险）

| 项 | 内容 | 预估 |
|---|---|---|
| R1 | 修 `GetDesktopWorkerW`：先 `EnumWindows` 找带 `SHELLDLL_DefView` 的窗口，其次标题 `Program Manager`，都没有则不动 z 序 | 30 行 |
| R2 | 删 `GetBackBuffer` / `g_bbMain` / `g_bbSet`（死代码） | −20 行 |
| R3 | 重拍 `docs/screenshot.png`、`docs/settings.png`（现在是旧版，还带已删的模糊滑块） | 10 分钟 |
| R4 | `refreshHz` 二选一：接回（做 P4）或从 UI 摘掉（避免"调了没反应"） | 取决于 P4 |

### 19.2 P3 文字颜色自适应（有决策点）

| 方案 | 做法 | 优点 | 缺点 |
|---|---|---|---|
| T1 | 读壁纸文件（`SystemParametersInfo(SPI_GETDESKWALLPAPER)`）采样平均亮度 → 自动切换浅/深字 | 全自动 | 动态壁纸读不到（Wallpaper Engine 的壁纸文件是静态图或纯色） |
| T2 | 设置里手选"浅色字 / 深色字 / 自动" | 简单可控 | 用户要自己判断 |

当前 `g_desktopLum` 恒 0.5 → 默认配置（灰 + 36%）算出的 `eff = 0.5` → 走"浅色字"分支，永远不会自适应。若做 T1，需要把 `g_desktopLum` 变成真实采样值（并且 `WM_SETTINGCHANGE` 时重算），届时 `TextColorFor` 会自动切换深浅字。

### 19.3 P4 真高斯模糊（需接受取舍）

回到"模糊"意味着必须拿背景像素，而背景来源只有两个：

1. **截屏**：每次重绘截一次桌面 → CPU 高、动态壁纸延迟一帧、多屏要拼接；
2. **DWM 缩略图 / `DwmRegisterThumbnail`**：只能拿到别的窗口，拿不到"壁纸本身"。

结论：做 P4 等于**放弃"动态壁纸实时透出"**，退化为"静态快照 + 模糊"。这是产品方向问题，需要用户拍板。

### 19.4 壁纸层嵌入（已论证不可行）

详见 `docs/DESKTOP-EMBED-ASSESSMENT-2026-11-24.md`；本文 §11.2 只保留结论要点。可行的平替按性价比排序：

| 方案 | 效果 | 成本 |
|---|---|---|
| A. 隐藏桌面图标（右键桌面 → 查看 → 显示桌面图标） | 视觉上组件就是"最底层" | 0 |
| B-1. 文字颜色自适应（= P3） | 任何壁纸都清晰 | 中 |
| B-2. 贴边吸附 | 更像"桌面的一部分" | 低 |
| B-3. 描边 / 降低不透明度 | 提升可读性 | 低 |
| C. 旧 shell 夹层代码 | 只在旧版 Windows 有效 | 高、本机无法验证 |

### 19.5 如果强行做嵌入，会付出什么代价

| 维度 | 代价 |
|---|---|
| 稳定性 | 需要向 `Progman` 发 `0x052C`、`SetParent` 到 shell 窗口；一旦 Explorer 重启，父窗口被销毁，组件会**一起消失**或变成孤儿窗口，必须写重挂逻辑 |
| 兼容性 | 新 shell（Win11 24H2+）**根本没有可挂的图标层**；同一份代码在旧 shell 能跑、新 shell 只能退化，等于维护两套行为 |
| 安全 | 想拿到"图标之下"的唯一办法是注入 explorer / hook 它的 DirectComposition，属于**高危操作**（用户已明确拒绝） |
| 维护 | `SetParent` 后窗口消息循环、DPI、命中测试、`UpdateLayeredWindow` 都要重新验证；每次 Windows 大版本更新都可能失效 |
| 收益 | 仅"视觉上更像桌面的一部分"，且用"隐藏桌面图标"就能零成本达到近似效果 |

> 结论：**收益极小、代价极大**，因此不做。当前"壁纸之上、应用之下"已经是本机结构能做到的最优位置。

---

## §20 版本与决策记录

| 时间 | 事件 | 结果 |
|---|---|---|
| 2026-11-24 之前 | 项目已有可运行版本（真透明 + 拖拽 + 设置面板） | 基线 `build\base.png` |
| 11-24 早 | 用户否决四种"优化"方案，原话「不做任何危险行为的优化」 | 转向只读核查 |
| 11-24 | 产出 `docs/PLAN-2026-11-24.md`（P0–P5 分级 + 安全边界 + 验收口径 + 5 个决策点），纠正指南 3 处失真 | 计划落盘 |
| 11-24 | 用户以 P0/P1/P2 表格截图视为批准 | 开始执行 |
| 11-24 | **P0 文档纠偏**：README 6 处 + PROJECT_GUIDE §4.1/§11/§16.4 | 文档与代码一致 |
| 11-24 | **P1 死代码清理**：删 16 个死函数 + 11 个死全局 + `TRANSPARENT_KEY`，3182 → 2793 行 | 0 error；151 warning 全为既有噪声 |
| 11-24 | **P2 设置面板空档填平**（方案 A，上移 48px），`SET_H` 500 → **516** | 24/24 命中断言通过 |
| 11-24 | 重建并交付 exe + 安装包 | 隔离测试 0 像素差异 |
| 11-24 晚 | 用户「字体用系统字体」→ `ReadSystemLabelFont` + `TrimFace` | 首版字号错误（12px） |
| 11-24 晚 | 用户截图质问「你觉得一样吗」→ 定位 `lfHeight` 单位问题 | 改 15px，差 77 px / max 1 |
| 11-24 深夜 | 用户要求「窗口嵌入壁纸层（WorkerW）」→ 探针实测 | **本机不可行**，产出评估文档 |
| 11-24 | 用户要求「整个项目最详细 MD」 | **本文** |

### 20.1 已被否决 / 已排除的方向

| 方向 | 否决理由 |
|---|---|
| 截屏 + 模糊作为默认 | 动态壁纸穿帮、CPU 开销 |
| 颜色键透明 | 边缘毛边、做不到半透明 |
| `SetParent` 到 Progman / WorkerW | 本机无宿主，注入 explorer 属危险操作 |
| 注入 explorer / hook DirectComposition | 危险行为，用户明确拒绝 |
| 删死代码时"顺手重构" | 用户要求"不做危险行为的优化"，只做锚点可验证的删除 |

---

## §21 附录

### 21.1 配置文件样例

```ini
[GlassDock]
folder=C:\Users\a1320\Desktop
width=420
height=560
x=2110
y=75
opacity=36
accent=808080
topmost=0
autostart=0
foldersFirst=1
showExt=1
refreshHz=60
```

### 21.2 构建产物核对清单

| 检查项 | 期望 |
|---|---|
| `GlassDock.exe` 存在且 > 500 KB | 532,480 B |
| `GlassDock.exe.manifest` 与 exe 同目录 | 必须，否则无 Common Controls v6 / DPI |
| `installer\setup.exe` 与 `release\...exe` 同大小 | 972,288 B |
| 运行后托盘出现图标 | `AddTrayIcon` 成功 |
| 启动后 z 序在 Progman 之上 | `EnumWindows` 序号相邻 |

### 21.3 回滚点

| 文件 | 内容 |
|---|---|
| `build\GlassDock.cpp.bak.2026-11-24` | 清理前源码（3182 行 / 135,673 B） |
| `build\GlassDock.exe.bak.2026-11-24` | 清理前 exe |
| `build\GlassDock-Setup-1.0.0.exe.bak.2026-11-24` | 清理前安装包 |
| `build\GlassDock.base.exe` + `.manifest` | 编译基线 |
| `build\GlassDock.dev.exe` / `GlassDock.dev2.exe` | 中间版本 |
| `build\GlassDock.exe.p1p2.bak` / `...Setup...p1p2.bak` | P2 后、字体改造前 |

回滚方法：停进程 → 覆盖 exe → 启动。源码回滚：`Copy-Item build\GlassDock.cpp.bak.2026-11-24 src\GlassDock.cpp`。

### 21.4 FAQ

| 问 | 答 |
|---|---|
| 为什么窗口不能置于图标之下？ | 新 shell 里图标和壁纸都在 Progman 的合成表面内，没有独立图标层窗口（§11.4） |
| 为什么点面板上的桌面图标没反应？ | 面板整块是实心命中区（§16.1） |
| 为什么文字在浅色壁纸上看不清？ | `g_desktopLum` 从未更新（恒 0.5），默认配置下 `TextColorFor` 必出浅字（§16.4） |
| 改分辨率/缩放后字变小？ | 系统级 DPI 感知，需重启程序（§18.2） |
| 为什么没有关闭按钮？ | 桌面组件防手滑（`GetButtons` 第三槽恒空） |
| 热键为什么是"显示"而不是"切换"？ | `ToggleWidget` 实际等价 `ShowWidget`（§12.2） |
| 能同时开两个吗？ | 不能，`Local\GlassDock_SingleInstance` |
| 配置在哪？ | `%APPDATA%\GlassDock\config.ini` |
| 卸载会删我的文件夹吗？ | 不会，只删安装目录与配置目录 |

### 21.5 术语表

| 术语 | 含义 |
|---|---|
| ULW | `UpdateLayeredWindow`，逐像素 alpha 提交 API |
| PARGB | 预乘 alpha 的 32bpp 格式（`PixelFormat32bppPARGB`） |
| coverage | 栅格化得到的"该像素被字形覆盖的比例"，0–255 |
| tint | 面板底色（accent + opacity 合成的半透明层） |
| host | 桌面宿主窗口（Progman 或 WorkerW） |
| guard | `TIMER_GUARD` 守护定时器 |
| 惰性设置 | 能存能显示但当前不影响渲染的配置项（如 `refreshHz`） |

### 21.6 文档维护建议

1. **本文是唯一"当前真相"**；`PROJECT_GUIDE.md` 的行号与已删符号已过时，建议在其顶部加一行指针指向本文，或直接归档。
2. 任何改动 `GlassDock.cpp` 的提交，都应同步更新 §17 的行号索引（否则下次接手又要重新 grep）。
3. 新增设置项的完整步骤见 §21.7，照做可保证"布局 / 命中 / 持久化 / 实时预览"四件套不漏。

### 21.7 如何新增一个设置项（照抄清单）

以"新增『行高』滑块"为例：

| 步 | 文件 / 位置 | 做什么 |
|---|---|---|
| 1 | `Config` 39–50 | 加字段 + 默认值 |
| 2 | `LoadConfig` 112 / `SaveConfig` 146 | 读 + 钳位 + 写 |
| 3 | 常量区 1433 附近 | 若影响面板高度，调 `SET_H` |
| 4 | `R_*` 布局 1442–1455 | 加矩形函数；**下方所有元素要整体下移** |
| 5 | `SH_*` 枚举 1460 | 加命中 id |
| 6 | `SetHit` 1930 | 加判定分支 |
| 7 | `PaintSettings` 1593 | 加绘制（标签+轨道+数值） |
| 8 | `SettingsProc` 2040 | `WM_LBUTTONDOWN` / `WM_MOUSEMOVE` 加拖动逻辑 |
| 9 | `SetApplyLive` 1971 | 保证改动即时生效 |
| 10 | `build\shot.exe hit` | 补断言，跑 24+N 全绿 |
| 11 | `build\shot.exe settings` | 出图目检，确认无重叠/穿字 |
| 12 | 本文 §10.2 / §17 | 更新布局表与行号 |

---

## 结语

这份文档的目标是：**任何人（包括三个月后的你）只读这一份，就能改代码、能发版、能排障、能说清"为什么这么设计"**。

- 想知道"为什么不能嵌到图标下面" → §11.4 + 评估文档
- 想知道"为什么字这么大/这么小" → §6.5 + §16.2
- 想知道"为什么点不到后面的图标" → §16.1
- 想知道"下一步能做什么" → §19

> 最后一条经验：这个项目所有"看起来诡异"的设计（alpha 钳 1、恒 `HTCLIENT`、不置顶、没有关闭按钮、`S()` 全局缩放），都是被真实问题逼出来的。**改之前先读对应章节的"为什么"，否则一定会踩回同一个坑。**










