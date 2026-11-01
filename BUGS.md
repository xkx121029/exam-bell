# ExamBell 项目 Bug 清单

- 审查对象：`src-native/exambell.cpp`（4127 行，Win32 主程序）、`build.ps1`、`README.md`、根目录遗留 .NET 工程
- 审查方式：逐行静态审查（未运行程序），按「是否影响打铃正确性」排序
- 级别定义：**P0** 会直接导致错铃/漏铃或监测卡死；**P1** 功能失效或数据被悄悄破坏；**P2** 显示/文档/边界错误；**P3** 健壮性与工程卫生
- 审查日期：2026-11-01

## 汇总

| 编号 | 级别 | 问题 | 位置 |
| --- | --- | --- | --- |
| 1 | P0 | 监测中追加场次导致播放/铃声队列索引错位，错铃且监测永不结束 | `AddExam` L531-560；`AddExamFromInput` L3124-3145 |
| 2 | P1 | 「启动中…」阶段按急停无效，校时结束后照样开始监测 | `PerformEmergencyStop` L3021-3026 |
| 3 | P1 | 试听会截断正在响的考试铃声，并把它记成「已完成」 | `PreviewAudio` L3252-3261 |
| 4 | P1 | 配置里非法日期（如 2-31）会产生未初始化的垃圾时间 | `ParseDateTimeText` L574-597，`SystemTimeToFt` L255-263 |
| 5 | P2 | 「本轮已完成」状态永远不会显示，完成后立刻跳回「待启动」 | `OnTick` L2902-2903，`CheckAllFinished` L2790-2792 |
| 6 | P2 | 运行日志（只读 EDIT）的浅灰底分支永远不生效 | `WM_CTLCOLOREDIT` L3728-3740 vs `WM_CTLCOLORSTATIC` L3709 |
| 7 | P2 | 选音频日志写死「考前 10 分钟 / 考前 5 分钟」，与可调间隔不符 | `ChooseAudio` L3232-3233 |
| 8 | P2 | 时间选择器小时增减 23↔0 不跨日 | `AdjustPick` L1485-1489 |
| 9 | P2 | 首次运行自动创建的默认场次不落盘 | `LoadSettings` L713-717 |
| 10 | P2 | 校时线程创建失败时 `g_syncBusy` 永久为真，启动按钮卡在「启动中…」 | `StartSyncThread` L1177-1190 |
| 11 | P3 | 窗口已销毁时校时结果对象泄漏 | `SyncThreadProc` L1160-1175 |
| 12 | P3 | `nodeState[g_playKind]` 未防御 `g_playKind == -1` | `OnTick` L2822-2823 |
| 13 | P3 | 未处理 `WM_QUERYENDSESSION`，关机不落盘 | `WndProc` L3953 附近 |
| 14 | P3 | 清空日程后顶部仍是「已急停」 | `UpdateCountdown` L2593-2602，`ClearExams` L3200 |
| 15 | P3 | 同一状态两套文案（节点「已错过」/ 列表「已过期」） | L2392 与 L2443 |
| 16 | P3 | 试听音频不在播放跟踪/看门狗内，卡死无兜底 | `PreviewAudio` L3261 |
| 17 | P3 | README 手工编译命令缺 `-lole32`，按文档编译会链接失败 | `README.md` L160-163 |
| 18 | P3 | 根目录遗留 .NET 工程仍可构建，与 README「已弃用」矛盾 | `ExamBell.csproj`、`bin/`、`obj/` |
| 19 | P3 | `AppDir()` 回退到 `C:\Temp` 但不创建目录，配置写入静默失败 | L331-342 |
| 20 | P3 | 日志时间戳用本机时区，与北京时间基准不一致 | `AppendLogFile` L368-372 |

---

## 修复状态（2026-11-01，v1.6.3）

20 项全部已修复，`dist\ExamBell.exe` 已重新编译（993 KB）。

| 编号 | 状态 | 修复方式 |
| --- | --- | --- |
| 1 | 已修复 | `AddExam` 插入后同步平移 `g_playExam` 与 `g_bellQueue` 中的场次下标 |
| 2 | 已修复 | `PerformEmergencyStop` 增加 `g_pendingStart` 分支：取消待启动、恢复按钮 |
| 3 | 已修复 | 监测期间禁用「试听 / 选择」按钮，`PreviewAudio` / `ChooseAudio` 各自加前置拦截，试听另设 10 分钟兜底停止 |
| 4 | 已修复 | `ParseDateTimeText` 按月份校验日；`SystemTimeToFt` 检查转换结果并返回 0；苏宁与 `Date` 头解析同步校验 |
| 5 | 已修复 | 新增 `g_completed`，自然结束后保留绿色「本轮已完成」，重新启动或改日程时清除 |
| 6 | 已修复 | 日志浅灰底移到 `WM_CTLCOLORSTATIC`（只读 EDIT 实际收到的是该消息），简化 `WM_CTLCOLOREDIT` |
| 7 | 已修复 | 「已设置音频」日志改用 `NodeShortName(index)` |
| 8 | 已修复 | `AdjustPick` 改为分钟→小时→日→月的统一进位/借位，`PgUp/PgDn` 跨 5 也正确进位 |
| 9 | 已修复 | 首次运行创建默认场次后立即 `SaveSettings()` |
| 10 | 已修复 | `CreateThread` 失败时回滚 `g_syncBusy` 与按钮状态，待启动则直接按本机时间启动 |
| 11 | 已修复 | 新增 `PostSyncResult`，`PostMessage` 失败时由线程自行释放 |
| 12 | 已修复 | 新增 `BellNodeStillPlaying()`，统一校验 `g_playExam` / `g_playKind` 与节点状态 |
| 13 | 已修复 | 新增 `WM_QUERYENDSESSION`：保存配置与日志 |
| 14 | 已修复 | `ClearExams` 复位 `g_stoppedByUser` 并刷新顶部胶囊 |
| 15 | 已修复 | `ExamStatusText` 的 `ST_MISS` 统一为「已错过」 |
| 16 | 已修复 | 试听记录 `g_previewTick`，空闲轮询中 10 分钟兜底停止 |
| 17 | 已修复 | README 编译命令补 `-lole32`，订正 Media Foundation 为动态加载 |
| 18 | 已修复 | 遗留 .NET 工程移入 `src/`（`ApplicationIcon` 指向 `../app.ico`，`AssemblyName` 改为 `ExamBell-Legacy`），删除 `bin/`、`obj/` |
| 19 | 已修复 | 回退目录改用 `GetTempPathW`，并去掉尾部斜杠后创建 `ExamBell` 子目录 |
| 20 | 已修复 | 日志时间戳改用 `FormatDateTime(FtNow())`，与打铃同一北京时间基准 |

---

## P0 / P1 详细说明

### 1. P0 · 监测进行中追加场次 → 索引错位（错铃 + 监测卡死）

**代码**：`AddExam()` 按开始时间有序插入（[exambell.cpp L531-560](src-native/exambell.cpp#L531-L560)），
监测期间追加走 [AddExamFromInput L3124-3145](src-native/exambell.cpp#L3124-L3145)。

**根因**：`g_exam` 是「值数组 + 下标」模型。`AddExam` 插入时把元素整体后移，但下面这些**只存了下标**的状态没有被平移：

- `g_playExam` / `g_playKind`（L180-181，正在播放的场次）
- `g_bellQueue` 中的 `BellItem{exam, kind}`（L2685-2687）
- 正在播放音频的收尾逻辑 `g_exam[g_playExam].nodeState[g_playKind]`（L2822-2826）

**复现**：10:00 场次开始响铃 → 播放中（`g_playExam = 0`）→ 监测中新增一场 09:00 的考试
（列表确认「时间已过去仍要添加」）→ 新场次插入到下标 0，原来的场次被挤到下标 1。之后：

1. 音频播完时，程序把 `g_exam[0]`（**新增的那场**）的节点置为 `已完成`，日志打印**错误的场次名**；
2. 真正在响的那一场节点永远停在 `播放中` → `CheckAllFinished()`（L2769-2779）永远看到 `ST_PLAY` → 监测永不结束，
   「启动监测」永久禁用、「删除/清空」永久锁死，只能急停重来；
3. 若队列里还有待播铃声，后续会播放到**错的场次**上，并按错的场次记账；原场次的待触发节点之后还会再响一次（重复打铃）。

**修复建议**：二选一，不要拖。
- 方案 A（最小改动）：插入前把 `g_playExam` 与 `g_bellQueue` 里所有 `>= pos` 的 exam 下标 +1（删除时同理 -1）；
- 方案 B（更稳）：把播放/队列改为持有场次稳定 ID（如给 `Exam` 加自增 `id`），所有跨轮询状态按 id 查表，不再用下标。

### 2. P1 · 「启动中…」时急停无效

**代码**：启动监测先置 `g_pendingStart = true` 并禁用按钮（L3831-3835），等 `WM_SYNC_DONE` 再 `StartMonitoring()`（L3926-3930）；
而急停入口在 `!g_running` 时直接 return（[L3021-3026](src-native/exambell.cpp#L3021-L3026)），`g_pendingStart` 没有被清掉。

**复现**：点「启动监测」→ 按钮显示「启动中…」（正在校时）→ 立刻点红色急停（或按 Esc）→
日志写「急停：当前没有正在进行的监测任务」→ 校时一完成，**监测照样开始，到点照样打铃**。

**影响**：这是考场场景里最危险的失效模式——操作员以为已经中止，实际仍在运行。

**修复建议**：`PerformEmergencyStop()` 里先判断 `g_pendingStart`：置 false、恢复按钮文案与可用状态、写「已取消启动」日志；Esc 也应能取消待启动。

### 3. P1 · 试听会截断真实铃声

**代码**：[PreviewAudio L3252-3261](src-native/exambell.cpp#L3252-L3261)：先 `AudioPlay()`（内部 `AudioStopAll()`），
若当时有铃声在播，则提前把该节点置 `ST_DONE` 并 `g_playExam = -1`。且「试听」按钮在监测期间并未禁用（L2311）。

**影响**：监考老师随手点一下试听，正在响的考试铃声被当场掐断，而界面与日志都显示「已完成/播放结束」——
事后追查会认为铃声正常播完。

**修复建议**：监测期间禁用「试听/选择」按钮；或在 `PreviewAudio` 内检测到 `g_playExam >= 0` 时提示「当前正在打铃，稍后再试听」。

### 4. P1 · 非法日期 → 未初始化时间

**代码**：[ParseDateTimeText L574-597](src-native/exambell.cpp#L574-L597) 只校验 `d <= 31`，
`SystemTimeToFt`（L255-263）忽略 `SystemTimeToFileTime` 的返回值。

**复现**：手工把 `config.ini` 改成 `exam0_time=2026-02-31 09:00` → 启动后该场次时间为未初始化的随机 `FILETIME`，
列表可能显示 `0000-00-00 00:00`，排序、节点排期、倒计时全部失真（极端值下 `now - nodeFt` 还会溢出）。

**修复建议**：`ParseDateTimeText` 用 `DaysInMonth` 校验日，并在 `SystemTimeToFt` 里把 `SystemTimeToFileTime` 的失败当错误返回（`ParseDateTimeText` 返回 false 即可跳过该场）。

---

## P2 详细说明

### 5. 「本轮已完成」显示不出来

`OnTick()` 先 `UpdateCountdown()`（此时 `g_running == true`、无待触发节点 → 文案「本轮已完成」、`g_runChip = 3`，L2565-2569），
紧接着 `CheckAllFinished()` 立刻把 `g_running = false` 并再调一次 `UpdateCountdown()`（L2792），
文案被覆盖为「待启动」、`g_runChip = 0`。

结果：README 里写的「完成转绿」在真实运行中一帧都看不到，用户只能靠日志判断本轮结束。
建议：给「已完成」单独留一个状态（例如 `g_runChip = 3` 保留到下次启动或急停），或在 `CheckAllFinished` 里显式设置完成文案。

### 6. 运行日志底色不生效

`hLog` 是用 `ES_READONLY | ES_MULTILINE` 创建的 EDIT（L2317-2321）。**只读 EDIT 的着色消息是 `WM_CTLCOLORSTATIC`，
不是 `WM_CTLCOLOREDIT`**。因此 L3728-3740 里为 hLog 准备的浅灰底（`bLog`）永远走不到，日志区实际是纯白底 + `C_INK` 正文，
与旁边卡片和正文区没有层次区分（`bLog` 这个画刷创建后从未被使用）。

### 7. 选音频日志档位写死

`ChooseAudio()` 里 `static const wchar_t* caps[3] = { "考前 10 分钟", "考前 5 分钟", "考试开始" }`（L3232-3233）。
间隔改成 3/4 分钟后，节点名已经是「考前 7 分钟 / 考前 4 分钟」，但日志仍写「已设置音频（考前 10 分钟）」，
事后查日志会得到错误的节点归属。改用 `NodeShortName(index)` 即可。

### 8. 时间选择器小时不跨日

`AdjustPick()` 的 `case 3`（小时）在 `23 + 1` 时直接回绕到 `0`，日期不变（L1485-1489）；而分钟进位会跨小时。
结果是用滚轮把 23 点调到 0 点，场次从「今天 23:00」变成「今天 00:00」（过去 23 小时），
与用户直觉相反，也更容易误触发「时间已过去」提示或直接记成已错过。建议小时越界时同步 ±1 天（复用分钟那套进位逻辑）。

### 9. 首次运行的默认场次不落盘

`LoadSettings()` 在「无配置文件且无场次」时自动 `AddExam(L"第 1 场", FtFromNowMinutes(30))`（L713-717），
但函数结束前不调用 `SaveSettings()`。用户看到界面上有一场「第 1 场」，如果此时进程被强杀（任务管理器/断电），
下次启动又会重新造一场新的默认场次；而正常退出会保存，行为不一致。建议创建后立即 `SaveSettings()`。

### 10. 校时线程创建失败 → 永久「启动中…」

`StartSyncThread()` 先置 `g_syncBusy = true`、禁用「重新校时」并写「正在校时…」（L1181-1185），
`CreateThread` 失败时（L1187-1189）没有回滚这两个状态：`g_syncBusy` 永远为真、没有任何 `WM_SYNC_DONE` 回来。
若这次失败发生在点「启动监测」时，按钮会永久卡在「启动中…」，程序只能重启。建议失败分支里复位 `g_syncBusy`、
恢复按钮并提示「无法启动校时线程」。

---

## P3 详细说明

### 11. 校时结果对象泄漏
`SyncThreadProc` 用 `PostMessageW(g_hwnd, WM_SYNC_DONE, 0, (LPARAM)result)` 把 `new SyncResult` 交给 UI 线程；
窗口已销毁时 PostMessage 失败，该对象无人释放（进程随即退出，影响很小，但属于明确的泄漏路径）。

### 12. `g_playKind` 未防御
`OnTick()` 中三处 `g_exam[g_playExam].nodeState[g_playKind]` 只校验了 `g_playExam` 的范围（L2822、L2836、L2850），
没有校验 `g_playKind >= 0`。当前代码里两者总是一起置位/复位，但一旦将来有人单独改 `g_playExam`（例如修复第 1 条时），
就会出现 `nodeState[-1]` 越界读。建议抽一个 `bool BellStillPlaying(exam, kind)` 统一判定。

### 13. 关机不落盘
只处理了 `WM_CLOSE`（L3953）。收到 `WM_QUERYENDSESSION`（注销/关机/更新重启）时直接走默认流程，
配置与日志都不落盘（考试机常被统一关机，日程可能丢）。建议在 `WM_QUERYENDSESSION` 里 `SaveSettings()` 并返回 TRUE。

### 14. 清空日程后仍是「已急停」
急停后 `g_stoppedByUser = true`，顶部胶囊显示「已急停」（L2593-2602）；随后「清空日程」（L3200-3202）不会复位它。
清空后界面处于「已急停 + 共 0 场」的组合，语义混乱。建议清空时一并复位 `g_stoppedByUser`。

### 15. 状态文案两套
`ExamStatusText()` 把 `ST_MISS` 叫「已过期」（L2392），`StateText()` 叫「已错过」（L2339），节点面板与日程列表同屏出现两种说法。
统一为一个词即可。

### 16. 试听无看门狗
`PreviewAudio()` 直接 `AudioPlay()`，既不设 `g_playExam`，也不受 10 分钟看门狗与「未进入播放状态」兜底约束（L3261）；
一套卡死的 MF 播放器只能等下一次调用 `AudioStopAll()` 才会被清理。建议试听也复用打铃那套「开始播放 + 超时判定」。

### 17. README 手工编译命令不完整
README L160-163 的命令是 `... -lwinhttp -lwinmm -lcomctl32 -lgdi32 -luser32`，
但源码 `MfEnsure()` 调用了 `CoInitializeEx`（ole32），`build.ps1` 里也确实多了 `-lole32`（L27）。
照 README 手工编译会在链接阶段报 `undefined reference to CoInitializeEx`。另外 README 的依赖清单把
`MFPlay / MFPlat` 列为导入依赖，实际是 `LoadLibrary` 动态加载，并不在导入表里——核对依赖时容易误判。

### 18. 遗留 .NET 工程仍在根目录
`ExamBell.csproj`（net45）与 `src/*.cs` 仍在仓库根目录，`bin/`、`obj/` 也还在（已被 .gitignore 忽略）。
README 说「早期 .NET 实现（已弃用，仅作历史保留）」，但用户 `dotnet build` 仍会在根目录产出**同名的 ExamBell.exe**，
与 `dist\ExamBell.exe`（原生版）混淆，且两个版本的配置/行为完全不同。建议把 .NET 工程移到 `legacy/` 或删除，
并在 README 里明确唯一构建入口。

### 19. `AppDir()` 回退目录未创建
`GetEnvironmentVariableW(L"APPDATA")` 失败时把目录设为 `C:\Temp`（L333-337），随后只对 `C:\Temp\ExamBell` 调 `CreateDirectoryW`；
父目录不存在时创建失败，配置/日志写入全部静默失败（`SaveSettings` 直接 return、`AppendLogFile` 直接 return）。
建议回退到 `%TEMP%`（`GetTempPath`）并检查 `CreateDirectory` 结果。

### 20. 日志时间戳时区
`AppendLogFile()` 用 `GetLocalTime` 打时间戳（L368-372），而程序内部打铃基准是北京时间（校时偏移）。
机器时区若不是 UTC+8，日志时间线与界面时钟/打铃时刻对不上，事后追查需要自己换算。建议日志统一写北京时间（`FtNow()`）。

---

## 另外值得排期的体验问题（非 bug）

- 添加一场后输入区时间固定回到「现在 +30 分钟」，连续添加两场很容易撞上「同一开始时间已存在」的提示；
  可考虑「沿用上一场 +1 小时」或保留用户上次选择的时间。
- 顶部校时文案右对齐宽度 264（L2212），「已同步（淘宝）· 偏差 +123 ms」这类长文案会顶到「重新校时」按钮，长时延下更容易贴边。
- MP4/M4A 走 Media Foundation 时 `MFPCreateMediaPlayer` 在 UI 线程同步创建播放器，解码器初始化较慢时会造成一次明显的界面卡顿。
