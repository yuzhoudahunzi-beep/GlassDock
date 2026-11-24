# 桌面壁纸层嵌入（WorkerW）可行性评估与平替方案

> 文档版本：v1.0 · 2026-11-24
> 对象项目：GlassDock（`D:\Doubao\GlassDock`，C++17 + Win32 + GDI+，MinGW-w64 单文件 exe）
> 触发需求：用户要求"固定在桌面 / 把窗口嵌进壁纸层，位于桌面图标之下、壁纸之上（Lively Wallpaper 原理）"
> 本文结论基于**本机实测**（只读探测 + 独立探针程序），未改动 GlassDock 一行代码

---

## 目录

| 章节 | 内容 |
|---|---|
| §0 | 一句话结论 |
| §1 | 需求拆解："固定在桌面"到底指什么（4 项能力 vs 现状） |
| §2 | 现状：GlassDock 现在是怎么贴到桌面的（机制 + 渲染 + 实测层级） |
| §3 | 原方案五步，逐条实测（0x052C / EnumWindows / SetParent / NOREDIRECTION / 重挂） |
| §4 | 为什么新 shell 上"图标之下"这个位置不存在 |
| §5 | 版本适配矩阵（Win10 → Win11 新 shell） |
| §6 | 就算挂上了会失去什么（影响清单） |
| §7 | 平替方案 A/B/C/D + E（旧 shell 参考实现骨架） |
| §8 | 建议路线 |
| §9 | 待办清单 |
| §10 | 附录：环境、探针源码与输出、结构探测命令、术语、5 个常见误区 |
| §11 | 变更记录 |

---

## 0. 一句话结论

**在原方案的经典形态下（0x052C → 找 WorkerW → SetParent），这台机器上做不到，原因是"目标窗口根本不存在"，不是代码难度问题。**

- 本机属于 Windows 11 新桌面（XAML / DirectComposition），`Progman` 是 `WS_EX_NOREDIRECTIONBITMAP` 且**零子窗口**；
- 枚举到的 13 个 `WorkerW` 全部不可见、尺寸只有 **170×47**（其中一个 0×0），**没有任何一个承载 `SHELLDLL_DefView`**；
- 壁纸和桌面图标住在**同一张合成表面**里，中间没有可插入的窗口缝隙；
- 连方案第一步 `FindWindowW(L"Progman", NULL)` 在本机都返回 0（`EnumWindows` 却找得到）。

**平替方案存在，而且有一条 10 秒就能验证、零代码零风险的路径**（见 §7：隐藏桌面图标）。

---

## 1. 需求拆解：用户说的"固定在桌面"到底指什么

"桌面组件"这个说法至少包含 4 个互不相同的能力，容易混为一谈：

| # | 能力 | 含义 | GlassDock 现状 |
|---|---|---|---|
| 1 | **不随应用切换消失** | 永远在桌面上，不被普通窗口永久覆盖 | ✅ 已达成（z 序锚定在桌面层） |
| 2 | **不抢焦点** | 点击不激活、不抢前台、不打断正在打字的应用 | ✅ 已达成（`WS_EX_NOACTIVATE` + `WM_NCHITTEST` 恒返 `HTCLIENT`） |
| 3 | **壁纸之上、应用之下** | 层级介于壁纸与普通窗口之间 | ✅ 已达成（窗口直接位于 `Progman` 之上） |
| 4 | **桌面图标之下** | 图标压在组件上，组件像"画在壁纸上" | ❌ **本机物理不可得** |

用户的原话"放在桌面图标之下、壁纸之上"对应的是 **第 4 条**。前三条现在已经成立——这一点很关键，因为它意味着**这次要动的是"层级"而不是"挂载"**。

---

## 2. 现状：GlassDock 现在是怎么"贴在桌面"的

### 2.1 机制（不是 SetParent）

`src\GlassDock.cpp`（2793 行版本）：

- `GetDesktopWorkerW()`（约 1071 行）：优先 `FindWindowW(L"Progman", NULL)`；拿不到就枚举顶层 `WorkerW`，找带 `SHELLDLL_DefView` 子窗的那个；再不行就返回"任意一个 `WorkerW`"。
- `SitOnDesktop()`（约 1091 行）：

```cpp
HWND host = GetDesktopWorkerW();          // Progman（或某个 WorkerW）
HWND insertAfter = HWND_BOTTOM;
if (host){
    insertAfter = GetWindow(host, GW_HWNDPREV);   // 取"壁纸宿主上面那个窗口"
    if (!insertAfter) insertAfter = HWND_TOP;
}
SetWindowPos(g_hwnd, insertAfter, 0,0,0,0,
             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
```

要点：**它是一个独立顶层窗口**，靠 z 序把自己摆到壁纸宿主的正上方；`HWND_BOTTOM` 只作为兜底（代码注释里明确写过：直接丢到底部有时会被 DWM 停到壁纸下面，出现"时有时无"）。

### 2.2 渲染

独立窗口 + `WS_EX_LAYERED` + `PixelFormat32bppPARGB` 位图 + `UpdateLayeredWindow(ULW_ALPHA)`：

```
PaintAll() → CommitLayered() → UpdateLayeredWindow   // 主窗口
PaintSettings() → CommitSetLayered() → UpdateLayeredWindow   // 设置窗口
```

这才是"真透明玻璃"的来源：**每个像素自带 alpha，桌面壁纸透过面板真实可见**，没有截屏、没有假模糊。

### 2.3 本机实测的层级位置

`EnumWindows` 按 z 序枚举（数字越小越靠上）：

| z 序 | 窗口 | 进程 | 尺寸 | 扩展样式 |
|---|---|---|---|---|
| 199 | `GlassDockWidget` | GlassDock | (2002,6)-(2550,800) | `0x8080080`（LAYERED + TOOLWINDOW + NOACTIVATE） |
| 200 | `Progman` | explorer | (0,0)-(2560,1440) | `0x200080`（**NOREDIRECTIONBITMAP** + TOOLWINDOW） |

> 也就是说：**当前组件已经位于 Progman 正上方**，即"壁纸之上"这一条已经满足；它同时也压在桌面图标上，因为图标也画在 Progman 里。

---

## 3. 原方案五个步骤，逐条实测结论

### 步骤 1：向 Progman 发送 `0x052C`

- **设计意图**：这是 Win10 / 旧 Win11 的"暗号"，让 Shell 额外生成一个承载壁纸的 `WorkerW`，从而获得一个可 `SetParent` 的宿主。
- **本机实测**：`FindWindowW(L"Progman", NULL)` **返回 0**（`GetLastError` = "操作成功完成"），但 `EnumWindows` 能找到 `Progman`（hwnd `0x1014a`，标题 `Program Manager`）。`FindWindowExW(NULL, 0, "Progman", NULL)` 同样返回 0。
- **结论**：**第一步就断在这里**。即便绕开查找、直接对 `0x1014a` 发 `0x052C`，本机也没有任何 `WorkerW` 变成可见的壁纸宿主。实测 13 个顶层 `WorkerW`：

```
hwnd=592410  pid=68708  vis=False rect=170x47 defview=False syslist=False
hwnd=854464  pid=9356   vis=False rect=170x47 defview=False syslist=False
hwnd=395868  pid=9356   vis=False rect=170x47 defview=False syslist=False
hwnd=262524  pid=11484  vis=False rect=170x47 defview=False syslist=False
hwnd=66230   pid=9356   vis=False rect=170x47 defview=False syslist=False
hwnd=131686  pid=9356   vis=False rect=170x47 defview=False syslist=False
hwnd=66144   pid=9356   vis=False rect=170x47 defview=False syslist=False
hwnd=66146   pid=9356   vis=False rect=170x47 defview=False syslist=False
hwnd=65890   pid=9356   vis=False rect=170x47 defview=False syslist=False
hwnd=65888   pid=9356   vis=False rect=0x0    defview=False syslist=False
hwnd=65882   pid=9356   vis=False rect=170x47 defview=False syslist=False
hwnd=65860   pid=9356   vis=False rect=170x47 defview=False syslist=False
hwnd=65858   pid=9356   vis=False rect=170x47 defview=False syslist=False
```

> 这些是 Shell 的内部工具窗（输入法/浮层用的 170×47 小窗），**不是壁纸宿主**，也没有 `SysListView32` 子窗（即不是桌面图标列表）。

> ⚠️ 顺带发现：`GetDesktopWorkerW()` 第一句用的正是 `FindWindowW`。在本机上它拿不到 Progman，程序因此落到"任意 WorkerW"兜底分支（探针当时抓到的那个是 136×38 的不可见小窗，本轮实测其余 WorkerW 为 170×47）。这正是历史上"偶尔位置飘 / 被盖住"的潜在根因（见 §9 待办 A）。

### 步骤 2：`EnumWindows` 找挂 `SHELLDLL_DefView` 的 `WorkerW`

- **本机实测**：全窗口树只有 **1 个** `SHELLDLL_DefView`（hwnd 789438），父链为：

```
SHELLDLL_DefView(789438) ← CtrlNotifySink ← DirectUIHWND ← DUIViewWndClassName
                        ← ShellTabWindowClass ← CabinetWClass(327814)   ← (root)
```

  这是**文件资源管理器窗口里的文件列表视图**，不是桌面。
- **结论**：不存在"挂 DefView 的 WorkerW"，经典方案的目标窗口在本机缺席。

### 步骤 3：`SetParent` 嵌入

- **本机实测**（独立探针 `build\embedprobe.cpp`，创建一个洋红色测试窗后 `SetParent`）：

```
baseline magenta=0
Progman=0x000000000001014a  WorkerW[1]=0x00000000000d09c0
SetParent(Progman) -> ret=000000000001000c err=0  GetParent=0000000000000000  IsWindowVisible=1
  cloaked=0
  progman exstyle=0x200080 noredirection=1  visible_magenta=0
SetParent(WorkerW): visible_magenta=0
WorkerW rect = (0,0)-(136,38)
```

- **解读**：
  - `SetParent` 调用**不报错**（返回值 `0x1000c` 是原父窗口 = 桌面窗口）。
  - `GetParent()` 返回 0 **不代表失败**——对于没有 `WS_CHILD` 的窗口，`GetParent()` 返回的是 *owner*；要查真实父窗口得用 `GetAncestor(GA_PARENT)`。
  - `Progman` 的 `WS_EX_NOREDIRECTIONBITMAP = 1`（`0x00200000`）：它**没有 GDI 重定向表面**，靠 DirectComposition 直接合成。子窗口的 `GDI / UpdateLayeredWindow` 输出没有落点。
  - ⚠️ **像素级可见性未完成验证**：执行探针时用户正在播放全屏视频（`Chrome_WidgetWin_1`，(-9,-9)-(2569,1389) 覆盖全屏），屏幕取像拿不到桌面层，`visible_magenta=0` 无法区分"被视频盖住"还是"根本没画出来"。这一条保留为**未闭环证据**，但结论不依赖它（结构层面已无宿主）。

### 步骤 4：Windows 11 的 raised desktop（`WS_EX_NOREDIRECTIONBITMAP`）兼容

- **本机就是这种形态**：`Progman` 扩展样式 `0x200080`，其中 `0x00200000` 即 `WS_EX_NOREDIRECTIONBITMAP`。
- **技术约束**：`IDCompositionTarget` / `IDCompositionVisual` 只能绑定**本进程创建的 HWND**。想把自己的视觉树送进 `Progman` 的合成树，只能靠把代码注入 `explorer.exe` 进程——这属于明确排除的危险操作。
- **替代**：DirectComposition 自绘到**自家** HWND 上，再靠 z 序贴到 Progman 之上——这其实**就是现在的方案**（只是绘制用的是 GDI+ + ULW 而不是 DComp）。

### 步骤 5：Explorer 崩溃重启后自动重挂

- **技术可行性**：完全可做。标准做法是注册 `TaskbarCreated` 消息 + 定时校验宿主 HWND 存活 + 防抖（约 200~500ms）重挂。
- **但要先有"可挂的宿主"**。本机没有，所以这套逻辑即使写了也是死代码。
- **而且现状反而更稳**：组件是独立顶层窗口，Explorer 重启最多短暂遮住它；重新收到 `TaskbarCreated` 后调一次 `SitOnDesktop()` 就回到原位，不存在"父窗口没了 → 子窗口被牵连"的风险。

---

## 4. 为什么新 shell 上"图标之下"这个位置不存在

桌面（壁纸 + 图标）在本机的渲染归属：

| 证据 | 内容 |
|---|---|
| `Progman` 尺寸 | (0,0)-(2560,1440)，全屏、可见 |
| `Progman` 子窗口数 | **0** |
| `Progman` 扩展样式 | `WS_EX_NOREDIRECTIONBITMAP`（无 GDI 表面，纯 DComp 合成） |
| 顶层窗口里除 `Progman` 外 | 没有第二个全屏、可见、layered 的"图标层"窗口 |
| 相邻全屏窗口 | 只有 `Windows.UI.Core.CoreWindow`（pid 17608 = `TextInputHost`，输入法），与桌面无关 |
| 相关 XAML 设施 | `XamlExplorerHostIslandWindow_WASDK`、`Windows.UI.Core.CoreWindow`（`DesktopWindowXamlSource`）均存在于 explorer 进程 |

**推论**：壁纸与桌面图标都在 `Progman` 内部的两层 DComp 视觉里被合成。窗口层级只到 `Progman` 这一层为止，**它内部没有子窗口边界可供插入**。

因此：
- "壁纸之上" = 窗口 z 序位于 `Progman` 之上 → **可达（现已达成）**
- "图标之下" = 需要插进 `Progman` 内部两层 DComp 视觉之间 → **跨进程不可达**

> 对照：在老 shell（Win10、早期 Win11）里，图标确实是独立的 `SHELLDLL_DefView`（在 `Progman` 或其兄弟 `WorkerW` 里），壁纸在另一个 `WorkerW` 里，两者是**两个真实窗口**——所以那套 SetParent/夹层的做法才成立。这也是该方案"教程满天飞、换台机器就失效"的根本原因：**它依赖的是窗口结构，不是 API**。

---

## 5. 版本适配矩阵（供以后换机/换系统参考）

| 系统 | 桌面结构 | 0x052C / WorkerW 方案 | 备注 |
|---|---|---|---|
| Win10 全系 | `Progman` + 兄弟 `WorkerW`（含 `SHELLDLL_DefView`） | ✅ 可用 | 经典形态 |
| Win11 21H2 / 22H2 / 23H2 | 同上，但可能需要 0x052C 触发 | ✅ 多数可用 | `Progman` 常带 `NOREDIRECTIONBITMAP`，子窗口 GDI 可能不显示 |
| Win11 24H2 (26100) 及以后新 shell | 图标与壁纸都在 `Progman` 的 DComp 表面内，`WorkerW` 退化为不可见小工具窗 | ❌ **不可用**（本机实测） | 本机 build **26300**.9974 即属此类 |
| 未来 | 只会更 DComp 化 | ❌ | 不该把产品架在这套 API 上 |

> 结论：**WorkerW 嵌入是一条"越往后越不可用"的路**。把它做进产品，等于给自己埋一个随系统更新失效的定时炸弹。

---

## 6. 如果真挂上了，会失去什么（影响清单）

即便换到一台"能挂"的机器，把 GlassDock 改成 `SetParent(WorkerW)` 也有明确代价：

| 维度 | 现状 | 嵌入后 | 影响 |
|---|---|---|---|
| **真透明** | `ULW_ALPHA` 逐像素 alpha，壁纸真实透过 | 父窗口若 `NOREDIRECTIONBITMAP` → 无 GDI 表面，ULW 不落地 | 🔴 **核心卖点可能直接失效**，要重写渲染层 |
| **层级** | 图标之上（盖住图标） | 图标之下（被图标压住） | 🟡 这是需求本身，但会被图标遮挡 |
| **输入** | `WS_EX_NOACTIVATE` + 恒 `HTCLIENT`，点击必达 | 输入先经父窗口消息链，拖动/滚轮/右键菜单更易出怪 | 🟠 全部交互语义要重验 |
| **右键菜单** | 临时放开 `WS_EX_NOACTIVATE` 以支持"点空白关闭" | 同上，父子窗口下弹出位置/关闭行为要重算 | 🟠 |
| **设置窗口** | 独立顶层窗口，模态与置顶关系简单 | 主窗成子窗口后，父子/置顶/DPI 关系要重算 | 🟠 |
| **崩溃恢复** | 独立窗口，`TaskbarCreated` 后 `SitOnDesktop()` 一次即回 | 父窗口销毁 → 子窗口失联，需要重挂 + 防抖 | 🟠 |
| **性能** | 默认 60Hz 重绘（可调 6 档） | 进 Explorer 合成树，每帧走它的通道 | 🟠 高刷/多显示器更吃帧 |
| **稳定性** | 与 Explorer 解耦 | 与 Explorer 生命周期强绑定 | 🔴 系统更新即失效（见 §5） |
| **Win+D** | 已随桌面显示/隐藏 | 与图标一致 | 🟢 无差别 |
| **托盘/单实例/自启** | 独立 | 不受影响 | 🟢 无差别 |

**净评价**：为了一个"被图标压住"的观感，付出"真透明可能失效 + 交互重验 + 系统更新即坏"的代价 —— **不划算**。

---

## 7. 平替方案（按性价比排序）

### 7.0 方案总览

| 方案 | 目标 | 本机可行性 | 成本 | 风险 | 是否影响现有功能 |
|---|---|---|---|---|---|
| **A 隐藏桌面图标** | 得到"纯壁纸"观感 | ✅ 立即可用 | 0 | 0 | 不影响（只是系统设置） |
| **B-1 文字颜色自适应** | 浅色壁纸不再糊字 | ✅ 可做 | 小（约 120 行） | 低 | 不影响，纯颜色采样 |
| **B-2 贴边吸附** | 视觉上"贴"在桌面 | ✅ 可做 | 小（约 30 行） | 低 | 轻微改变拖动手感 |
| **B-3 描边/不透明度** | 削弱"浮起来"感 | ✅ 可做 | 极小 | 低 | 只改绘制参数 |
| **C 旧 shell 夹层嵌入** | 图标之下 | ❌ 本机无宿主 | 中（约 200 行 + 维护） | 高 | 会牺牲 ULW 真透明 |
| **D DComp 注入 / 注入 explorer** | 进入 Progman 合成树 | ❌ 技术上不可行 / 危险 | 极高 | 极高 | 不建议 |

> 结论：**A 是零成本的最优解；B-1 是唯一值得写代码的一项；C/D 不做。**

### 方案 A ★★★★★ 隐藏桌面图标（零代码、零风险、10 秒）

**做法**：桌面右键 → 查看 → 取消勾选「显示桌面图标」。

**效果**：桌面只剩壁纸，组件自然"躺在壁纸上"，观感与 Lively 的壁纸层完全一致。需要图标时再勾回来。

- 成本：0
- 风险：0
- 适用：用户真正想要的是"壁纸感"而不是"被图标压住"
- 说明：这是唯一能做到"零成本、效果≈真嵌入"的路径

### 方案 B ★★★★☆ 视觉贴近壁纸（不改层级，全部在现有代码内）

三项独立小改动，都不触碰 `SitOnDesktop()` 与 ULW 链路：

#### B-1 文字颜色随壁纸明暗自适应（原计划 P3）

**现状缺陷**：`g_desktopLum` 恒为 0.5，文字永远是浅色 `(247,247,250)`。换到浅色壁纸就糊成一片。

**设计（T1：读壁纸文件采样）**：

```
WallpaperLumUnder(RECT) -> float
  1) 读 HKCU\Control Panel\Desktop\WallPaper（壁纸文件路径）
  2) 读 WallpaperStyle / TileWallpaper 决定映射：fill / fit / stretch / center / tile
  3) 只解码一次缩略图（宽度 ≤1024），按上述映射裁剪出"组件所在矩形"对应的区域
  4) 求平均相对亮度（Rec.709：0.2126R + 0.7152G + 0.0722B）
  5) 缓存 key = (path, 文件大小, mtime, 屏幕矩形)
```

- 失效时机：启动 / `WM_SETTINGCHANGE` / 移动或 resize（200ms 去抖）/ 显示器变更
- 任何一步失败 → 回退 `lum = 0.5`（等价现状，不会更差）
- 动态壁纸（Wallpaper Engine 等）读不到静态文件 → 同样回退 0.5
- 收益：浅色壁纸下文字从"糊"变成"可读"，这是真实缺陷修复，不是花活
- 风险：低（纯采样 + 颜色选择，无渲染结构改动）

#### B-2 贴边吸附

松手时若距屏幕边缘 < 24px，自动吸附；配合"避开图标网格区"可让组件永远落在桌面空白处，看起来像壁纸的一部分。

- 成本：约 30 行（`WM_EXITSIZEMOVE` 里做）
- 风险：低（可能被用户视为"多管闲事"，建议做成可关闭）

#### B-3 不透明度与描边

已有滑块（默认 36%）。可增加"极细描边 / 无描边"选项，降低"浮起来"的观感。

### 方案 C ★☆☆☆☆ 旧 shell 夹层（不建议）

在 Win10/旧 Win11 上把窗口插到 `SHELLDLL_DefView` 之下、`WorkerW` 之上。

- 本机：无宿主窗口，**代码即死分支**
- 代价：牺牲 ULW 真透明（见 §6）
- 结论：**不为它写代码**

### 方案 D ☆☆☆☆☆ 已排除的两条

- **DirectComposition 自绘进 Progman**：`IDCompositionTarget` 只能绑本进程 HWND，跨进程不可行
- **注入 explorer**：危险操作，明确不做

---

### 方案 E 参考实现（旧 shell 完整骨架，**本机不可用**）

> 说明：以下是"标准 WorkerW 嵌入"的完整可运行骨架，供将来在 Win10 / 旧 Win11 上参考。**在本机（build 26300 新 shell）执行必然失败**：`FindWindowW("Progman")` 返回 0、13 个 `WorkerW` 均无 `DefView`、`Progman` 无重定向表面。**不要直接合进 GlassDock**——合进去就是死代码，且会牺牲 ULW 真透明（见 §6）。

```cpp
// ============ 仅限 Win10 / 旧 Win11 的参考骨架 ============
static HWND     g_host       = NULL;   // 当前宿主
static HWND     g_origParent = NULL;   // 原父窗口（回滚用）
static LONG_PTR g_origStyle  = 0;
static LONG_PTR g_origExStyle= 0;

// ① 触发 Shell 生成承载壁纸的 WorkerW（未公开消息，失败无害）
static void NudgeShellSpawnWorkerW(HWND progman){
    DWORD_PTR r = 0;
    SendMessageTimeoutW(progman, 0x052C, 0,    0, SMTO_NORMAL, 1000, &r);
    SendMessageTimeoutW(progman, 0x052C, 0x0D, 0, SMTO_NORMAL, 1000, &r); // 部分版本需要
}

// ② 找壁纸宿主：老 shell 里「挂 DefView 的 WorkerW」之上还有一个承载壁纸的 WorkerW
static HWND FindWallpaperHost(){
    struct S { HWND progman; HWND defviewWorker; HWND anyWorker; } s{0,0,0};
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        S* s = (S*)lp;
        WCHAR cls[64]; GetClassNameW(h, cls, 64);
        if (!wcscmp(cls, L"Progman")) s->progman = h;
        else if (!wcscmp(cls, L"WorkerW")){
            if (!s->anyWorker) s->anyWorker = h;
            if (FindWindowExW(h, NULL, L"SHELLDLL_DefView", NULL)) s->defviewWorker = h;
        }
        return TRUE;
    }, (LPARAM)&s);

    if (s.defviewWorker){                    // 取另一个 WorkerW 当作壁纸层
        HWND w = NULL;
        while ((w = FindWindowExW(NULL, w, L"WorkerW", NULL)) != NULL)
            if (w != s.defviewWorker) return w;
    }
    return s.progman;
}

// ③ 挂载：改成 WS_CHILD 再 SetParent，并记录回滚信息
static bool EmbedIntoDesktop(HWND wnd){
    HWND host = FindWallpaperHost();
    if (!host) return false;

    g_origParent  = GetAncestor(wnd, GA_PARENT);      // 注意：不是 GetParent()
    g_origStyle   = GetWindowLongPtrW(wnd, GWL_STYLE);
    g_origExStyle = GetWindowLongPtrW(wnd, GWL_EXSTYLE);

    SetWindowLongPtrW(wnd, GWL_STYLE, (g_origStyle & ~WS_POPUP) | WS_CHILD);
    SetWindowPos(wnd, NULL, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);
    if (!SetParent(wnd, host)){
        SetWindowLongPtrW(wnd, GWL_STYLE, g_origStyle);   // 失败就还原
        return false;
    }
    g_host = host;
    return true;
}

// ④ 回滚（设置窗口/调试/退出时用）
static void DetachFromDesktop(HWND wnd){
    if (g_origParent) SetParent(wnd, g_origParent);
    SetWindowLongPtrW(wnd, GWL_STYLE, g_origStyle);
    SetWindowLongPtrW(wnd, GWL_EXSTYLE, g_origExStyle);
    SetWindowPos(wnd, NULL, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);
    g_host = NULL;
}

// ⑤ Explorer 重启后重挂
// WndProc 里：static UINT s_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
//             case s_taskbarCreated: ScheduleRehook(); break;   // 200~500ms 去抖后再执行
static void RehookDesktop(HWND wnd){
    // IsWindow 为假 只说明句柄失效；句柄可能被复用，所以补一次类名校验
    if (IsWindow(g_host)){
        WCHAR cls[64] = {0};
        GetClassNameW(g_host, cls, 64);
        if (!wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Progman")) return;
    }
    EmbedIntoDesktop(wnd);
}
```

**这段骨架落地时必须知道的 6 件事**

1. `GetParent()` 对非 `WS_CHILD` 窗口返回的是 owner，**回滚要用 `GetAncestor(GA_PARENT)`**。
2. 改成 `WS_CHILD` 后再 `SetParent` 的顺序不能反，否则会被强制重设样式。
3. `WS_EX_LAYERED` 的子窗口从 Win8 起才被支持；但在 `NOREDIRECTIONBITMAP` 宿主下 **ULW 没有落点**，等于白画。
4. `TaskbarCreated` 在一次 Explorer 重启里**可能连发两次**，必须去抖 + 幂等（`g_host` 相同则直接返回）。
5. 多显示器/混合 DPI 下，宿主矩形与子窗口坐标系要重算，`WM_DPICHANGED` 必须处理。
6. 挂进去以后，右键菜单、拖动、滚轮事件都会先过 Explorer 的消息链，**所有交互都要重测**（见 §6）。

---

## 8. 建议路线

1. **先让用户用方案 A 试 10 秒**——如果"隐藏图标"后的观感就是目标，工程量为 0，到此结束。
2. 若用户不愿隐藏图标、只是嫌"浮着不像壁纸" → **做方案 B-1（文字自适应）**，这是当前唯一真实缺陷；B-2/B-3 作为可选项。
3. **不做 WorkerW 嵌入**（方案 C/D）。
4. 顺带修一个真问题：`GetDesktopWorkerW()` 用 `EnumWindows` 取代 `FindWindowW`（见 §9）。

---

## 9. 待办与后续

| 编号 | 事项 | 类型 | 风险 | 说明 |
|---|---|---|---|---|
| A | `GetDesktopWorkerW()` 改用 `EnumWindows` 比对类名 | 稳定性修复 | 低 | 本机 `FindWindowW("Progman")` 返回 0，现走"任意 WorkerW"兜底，锚点可能落在 136×38 / 170×47 的不可见小窗上，可能导致偶发 z 序异常 |
| B | 方案 B-1 文字颜色自适应 | 功能 | 低 | 见 §7 |
| C | 复测探针的像素可见性 | 验证 | 低 | 需在全屏视频关闭时执行 §10.2，补齐"挂上去到底画不画"的最后一块证据 |
| D | `docs/screenshot.png` / `docs/settings.png` 重拍 | 文档 | 低 | 仍为旧版（含已删除的模糊滑块） |

---

## 10. 附录：复现与验证

### 10.1 环境

```
系统     : Windows 10 Pro for Workstations（实为 Win11 内核）build 26300.9974，DisplayVersion 26H2
explorer : 10.0.26100.9818
屏幕     : 2560×1440，缩放 125%（g_scale = 1.25）
工具链   : D:\Doubao\toolchain\mingw\mingw64\bin\g++.exe（16.2.0）
```

### 10.2 探针程序（`build\embedprobe.cpp`）

做什么：创建一个洋红测试窗 → 用 `BitBlt(GetDC(NULL))` 数屏幕上的洋红像素 → `SetParent` 到 `Progman` / `WorkerW` → 再数一次。

编译：

```powershell
& 'D:\Doubao\toolchain\mingw\mingw64\bin\g++.exe' -municode -static -O2 `
  'D:\Doubao\GlassDock\build\embedprobe.cpp' `
  -o 'D:\Doubao\GlassDock\build\embedprobe.exe' -ldwmapi -lgdi32 -luser32
```

运行（**注意：需在桌面未被全屏窗口覆盖时执行**）：

```powershell
& 'D:\Doubao\GlassDock\build\embedprobe.exe'
```

本机输出（2026-11-24，全屏视频在播，像素列无效）：

```
baseline magenta=0
Progman=0x000000000001014a  WorkerW[1]=0x00000000000d09c0
SetParent(Progman) -> ret=000000000001000c err=0  GetParent=0000000000000000  IsWindowVisible=1
  cloaked=0
  progman exstyle=0x200080 noredirection=1  visible_magenta=0
SetParent(WorkerW): visible_magenta=0
WorkerW rect = (0,0)-(136,38)
```

### 10.3 结构探测（只读 PowerShell）

```powershell
# 1) Progman 的扩展样式 / 子窗口
$pm = ...   # EnumWindows 里类名为 Progman 的那个
# exstyle 0x200080 → WS_EX_NOREDIRECTIONBITMAP(0x00200000) + WS_EX_TOOLWINDOW(0x80)

# 2) 全部顶层 WorkerW
#    本机 13 个，全部 visible=False，尺寸 170×47（一个 0×0），均无 SHELLDLL_DefView / SysListView32 子窗

# 3) 全树搜 SHELLDLL_DefView
#    仅 1 个：hwnd 789438，父链 CabinetWClass（资源管理器窗口）

# 4) z 序（EnumWindows 顺序即 z 序，序号越小越靠上）
#    199 GlassDockWidget  →  200 Progman
```

### 10.4 术语

| 术语 | 含义 |
|---|---|
| `Progman` | Shell 的桌面窗口类名（标题 `Program Manager`） |
| `WorkerW` | Shell 的辅助工作窗口类名；老 shell 中由它承载壁纸 |
| `SHELLDLL_DefView` | 桌面/文件夹视图的宿主窗口类名 |
| `0x052C` | 发给 `Progman` 的未公开消息，触发 Shell 生成承载壁纸的 `WorkerW` |
| `WS_EX_NOREDIRECTIONBITMAP` | 无重定向位图的扩展样式：窗口内容由 DirectComposition 直接合成，GDI 绘制无落点 |
| ULW | `UpdateLayeredWindow`，逐像素 alpha 的分层窗口提交方式 |

### 10.5 常见误区

1. **"`SetParent` 返回 0 就是失败"** —— 错。它返回的是**原父窗口**；`GetParent()` 对非 `WS_CHILD` 窗口返回 owner，也不代表父窗口。要查真实父窗口用 `GetAncestor(GA_PARENT)`。
2. **"教程里能跑，我这里也应该能跑"** —— 这套方案依赖窗口结构而非 API，结构随系统版本变化（见 §5）。
3. **"挂上去就万事大吉"** —— 老 shell 上 `Progman` 通常已经带 `NOREDIRECTIONBITMAP`，子窗口的 GDI 绘制很可能不显示；这也是 Lively / Wallpaper Engine 在不同版本上反复出问题的根源。
4. **"把窗口扔到底部就行"** —— `HWND_BOTTOM` 会被 DWM 停到壁纸之下，表现为"时有时无"（GlassDock 的代码注释里已记录这一坑）。
5. **"用 DComp 就能钻进去"** —— `IDCompositionTarget` 只接受**本进程**创建的 HWND；跨进程只能注入。

---

## 11. 变更记录

| 日期 | 版本 | 内容 |
|---|---|---|
| 2026-11-24 | v1.0 | 首次评估：WorkerW 方案在本机不成立（无宿主）、五步逐条实测、平替方案 A/B 设计、待办清单 |
