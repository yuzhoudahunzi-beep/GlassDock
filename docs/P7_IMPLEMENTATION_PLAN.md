# GlassDock P7 — Desktop Persistence Upgrade 实施计划

> **状态**：第一阶段（审计）已完成，等待确认后进入编码
> **日期**：2026-11-24
> **基准代码**：`src/GlassDock.cpp` 2839 行（19:54 版本，未做任何修改）
> **参考文档**：`docs/GLASSDOCK-FULL-MANUAL.md`（与代码核对一致，仅 1 处数字过时，见 §0.4）

---

## §0 第一阶段审计结论

### 0.1 审计动作（全部只读 / 备份）

| # | 动作 | 结果 |
|---|---|---|
| 1 | 项目目录全清单 | 见 `docs/GLASSDOCK-FULL-MANUAL.md` §3；根目录无异常新增文件 |
| 2 | 重读 GLASSDOCK-FULL-MANUAL.md | 与源码核对：函数行号、常量、行为描述全部一致 |
| 3 | 核对 src/GlassDock.cpp | 19:54 未变（121,154 B）；未发现文档未记载的代码 |
| 4 | 核对 build.ps1 / setup.cpp / app.manifest | 与手册 §14 / §13 一致 |
| 5 | 现有备份 | `build\*.bak*` 5 个均在，未覆盖 |
| 6 | **P7 独立备份** | `build\P7_backup\` 已建（16 个文件，见 0.3） |
| 7 | 基线编译（不碰生产 exe） | 0 error / 0 warning（默认参数）；`-Wall -Wextra` 177 条 = 167 结构体初始化噪声 + 10 条已知真警告 |
| 8 | L2 离屏基线 | `shot.exe hit` **24/24**；settings/main 基线 PNG 已存 |
| 9 | L3 真进程基线 | pwcap2 抓运行中生产实例（PID 80712）→ `widget_baseline.png`（548×794 @ (2002,6)），**未打扰进程** |

### 0.2 关键差距确认（本次 P7 的改造对象）

| # | 差距 | 位置 | 说明 |
|---|---|---|---|
| G1 | 宿主查找会**兜底到任意 WorkerW** | `GetDesktopWorkerW` 1071 | 本机 `FindWindowW("Progman")` 实测返回 0，会落到 170×47 不可见小窗 |
| G2 | `SitOnDesktop` 返回 void，宿主缺失时**仍然 SetWindowPos(HWND_BOTTOM)** | 1091 | 最危险的兜底路径 |
| G3 | `WM_SIZE(SIZE_MINIMIZED)` **无条件** ShowWindow+SitOnDesktop+Repaint | 2457 | 可能与 TIMER_GUARD 互触发；未查 `g_userHidden` |
| G4 | TIMER_GUARD 每 5 tick **无条件** SitOnDesktop | 2479 | 无状态比较，可能无意义 SetWindowPos |
| G5 | **无** `TaskbarCreated` 监听 | 全文件 | Explorer 重启后托盘/驻留不恢复 |
| G6 | **无** `WM_DISPLAYCHANGE` / `WM_DPICHANGED` 处理 | 全文件 | 分辨率/显示器变化不做位置修正 |
| G7 | 状态只有 `g_userHidden` 一个布尔 | 2353 | 无法区分"系统最小化 / 恢复中 / 宿主不可用" |
| G8 | 设置面板无"隐藏桌面图标"辅助说明 | `PaintSettings` 1593 | P7 第八阶段要求补充（纯文字） |
| G9 | 手册 §14.6「151 条 warning」数字过时 | 文档 | 实测 177（167 噪声 + 10 真警告），P7 收尾时订正 |

### 0.3 P7 备份内容（`build/P7_backup/`，16 个文件）

```
GlassDock.cpp / crisptext.h / build.ps1 / setup.cpp / app.manifest
GlassDock.exe (532,480 B) + GlassDock.exe.manifest      ← 生产二进制原样
GlassDock.P7.base.exe / GlassDock.P7.base.wall.exe       ← 基线编译产物
baseline_warnings.txt / baseline_wall_warnings.txt       ← 编译基线
hit_baseline.txt (24/24)                                 ← 命中基线
settings_baseline.png / main_baseline.png / widget_baseline.png  ← 渲染基线
config.sample.ini                                        ← 配置样例（含新键 debugLog）
```

原始 `.bak.2026-11-24` / `.p1p2.bak` 均未动。

### 0.4 文档一致性说明

`docs/GLASSDOCK-FULL-MANUAL.md` 唯一过时点：§14.6「151 条 warning」应为「177 条（167 结构体噪声 + 10 真警告）」；该数字与 P7 代码无关，将在 P7 最终交付时一并订正（列入 P7_CHANGELOG）。

---

## §1 边界确认（先读这里）

本计划**只做**以下五件事，其余一律不动：

1. **宿主识别重构**（G1）——去掉"任意 WorkerW"兜底；
2. **SitOnDesktop 安全化**（G2）——宿主不明时不做 Z 序操作；
3. **Win+D 状态机 + 恢复调度**（G3/G7）——区分用户隐藏/系统最小化/宿主不可用；
4. **守卫定时器拆分 + 条件化**（G4）——状态不变不 SetWindowPos、不 Repaint；
5. **Explorer 重启 + 显示变化恢复**（G5/G6）——TaskbarCreated、WM_DISPLAYCHANGE；
6. **设置面板加一段纯文字说明**（G8，第八阶段授权）。

**不做**（与任务书第九阶段逐条对齐）：

| 禁止项 | 本计划 |
|---|---|
| 注入 explorer / Hook / 改 DLL / 改内存 / 改图标排列库 | ❌ 不做 |
| 通过不明句柄 SetParent | ❌ 不做（主窗口**保持 WS_POPUP**，任何 SetParent 都不出现） |
| 无条件 HWND_TOPMOST | ❌ 不做（SitOnDesktop 仍清 TOPMOST；新增代码零 TOPMOST 调用） |
| 无限循环强恢复 / 额外守护进程 / 多线程 | ❌ 不做（保持单线程 GetMessage 循环） |
| 删除现有功能 / 重构渲染管线 / 改文件双击逻辑 | ❌ 不做 |
| 擅自改设置面板布局 | ✅ 唯一例外：第八阶段授权的**一段静态说明文字**（不新增控件、不改命中区） |
| 覆盖生产版本与备份 | ❌ 不做（P7 编码产出 `release\GlassDock P7.exe`，生产 `GlassDock.exe` 与备份不动） |
| 改注册表桌面图标布局 | ❌ 不做 |
| 谎报兼容性 | ❌ 不做（仅本机实测结果，其余系统标注"未验证"） |

---

## §2 修改设计（按任务书阶段）

### 第二阶段：宿主识别重构（G1）

**新增**

```cpp
struct DesktopHostInfo {
    HWND  hwnd = NULL;
    bool  valid = false;
    bool  isLegacyDesktop = false;   // 带 SHELLDLL_DefView 的旧式桌面
    bool  isProgman = false;         // 标题 Program Manager 的新式外壳
    DWORD processId = 0;
};

static DesktopHostInfo ProbeDesktopHost();   // 详见下文
```

**查找顺序（严格三级，无兜底）**

1. `EnumWindows`：顶层窗口中有 `SHELLDLL_DefView` 子窗者 → `isLegacyDesktop=true`，记 pid、rect、可见性、子窗数；
2. `EnumWindows`：标题 == `L"Program Manager"` → `isProgman=true`，记录类名/标题/HWND/父窗/可见/rect/子窗数；
3. 都没有 → `{ valid=false }`，**返回明确的失败状态**。

`GetDesktopWorkerW()` 改为薄封装：`return ProbeDesktopHost().hwnd;`（行为不变、无返回值类型改动，最小侵入）。**删除** `FindWindowExW("WorkerW")` 循环与 `FindWindowW("WorkerW")` 兜底。

**诊断日志（可关闭）**

- `Config` 新增 `bool debugLog = false`（`GetIniInt(L"debugLog", 0)`；**不**写入 SaveConfig 的 UI 路径，仅在用户手改 ini 时生效；命令行 `--log` 强制开启）。
- `static void DiagLog(const wchar_t* fmt, ...)`：写 `%APPDATA%\GlassDock\p7.log`，超过 256 KB 截断重开。
- **只记录状态跃迁**：宿主探测结果变化、放置结果、TaskbarCreated、恢复动作、显示变化——**绝不每秒输出**（TIMER_GUARD 分支无日志）。
- 默认关闭（`debugLog=0`），零开销。

### 第三阶段：SitOnDesktop 安全改造（G2）

**新增返回类型**

```cpp
enum class DesktopPlacementResult { Success, HostUnavailable, WindowInvalid, PlacementFailed };
```

**逻辑**

```
SitOnDesktop() -> DesktopPlacementResult:
  if (!IsWindow(g_hwnd)) return WindowInvalid;
  exstyle: 保持 NOACTIVATE|TOOLWINDOW，清除 TOPMOST（同现状）;
  host = ProbeDesktopHost();
  if (!host.valid) { DiagLog(HostUnavailable); return HostUnavailable; }   // 不动 Z 序、不隐藏
  insertAfter = GetWindow(host.hwnd, GW_HWNDPREV);
  if (!insertAfter) { DiagLog(no-prev); return PlacementFailed; }          // 不再 HWND_TOP/HWND_BOTTOM
  SetWindowPos(hwnd, insertAfter, ..., SWP_NOMOVE|NOSIZE|NOACTIVATE|SHOWWINDOW);
  return Success;
```

- 保持 `WS_POPUP + WS_EX_LAYERED` 架构，**不出现 SetParent**；
- 保持 `SWP_SHOWWINDOW` 原子显示（现注释已论证，不动）；
- 所有调用点改为**检查返回值**，失败时不做任何破坏性动作。

### 第四阶段：Win+D 状态机（G3/G7）

**新增状态**

```cpp
enum class WidgetVisibilityState {
    VisibleOnDesktop, UserHidden, SystemMinimized, Recovering, HostUnavailable
};
static WidgetVisibilityState g_visState = VisibleOnDesktop;
```

**恢复调度（单线程、无轮询）**

```cpp
static bool  g_recoveryPending = false;
static DWORD g_lastRecoveryTick = 0;
#define WM_RECOVER (WM_APP + 98)
```

- `RequestRecovery()`：`if (g_userHidden || g_dragging || g_resizing) return;` `if (g_recoveryPending) return;`（防重复投递）→ `g_recoveryPending=true; PostMessage(hwnd, WM_RECOVER, 0, 0);`
- `WM_RECOVER` 处理：清 pending → 再查 `g_userHidden` / 句柄有效 → `SitOnDesktop()`；返回 `Success` 才 `Repaint()`。
- `WM_SIZE(SIZE_MINIMIZED)` 重写：**不再直接 ShowWindow**，只置 `g_visState=SystemMinimized` + `RequestRecovery()`。
- `ShowWidget/HideWidget/ToggleWidget` 同步维护 `g_visState`（UserHidden ↔ VisibleOnDesktop）。
- 宿主探测失败时 `g_visState=HostUnavailable`，守卫只重试探测（§5），**不**反复恢复。

**消除循环**：TIMER_GUARD 的可见性恢复也改走 `RequestRecovery()`（同一条单飞路径），WM_SIZE 与 TIMER 不再各自 ShowWindow。

### 第五阶段：守卫定时器拆分（G4）

`WM_TIMER / TIMER_GUARD` 内拆成四个小函数（命名按任务书建议）：

```
CheckVisibility():   !g_userHidden && (!visible || iconic) && !dragging && !resizing
                     → RequestRecovery()
CheckDesktopHost():  每 30 tick（30s）或宿主句柄无效时 → ProbeDesktopHost()；
                     宿主 HWND 变化且 valid → RefreshDesktopPlacement()
CheckZOrder():       每 5 tick → EnumWindows 比较 widget 与 host 的序号；
                     仅当 widget 位于 host 之下时才 SitOnDesktop()（状态比较，命中才动）
RecoverWidget() / RefreshDesktopPlacement(): 前两者的动作封装，内部复用 SitOnDesktop
```

- **不做**"每 5 秒无条件 SetWindowPos"；
- **不做**无条件 `Repaint`（只有真正执行了 show/重锚或表面失效才重绘）；
- 拖动/缩放期间（`g_dragging || g_resizing`）全部跳过。

### 第六阶段：Explorer 重启（G5）

```cpp
static UINT g_msgTaskbarCreated = 0;   // wWinMain: RegisterWindowMessageW(L"TaskbarCreated")
```

`WndProc` 增加分支（与 `WM_SETTINGCHANGE` 同级）：

```
if (msg == g_msgTaskbarCreated && msg != 0) {
    DiagLog("TaskbarCreated");
    if (IsWindow(g_hwnd)) {
        重新探测宿主；
        if (!g_trayAdded) AddTrayIcon();       // 防重复：已有标志位判断
        if (!g_userHidden) { SitOnDesktop(); Repaint(); }
    }
    return 0;
}
```

- **不销毁窗口、不重建实例、不重复注册热键/定时器**（热键与计时器绑定窗口线程，Explorer 重启不影响）；
- 配置（文件夹/尺寸/位置/透明度/主题）由既有 cfg 机制天然保留，不重新加载覆盖；
- `RegisterShellHookWindow` **暂不引入**；仅当实测 TaskbarCreated 在本机不触发时，再作为备选上报（需你拍板）。

### 第七阶段：显示器与 DPI（G6）

```cpp
case WM_DISPLAYCHANGE:   // 分辨率/显示器拓扑变化
    KeepOnScreen();       // 既有函数，只有越界才修正并 SavePos
    SitOnDesktop();       // 重新锚定（宿主可能已重建）
    return 0;
```

- 系统级 DPI 感知下**不会**收到 `WM_DPICHANGED`（该消息要求 per-monitor v2，属 P5 范围）；在 `WM_SETTINGCHANGE` 里附带重跑 `KeepOnScreen`（现有分支扩一行）；
- 位置仅在**超出可用显示区域**时才修正并落盘（任务书要求），不覆盖用户手动位置；
- 多显示器断开导致窗口在不可见区域 → `KeepOnScreen` 拉回主屏可见区。

### 第八阶段：隐藏桌面图标备用模式（G8）

- **主模式不变**：保留桌面图标，GlassDock 与图标共存；
- 设置面板底部提示区改为**两行静态文字**（无新控件、无命中区，`shot.exe hit` 仍 24 项断言）：

```
行1：桌面小组件 · 只显示在桌面，不遮挡其他窗口        （y=414，沿用现有提示）
行2：隐藏系统图标：桌面右键 → 查看 → 显示桌面图标      （y=430，新增，约 23 字 ≤ 可用宽）
```

- 分界线（452）与按钮（460）不动，`SET_H=516` 不变；若离屏渲染发现穿字，优先缩短行 2 文案，**不**改面板尺寸；
- 不注入、不 Hook、不改 Explorer 状态、不动注册表（仅文字指引，用户自行在 Windows 设置操作）。

### 第十~十一阶段：测试与回归（详见 P7_TEST_REPORT.md）

按任务书 §10 逐项执行：L1（build.ps1 + -Wall 对比基线 177 条）、L2（shot.exe hit 24/24 + 渲染基线比对）、L3（隔离 APPDATA + pwcap2 抓图比对）、Win+D 十项矩阵、桌面图标互操作、回归清单 15 项。

---

## §3 文件与交付物

| 阶段 | 动作 |
|---|---|
| 现在 | ✅ 审计 + 备份 + 基线（本计划产出） |
| 待确认 | 本计划经你确认无越界后 → 开始编码 |
| 编码 | 只改 `src/GlassDock.cpp`（预计 +200/-40 行左右）；**不改** crisptext.h、setup.cpp、build.ps1、manifest |
| 测试 | `build\P7_backup\` 内完成 L1/L2/L3 + 十项矩阵 + 回归 |
| 交付 | `release\GlassDock P7.exe`（+ 同目录 manifest）；`docs/P7_IMPLEMENTATION_PLAN.md`（本文件）、`docs/P7_TEST_REPORT.md`、`docs/P7_CHANGELOG.md`；P7 测试截图入 `build\P7_backup\` |
| 收尾 | 订正手册 §14.6 warning 数字（列入 CHANGELOG）；生产 `GlassDock.exe` 仅在你说"替换"后才由 P7 版本替换 |

## §4 风险与回滚

- 全部改动集中在 3 个函数 + 4 个新消息分支 + 状态机字段，可独立回滚；
- 回滚点：`build\P7_backup\GlassDock.cpp`（本次审计原件）+ `build\GlassDock.cpp.bak.2026-11-24`（更早）；
- 若 Win+D 十项矩阵中任何一项出现"组件遮挡普通应用"或"消失不可恢复"，立即停止并报告，不进入交付。

---

## §5 执行记录（2026-11-24 编码期实测修正）

用户批准边界后按 §2 编码，L3 实测暴露两处设计需按「以实际窗口探测决定策略」修正，均已落码并全量回归：

1. **锚定目标从 GW_HWNDPREV(宿主) 改为「Progman 之上」**：本机 26H2 的 WorkerW 带碎裂（Progman 在 Z 序最底，WorkerW 残留散布其上、中间夹应用窗口），按原设计锚「最高 WorkerW」会把 widget 停在应用窗口之上（实测盖住 Chrome）。最终策略：widget 停靠宿主 Progman 上方紧邻（Z 序最底，与桌面图标同级），插入点三级回退（GW_HWNDPREV(host) → anchorAbove → GW_HWNDPREV(anchor) → 自身链）。
2. **CheckZOrder 判据改为「被埋检测」**：widget 停靠 Progman 之上即正确；仅当 EnumWindows 顺序中 widget 位于宿主之下（被埋到壁纸带下）才重锚。原「邻居比较」判据会被本进程辅助窗口（GDI+ hook、TSF MSCTFIME）与带碎裂干扰，造成每 5 秒重锚循环（实测发现并修复）。

另两处实测发现：MinGW vswprintf 的 %s 为窄语义（宽字符串用 %ls）；FindWindowW("GlassDockWidget") 在本机返回 0（分层工具窗口），测试一律用 EnumWindows 定位。
