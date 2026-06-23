# Progress Tracker

## 1. 目的

这份文档只做一件事：

**让维护者和后续 Agent 可以快速、持续、低成本地把握项目当前进度。**

它不是设计文档，不展开讲原理；它只回答：

- 现在做到哪了
- 还差什么
- 正在卡什么
- 下一步最该做什么

配套文档：

- [WINDOWS_TERMINAL_EXPERIENCE_PARITY_SPEC.md](./WINDOWS_TERMINAL_EXPERIENCE_PARITY_SPEC.md)
- [MICROSOFT_TERMINAL_REUSE_MATRIX.md](./MICROSOFT_TERMINAL_REUSE_MATRIX.md)
- [MICROSOFT_TERMINAL_MODULE_MAPPING.md](./MICROSOFT_TERMINAL_MODULE_MAPPING.md)
- [handover_knowledge_base.md](./handover_knowledge_base.md)

---

## 2. 当前结论

### 总体状态

- **总体阶段**：Phase 1 稳定性收敛中
- **当前目标**：先把主缓冲区交互、滚动、备用屏误判、输入协议、IME 基础支持收敛到可持续状态
- **整体完成度（主观估计）**：`45% - 55%`

### 当前判断

- 项目已经从“能显示终端”推进到“部分主流交互已可用”
- 主缓冲区滚动、备用屏判定、鼠标协议、bracketed paste、IME 基础路径已经明显改善
- 但项目还没有进入“可宣称体验对标 Windows Terminal”的阶段
- 当前最大未完成项仍然是：
  - 独立终端模型
  - 主缓冲区局部重绘场景的长期稳定性
  - 真实程序级回归矩阵
  - reflow / scrollback / selection 的系统化收敛

---

## 3. 当前阶段

### 当前阶段定义

**Phase 1：停止体验继续恶化**

目标：

- 先修掉最明显、最影响体感的交互回归
- 先让 `Claude Code`、`pwsh`、`PSReadLine` 这类程序“不再明显被宿主终端干扰”

当前该阶段下已完成的重点：

- 主缓冲区滚动到历史区后，不再因为普通输入立刻抢回底部
- 用户未手动滚动时，追加输出会继续跟随到底部
- 主缓冲区 `clear/home` 重绘场景已区分“顶部可见”与“追加到底部”
- heuristic alternate screen 默认关闭，避免将 `Claude Code` 等误判成备用屏
- 鼠标模式已拆分为 tracking mode 与 encoding mode
- bracketed paste 已接入并补了回归测试
- IME 已具备 preedit/commit 基础路径和候选框锚点
- 日志已分层，默认只保留高价值视口诊断日志

当前该阶段下仍未完成的重点：

- `Claude Code` 长对话主缓冲区局部重绘场景还需要更多真实日志与程序级验证
- 真实 shell/程序矩阵尚未系统手测
- 选择模型与复制策略仍主要依赖宿主控件默认行为

---

## 4. 能力矩阵

状态定义：

- `Done`：已实现并有基础验证
- `In Progress`：已部分实现，但仍有明显风险或缺测试
- `Blocked`：当前存在明确阻塞，需先解决上游问题
- `Not Started`：尚未实质推进

| 能力项 | 优先级 | 当前状态 | 当前判断 | 下一步 |
|---|---|---|---|---|
| FollowOutput / BrowseHistory | P0 | `Done` | 已有自动化回归保护 | 继续做真实程序级验证 |
| 主缓冲区 clear/home 重绘 | P0 | `Done` | 已区分顶部可见与追加到底部 | 继续观察 `pwsh` / `Claude Code` 实测 |
| Heuristic alternate screen | P0 | `Done` | 默认已关闭，仅信显式 VT 备用屏序列 | 保持现状，除非有强证据再重开 |
| 显式 alternate screen 切换 | P0 | `Done` | 基础能力已在 | 补更多 TUI 程序实测 |
| Win32 input mode | P0 | `Done` | 基础 press/release 已通 | 扩大真实程序验证 |
| Bracketed paste | P0 | `Done` | 已接入并有测试 | 补多行粘贴真实手测 |
| 鼠标 tracking/encoding 分离 | P1 | `Done` | `1000/1002/1003/1005/1006` 基础路径已覆盖 | 补更多复杂鼠标场景 |
| IME commit/preedit/query | P1 | `In Progress` | 基础路径已通，但仍未到原生级体验 | 继续做预编辑稳定性和程序级手测 |
| 选择/复制/右键策略 | P1 | `In Progress` | 仍依赖 `QPlainTextEdit` 默认选区语义 | 设计 `SelectionModel` |
| 独立 InputTranslator | P0 | `Done` | 已引入并承接核心输入路径 | 继续扩协议覆盖 |
| 独立 ViewportModel | P0 | `Not Started` | 逻辑仍散在 `TerminalWidget` | 需要正式抽离 |
| 独立 CursorModel | P0 | `Not Started` | 状态已较清晰，但仍未分层 | 抽离光标逻辑与视觉层 |
| 独立 TerminalBuffer | P0 | `Not Started` | 仍主要依赖 `QTextDocument` | 这是后续最大工程项 |
| 独立 SelectionModel | P1 | `Not Started` | 尚未建立 | 等主缓冲区模型更稳定后推进 |
| 完整 reflow | P2 | `Not Started` | 仍未真正开始 | 需等 buffer 模型先成型 |
| Scrollback limit / trimming | P1 | `Not Started` | 仍无正式策略 | 依赖独立 buffer |
| 程序级手测矩阵 | P0 | `In Progress` | 零散验证有了，系统矩阵还没有 | 建立固定手测清单 |

---

## 5. 已完成

### 已落地的代码结构与行为改进

- 引入独立 `InputTranslator`
- 引入鼠标 tracking mode / encoding mode 拆分
- 接入 bracketed paste
- 接入 Win32 key release 路径
- 接管 IME preedit 基础绘制
- 接管 `ImCursorRectangle` 查询
- 视口日志分层
- 默认关闭高噪音 trace

### 已落地的回归测试

当前 `terminalwidget_wheel_test` 已覆盖：

- 备用屏未接管滚轮时不伪造方向键
- 备用屏接管滚轮时发送鼠标滚轮报告
- 浏览历史时 `syncCursor` 不抢回到底部
- 跟随输出时持续跟随到底部
- 主缓冲区 clear/home 重绘时保持顶部可见
- 鼠标按下/松开编码
- bracketed paste
- 组合鼠标模式
- Win32 key release
- IME preedit/commit/query
- 1005 鼠标编码
- heuristic alternate screen 默认关闭

---

## 6. 进行中

### A. `Claude Code` 类程序主缓冲区局部重绘兼容性

当前状态：

- 已确认其主要问题不在“普通滚动”本身
- 已确认它不应被归入备用屏
- 已建立专门的 `[VIEWPORT]` 日志通道用于继续定位

剩余问题：

- 对话很长时，局部重绘、光标位置、输入框可见性之间是否仍有偶发失稳，尚需真实日志驱动继续收敛

### B. IME 最后 20%

当前状态：

- 已从“基础可用”提升到“高级可用”

剩余问题：

- preedit 仍然写入当前文档承载层，不是独立 overlay
- anchor 仍与当前主缓冲区模型耦合
- 真实输入法和真实 shell/CLI 工具矩阵尚未完整手测

---

## 7. 阻塞项

### 结构性阻塞

- `TerminalWidget` 仍承担过多职责
- `QTextDocument` 仍是主终端状态的重要承载体
- 没有显式 `TerminalBuffer / ViewportModel / CursorModel`

### 验证性阻塞

- 缺少固定的真实程序手测矩阵
- 缺少更接近“主缓冲区局部重绘程序”的自动化模拟测试

---

## 8. 下一步

建议严格按下面顺序推进：

1. 继续收敛 `Claude Code` 长会话主缓冲区重绘问题  
   目标：拿真实日志，把剩余偶发问题继续缩成自动化失败测试

2. 建立程序级手测矩阵  
   最少覆盖：
   - `pwsh` + PSReadLine
   - `cmd`
   - `bash`
   - `Claude Code`
   - `vim`
   - `less`
   - `fzf`

3. 明确 `SelectionModel` 设计  
   目标：让复制/选择/右键策略逐步摆脱默认编辑器语义

4. 设计 `ViewportModel` 与 `CursorModel` 抽离  
   目标：把现在散落在 `TerminalWidget` 内的状态正式拆出来

5. 评估 `TerminalBuffer` 引入路径  
   目标：为后续 reflow、scrollback、复杂 redraw 收敛打基础

---

## 9. 最近验证结果

最近一次确认通过的验证：

- `cmake --build build --target qtterminalwidget terminalwidget_wheel_test`
- `ctest --test-dir build --output-on-failure -R terminalwidget_wheel_test`

结果：

- 当前自动化回归通过

注意：

- 自动化通过不等于程序级体验完全达标
- 所有“看起来像真实终端工具”的程序，仍需要持续进行手测和日志比对

---

## 10. 维护规则

后续每次改动后，必须同步更新这份文档中的至少一项：

- 某个能力项状态
- 当前阶段
- 阻塞项
- 下一步
- 最近验证结果

如果这份文档一周以上没有更新，就意味着项目进度已不可见。

