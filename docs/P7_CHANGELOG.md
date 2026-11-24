# GlassDock P7 变更日志

> 规则：每完成一个阶段追加一条记录。

## 2026-11-24 · 第一阶段：审计 / 备份 / 基线（已完成）

**无生产代码变更。**

- 审计：源码 2839 行与 `docs/GLASSDOCK-FULL-MANUAL.md` 一致；确认 8 项差距（G1–G9，见 P7_IMPLEMENTATION_PLAN.md §0.2）
- 新建 `build/P7_backup/`（16 文件）：源码/构建脚本/生产 exe 与 manifest/基线编译产物/基线警告/hit 基线/三张基线 PNG/配置样例
- 基线：L1 0 error 0 warning（-Wall -Wextra 177 条全为既有项）；L2 hit 24/24；L3 抓生产实例 548×794
- 产出：`docs/P7_IMPLEMENTATION_PLAN.md`、`docs/P7_TEST_REPORT.md`、`docs/P7_CHANGELOG.md`（本文件）

## 2026-11-24 · 第二~八阶段：编码（已完成）

**全部改动集中在 `src/GlassDock.cpp`**（2839 → 3090 行左右）。生产 exe/安装包/运行实例未触碰。

### 第二阶段：宿主识别（G1）

- 新增 `DesktopHostInfo{hwnd, anchor, anchorAbove, valid, isLegacyDesktop, isProgman, processId}`、`HostScanCtx`、`HostScanProc`、`ProbeDesktopHost()`
- 三级查找：EnumWindows 找含 `SHELLDLL_DefView` 子窗的窗口 → 标题 "Program Manager" → 明确失败（valid=false）。**删除任意 WorkerW 兜底**
- 同步收集：最高 WorkerW（`anchor`）与它枚举序前一位的窗口（`anchorAbove`），供诊断与插入点回退
- 本机实测（build 26300）：宿主 = Progman（legacy=1，DefView 层存在）；`FindWindowW("Progman")` 仍为 0，EnumWindows 路径可靠

### 第三阶段：SitOnDesktop 安全化（G2）

- 返回 `enum class DesktopPlacementResult{Success, HostUnavailable, WindowInvalid, PlacementFailed}`
- 宿主不明 / 插入点无效时**不做任何 Z 序操作**（废除 HWND_BOTTOM/HWND_TOP 兜底）
- 插入点三级回退：`GW_HWNDPREV(host)` → `anchorAbove` → `GW_HWNDPREV(anchor)` → 自身上方邻居（隐藏态恢复用）

### 第四阶段：Win+D 状态机（G3、G7）

- `enum class WidgetVisibilityState{VisibleOnDesktop, UserHidden, SystemMinimized, Recovering, HostUnavailable}` + `g_visState`
- `#define WM_RECOVER (WM_APP+98)`；`g_recoveryPending` 单飞标志；`RequestRecovery()`（用户隐藏/拖动/缩放时放弃）
- WM_SIZE(SIZE_MINIMIZED) 只记录状态 + 单飞恢复，不再同步 ShowWindow
- ShowWidget/HideWidget 同步状态赋值

### 第五阶段：守卫拆分（G4）

- `CheckVisibility()` 每 tick（不可见/最小化 → RequestRecovery）
- `CheckZOrder()` 每 5 tick：EnumWindows 比较 widget 与宿主顺序，**仅"被埋到宿主之下"才重锚**（实测驱动：widget 停靠 Progman 之上即正确，避免 WorkerW 带碎裂导致的锚定误判）
- `CheckDesktopHost()` 每 30 tick：宿主/锚点变化才重锚
- `GuardTick()` 总入口；用户隐藏/拖动/缩放全程零操作

### 第六阶段：Explorer 重启恢复（G5）

- `RegisterWindowMessageW(L"TaskbarCreated")` + WndProc default 分支：重探测宿主、`if(!g_trayAdded) AddTrayIcon()`、非隐藏态 SitOnDesktop+Repaint
- 不销毁窗口、不重建实例、不重复注册热键定时器；未引入 RegisterShellHookWindow（TaskbarCreated 实测有效触发）

### 第七阶段：显示变化（G6）

- `WM_DISPLAYCHANGE` → DiagLog + KeepOnScreen + SitOnDesktop
- `WM_SETTINGCHANGE` 增加 KeepOnScreen（渲染背景保持原逻辑）
- DPI 消息：系统级 DPI 感知下不会来（per-monitor v2 属 P5，不在 P7 范围）

### 第八阶段：设置面板说明（G8）

- 底部单行提示改为**两行纯文字**（y=414 / y=431，S(16) 高）："隐藏系统图标：桌面右键 → 查看 → 显示桌面图标"
- 无新控件、无命中区，SET_H=516 不变

### 附加：诊断日志（G9 配套）

- `Config.debugLog`（ini-only，默认 0，不写回）+ 命令行 `--log`
- `g_diagEnabled` + `DiagLog()`：仅记状态跃迁（boot/host 变化/重锚/TaskbarCreated/显示变化），>256KB 截断重开
- **工具链坑**：本机 MinGW-w64 16.2 + UCRT 的 `vswprintf`/`swprintf` `%s` 为窄字符语义，宽字符串一律 `%ls`（项目既有代码用 `wsprintfW`+`%s` 正常）

## 2026-11-24 · 第十阶段：测试（已完成）

- L1：默认 0 error / 0 warning；`-Wall -Wextra` 180 = 基线 177 + 3 条 P7 新增结构体噪声，真实警告与基线逐条一致（净增 0）
- L2：hit 24/24；main 0 像素差异；settings 仅底部提示区差异（同环境新旧对照；基线 PNG 与复测环境存在桌面文件/字体变化，已用旧版 shot.exe 同刻渲染排除代码因素）
- L3：隔离 APPDATA 真进程抓图正常；不碰注册表
- Win+D 十项矩阵 10/10（`Shell.Application.ToggleDesktop()` 触发，注明于报告）
- Explorer 重启：进程存活、TaskbarCreated 触发、宿主重探测重锚、托盘防重复
- WM_DISPLAYCHANGE 注入验证；单实例验证
- 回归 15 项全过

## 2026-11-24 · 第十二阶段：交付（已完成）

- `release\GlassDock P7.exe`（564,736 B，默认参数编译）
- 三文档回填：`docs/P7_TEST_REPORT.md`、`docs/P7_CHANGELOG.md`（本文件）、`docs/P7_IMPLEMENTATION_PLAN.md`
- 手册订正：`docs/GLASSDOCK-FULL-MANUAL.md` §14.6 warning 数字 151 → 177（167 结构体噪声 + 10 真警告）
- **生产替换**：未执行（按计划仅用户确认后替换 `GlassDock.exe`）；测试期间停掉的生产实例已恢复

## 2026-11-24 · 事故记录：测试写入真实配置（已恢复）

- 现象：22:07:56 用户真实 config.ini 被写为测试值（accent=808080 默认灰、autostart=0、refreshHz=60、窗口 420×596），用户"质感"丢失
- 根因：F7 类自动化交互（点色板/自启动/频率/拖窗口）在一次未隔离 APPDATA 的会话中对真实配置执行了 SaveConfig（WM_DESTROY 无条件保存）
- 恢复：以 11/23 程序自动备份 config.ini.bak 还原（accent=243447、autostart=1），窗口还原 (2002,6) 548×794 并固化进 config（UTF-16 LE）；自启动 Run 注册表项确认存在；抓图验证设置面板 HEX=#243447、路径=AI短片、7 项文件完整
- 教训：**任何自动化交互测试（F7）必须先在隔离 APPDATA 下执行并核对 C:\Users\a1320\AppData\Roaming**；真实环境只允许读、不允许写

---

## P8 视觉精修（2026-11-24 晚，用户验收驱动）

用户反馈：设置面板底部两行提示多余；默认高度应约 611px；行距偏挤；字体锯齿重、
相邻字大小不均、边缘粗糙；质感需再提升。

改动（`src/crisptext.h` + `src/GlassDock.cpp`）：
1. **平滑字体**：`crisp::Rasterize` 改为 3x 超采样 —— 先以 3 倍像素高度光栅化整行，
   再 3x3 盒式平均降采样回设备像素。消除 1x hinting 的像素网格吸附（相邻字高矮/
   粗细不一的根源），边缘成为连续 alpha 渐变（"顺滑"），仍只取灰度覆盖通道，
   无 ClearType 彩色镶边。尺寸自适应降级（box*3 超 4096x1536 时降 2x/1x）。
2. **删除提示行**：移除设置面板底部两行静态文案（"桌面小组件…" / "隐藏系统图标…"），
   底部分隔线与按钮组上移 28px，`SET_H` 516→488。
3. **默认高度**：`Config()` 与 ini 缺省 560→611（用户拖拽验收值）。
4. **行距**：`RowH()` S(22)→S(24)。
5. **质感**：`DrawFrame` 顶部 sheen 与底部环境阴影改为圆角裁剪（光随圆角走）；
   边框 alpha 90→100、内环 22→26；新增内顶 1px catch-light 与内底 1px 阴影，
   读出"抛光玻璃厚度"。

验证：编译 0 error；替换 `D:\GlassDock\GlassDock.exe` 与项目副本；重启抓图
`build/P7_backup/p8_widget.png`（420x611）、`p8_settings.png`（500x610，无提示行）、
`p8_zoom_row.png`（4x nearest 放大检查边缘 alpha 渐变）。

### P8.1 间距微调（用户："离得太近了稍微拉开一点，两张都是"）
- 组件：新增 `ListTop() = TitleH() + S(5)`，标题分隔线与首行之间留出呼吸区；
  `RowH()` S(24)→S(26)。DrawList / DrawScrollbar / ClampScroll / HitRow / hover /
  折叠点击全部改用 ListTop()，命中与渲染保持一致。
- 设置面板：Hz 选项底边(424)原先紧贴分隔线 → 分隔线 424→436、按钮组与
  "改动即时生效" 432→448、`SET_H` 488→504，线上下各留 12px。
- 验证：编译 0 error；替换两处 exe 重启；抓图 p9_widget / p9_settings，
  裁剪 p9_header / p9_setbottom 确认间距。未改用户 config（用户已自行拖到 409x654）。

## P9 头部精简 + 实时刷新（2026-11-24 晚）

用户："七项这个东西删了，刷新改成实时的，顺便把刷新图标删了，设置图标保持和
左边的文件夹名在一条水平线上。"

1. **删计数 chip**：DrawTitle 中 "N 项" 徽章整块移除；标题可用宽度放宽到只剩齿轮占位。
2. **删刷新按钮**：GetButtons 只保留 settings（index 1），index 0/2 置空矩形；
   点击 switch 移除 case 0；DrawTitle 单按钮绘制。
3. **齿轮对齐**：按钮 y 由固定 S(7) 改为 `(TitleH()-s)/2`，与文件夹名同一水平中线。
4. **实时刷新**：新增 ReadDirectoryChangesW 递归监听根目录（FILE_NAME/DIR_NAME/
   LAST_WRITE/SIZE），独立线程 + overlapped event；通知经 WM_LIVE(WM_APP+97) +
   TIMER_LIVE(4) 120ms 去抖合并为一次 LiveReload()；LiveReload 保留展开目录集合
   与滚动位置（CollectExpanded/RestoreExpanded），列表不跳。Reload()/初始化/
   换目录时 StartWatch() 按路径去重重启监听；WM_DESTROY StopWatch()。
   缓冲区用 DWORD 数组保证 4 字节对齐（alignas 写在 decl-specifier 中间会编译报错）。

验证：编译 0 error；替换两处 exe 重启；实测在监听目录新建/~删除临时文件，
抓图 p10_live_added.png（8 行自动出现）与 p10_live_removed.png（自动回到 7 行），
临时文件已清理；p10_header.png 确认无 chip、无刷新、齿轮与标题同中线。

## P10 刷新率固定 60Hz + 移除选项（留白）

用户："刷新率固定在60hz吧，然后去掉这个选项吧，空着的地方就空着，留白。"

- `LoadConfig` 中 `cfg.refreshHz` 不再读 ini，固定 60；`g_liveBgInterval` 恒为 16ms。
- 删除设置面板"拖动刷新率"整行（label + 6 个 Hz 胶囊）、`R_HzLabel/R_HzOption`、
  `g_hzPresets`、枚举 `SH_HZ0..5`、命中循环、点击分支与手型光标条件。
- 该行原占位**保留为空白**（用户要求留白），其余控件位置与面板高度不变。
- 附带修复：发现 config.ini 的 accent 曾漂回默认灰 808080（非本轮代码路径写入，
  磁盘时间戳早于本轮重启），已恢复用户主题色 243447 并验证重开面板保持。

验证：编译 0 error；替换两处 exe 重启；p11_settings_accent.png 确认无刷新率行、
留白在"开机自动启动"与底部分隔线之间、HEX=#243447。
