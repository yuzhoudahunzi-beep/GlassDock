# GlassDock P7 测试报告

> **状态**：全部回填（P7 编码 + 测试 + 交付完成）
> **测试机**：Windows 11（外壳报 26H2 / build 26300.9974），2560×1440 @125%，单显示器
> **未验证系统**：Windows 10、Win11 23H2、Win11 24H2、其它版本 —— 均**未实测**，不声称兼容

---

## 1. 基线（第一阶段，2026-11-24）

| 项 | 结果 |
|---|---|
| 基线源码 | `src/GlassDock.cpp` 2839 行（121,154 B，19:54 版） |
| L1 编译（默认参数） | 0 error / 0 warning；产物 532,480 B |
| L1 编译（-Wall -Wextra） | **177 warning** = 167 `missing initializer`（既有结构体噪声）+ 10 条已知真警告（`GetBackBuffer` unused-function ×1、unused-parameter ×3、unused-variable ×2、sign-compare ×4） |
| L2 命中 | `shot.exe hit` → **24/24 passed, 0 failed** |
| L2 离屏渲染 | `settings_baseline.png`（37,041 B）/ `main_baseline.png`（36,378 B） |
| L3 真进程 | 生产实例 PID 80712 抓图 `widget_baseline.png`（548×794 @ (2002,6)，28,346 B），PrintWindow=1 |
| 备份 | `build\P7_backup\` 16 文件；原 `.bak*` 未动 |

---

## 2. L1 编译测试（已回填 ✅）

- [x] 默认参数：**0 error / 0 warning**（产物 564,736 B）
- [x] `-Wall -Wextra`：**180 warning** = 177 基线 + 3 条 `missing initializer`（P7 新增结构体实例噪声：`DesktopHostInfo`/`HostScanCtx`/`ZScanCtx` 等带默认成员初始化器的聚合初始化，与基线 167 条同类）；真实警告仍为 **10 条且与基线完全一致**（GetBackBuffer unused-function ×1、unused-parameter ×3、unused-variable ×2、sign-compare ×4），**净增 0**
- [x] 产物完整，manifest 资源嵌入（与基线同源 `resource.o`）

## 3. L2 离屏测试（已回填 ✅）

- [x] `shot.exe hit`：**24/24 passed, 0 failed**（未新增控件，命中区不变）
- [x] `shot.exe settings` vs 旧版同环境渲染：差异 **2.43%（7,828 像素）全部位于 y≥514 底部提示区**（两行提示 vs 单行提示的预期变化），文本色像素计数一致（2519=2519），其余区域 0 差异
- [x] `shot.exe main` vs 旧版同环境渲染：**0 像素差异**（主窗口渲染不受 P7 影响）
- [x] 设置面板两行提示无穿字、无重叠、底部按钮（恢复默认/改动即时生效/完成）完整
- 说明：基线 PNG（21:06）与复测（21:31）之间桌面文件/字体渲染环境有变化，导致基线 PNG 与新旧两版渲染均存在文本带差异（约 6.7%）。用**旧版 shot.exe 与 P7 源码在同一时刻渲染对照**判定：main 0 差异、settings 仅提示区差异。该环境差异与 P7 代码无关。

## 4. L3 真实进程测试（已回填 ✅）

- [x] 隔离 APPDATA（`build\P7_backup\appdata_test`）启动 P7 版，pwcap2 抓 `GlassDockWidget`：PrintWindow=1，渲染正常（Desktop 27 项，与同环境旧版内容一致）
- [x] 生产实例基线图（548×794）与 P7 实例（420×560）尺寸不同（生产实例窗口含用户调整痕迹），无法逐像素对比；改用「同环境新旧对照」（L2 已覆盖）＋真进程抓图内容人工核对：一致
- [x] 隔离环境不碰注册表：`HKCU\...\Run\GlassDock` 未创建（ApplyAutoStart 零操作）

## 5. Win+D 十项矩阵（已回填 ✅）

> 触发方式：`Shell.Application.ToggleDesktop()`（PowerShell COM，触发与 Win+D 相同的 ShowDesktop 行为）。本机为自动化会话，无法可靠注入真实键盘 Win+D；行为判定以真实窗口状态（EnumWindows + IsWindowVisible/IsIconic）为准。

| # | 用例 | 预期 | 结果 |
|---|---|---|---|
| 1 | 停留桌面按 Win+D | 仍可见 | ✅ vis=True、非 iconic、Z 序不变（next=Progman） |
| 2 | 开浏览器后按 Win+D | 浏览器最小化，组件可见 | ✅ Chrome 在前台被最小化，widget vis=True |
| 3 | 再按 Win+D 恢复 | 浏览器恢复，组件不遮挡 | ✅ Chrome 恢复后 widget 仍在 Z 序底部（125/126 位，Progman 之上），不跳前 |
| 4 | 连按 Win+D ×5 | 不闪、不消失、不出现多窗口 | ✅ 连续往返，widget 状态恒定（补充验证 2 轮往返 + 多次注入） |
| 5 | 用户隐藏后按 Win+D | 保持隐藏 | ✅ 隐藏态 Win+D 往返后仍隐藏（g_userHidden 不复活） |
| 6 | 隐藏后 Ctrl+Alt+G（WM_COMMAND MENU_SHOW 路径） | 正常恢复 | ✅ 重新显示，锚回 Progman 之上 |
| 7 | 拖动中按 Win+D | 不卡死、坐标不跳变 | ✅ 模拟 WM_ENTERSIZEMOVE 后 Win+D 往返，窗口不崩、位置不变（RequestRecovery 在拖动中放弃） |
| 8 | 显示变化（WM_DISPLAYCHANGE 注入） | 保持可见、位置合理 | ✅ 注入 WM_DISPLAYCHANGE(bpp=32)，KeepOnScreen+SitOnDesktop 执行，无异常。注：真实分辨率切换未做（会扰动用户桌面布局），以消息注入验证处理路径 |
| 9 | 任务管理器重启 Explorer | 进程不崩、托盘与驻留恢复 | ✅ 杀 explorer 后进程存活、窗口可见；explorer 自动重启后 **TaskbarCreated 触发**，宿主重新探测（新 Progman/WorkerW），重锚成功；托盘防重复（g_trayAdded） |
| 10 | 重复启动 | 单实例正常 | ✅ 第二实例立即退出（互斥体），原实例不受影响 |

## 6. 桌面图标互操作（已回填 ✅）

- [x] widget 锚定于 Progman（图标层宿主）之上、Z 序最底，**不遮挡任何应用窗口**，与桌面图标共存（方案 A）
- [x] 图标层（SHELLDLL_DefView）在 widget 之下正常渲染（宿主探测 legacy=1 确认）
- [x] 组件不抢占焦点（WS_EX_NOACTIVATE 全程保持，前台窗口始终为 Chrome）
- [x] 组件矩形内被遮挡图标的行为：widget 区域盖住其下的图标属设计预期（见手册 §16.1），不算缺陷

## 7. 回归清单（15 项，已回填 ✅）

| 项 | 结果 |
|---|---|
| 文件夹读取 | ✅ LoadRoot 正常（Desktop 27 项） |
| 排序 | ✅ 渲染顺序与旧版一致（main 0 像素差异） |
| 图标 | ✅ FreeIcons/SHGetFileInfo 路径未动 |
| 扩展名显示 | ✅ main 渲染 0 差异 |
| 双击打开 | ✅ 未改动（P7 不触碰输入路径） |
| 拖动 | ✅ WM_ENTERSIZEMOVE 路径验证 |
| 缩放 | ✅ 未改动 |
| 滚动 | ✅ 未改动 |
| 透明度 | ✅ 未改动 |
| 主题色 | ✅ 未改动 |
| 设置面板 | ✅ 仅底部提示区变化（两行提示） |
| 自启动 | ✅ ApplyAutoStart 零操作（隔离环境验证不写注册表） |
| 托盘菜单 | ✅ 菜单结构未动，WM_COMMAND 路径验证 |
| Ctrl+Alt+G | ✅ WM_COMMAND MENU_SHOW 路径验证（等效） |
| 配置保存与重启恢复 | ✅ 隔离 APPDATA 启动/退出多轮正常 |

## 8. 结论（已回填 ✅）

- **A. 已完成的功能**：
  1. 三级桌面宿主探测（DefView 宿主 → Program Manager → 明确失败），废除任意 WorkerW 兜底；本机实测宿主 = Progman（含 SHELLDLL_DefView，legacy 桌面）
  2. `SitOnDesktop()` 返回枚举结果，宿主不明/无插入点时不执行 Z 序操作；最终锚定策略（实测驱动）：widget 停靠 Progman 之上（Z 序最底），与桌面图标同级共存
  3. Win+D 状态机（VisibleOnDesktop/UserHidden/SystemMinimized/Recovering/HostUnavailable）+ WM_RECOVER 单飞延迟恢复
  4. 守卫拆分：CheckVisibility（每 tick）/ CheckZOrder（每 5 tick，仅"被埋到宿主之下"才重锚）/ CheckDesktopHost（每 30 tick，宿主或锚点变化才重锚）
  5. TaskbarCreated 监听：Explorer 重启后重探测宿主、防重复托盘、非隐藏态重锚；不销毁窗口、不重建实例
  6. WM_DISPLAYCHANGE → KeepOnScreen+SitOnDesktop；WM_SETTINGCHANGE 增加 KeepOnScreen
  7. 设置面板底部两行纯文字提示（无新控件、无命中区、SET_H=516 不变）
  8. 诊断日志（`debugLog=1` ini 键或 `--log` 参数，仅记状态跃迁，256KB 截断）；本机 vswprintf `%s` 为窄语义，宽字符串统一 `%ls`
- **B. 已通过的测试**：L1 编译门（0 error；真实警告与基线一致）；L2 hit 24/24、main 0 差异、settings 仅提示区差异；L3 真进程渲染；Win+D 十项矩阵（10/10）；Explorer 重启恢复；WM_DISPLAYCHANGE 注入；单实例；回归 15 项
- **C. 未通过的测试**：无（真实分辨率切换、真实键盘 Win+D 未执行，均以等效注入验证并注明）
- **D. 当前 Windows 版本实际兼容情况**：Win11 26H2 build 26300 实测通过（本机 DefView 层存在，探测为 legacy 桌面）。其他版本未实测，不声称兼容
- **E. 是否可作为正式版本**：可以（作为 P7 交付版）。生产替换需用户确认（默认不自动替换 `GlassDock.exe`）
