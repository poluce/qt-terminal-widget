# Microsoft Terminal Module Mapping

## 1. 文档目的

本文档把微软官方 `terminal` 仓库中的关键模块，与当前 `qt-terminal-widget` 仓库中的现有模块做一对一映射，并明确回答四个问题：

1. 微软那边对应的职责是什么
2. 当前项目里谁在承担这个职责
3. 当前项目是“已有实现”、“弱实现”还是“缺失”
4. 后续应该选择：
   - 直接复用
   - 选择性移植
   - 只参考
   - 自己重写

本文档与以下两份文档配套使用：

- [WINDOWS_TERMINAL_EXPERIENCE_PARITY_SPEC.md](./WINDOWS_TERMINAL_EXPERIENCE_PARITY_SPEC.md)
- [MICROSOFT_TERMINAL_REUSE_MATRIX.md](./MICROSOFT_TERMINAL_REUSE_MATRIX.md)

---

## 2. 使用方式

这份文档不是介绍性材料，而是执行材料。

后续每开始一个终端能力改造前，都应该先回答：

- 这个能力在微软体系里归哪个模块
- 当前项目里归哪个模块
- 这个职责是否混在错误的地方
- 这次改动是修补现有模块，还是应该先拆模块边界

如果一个能力在当前项目里找不到清晰承载层，就不应直接编码，应先补模块边界。

---

## 3. 当前项目的现状摘要

从当前仓库结构看，项目目前主要由以下模块构成：

- `src/terminal/terminalwidget.*`
  当前同时承担：
  - UI 控件宿主
  - 输入事件处理
  - ANSI token 消费
  - 文本写入与擦除
  - 光标同步
  - 滚动策略
  - 主/备用屏管理
  - 选择与右键行为
  - IME 提交

- `src/terminal/ansiparser.*`
  当前承担：
  - 基础 VT / ANSI 解析
  - 文本属性 token 化

- `src/pty/*`
  当前承担：
  - PTY/ConPTY 进程封装
  - I/O 缓冲与通知

当前最大的问题不是“没有模块”，而是：

- `TerminalWidget` 职责过于集中
- 真正的终端模型仍未独立出来
- 视口、光标、滚动、选择仍与编辑器控件行为耦合

---

## 4. 高层映射总表

| 微软模块/层 | 微软职责 | 当前项目对应 | 当前状态 | 后续建议 |
|---|---|---|---|---|
| `TerminalControl` | UI 控件宿主、输入桥接、滚动条、外观、与 core 协作 | `TerminalWidget` | 有对应，但职责过载 | 不复用官方控件；拆分当前 `TerminalWidget` |
| `TerminalCore` | 终端核心状态、buffer/view/cursor/input/render data 协调 | `TerminalWidget` + 少量 `AnsiParser` | 弱实现且耦合严重 | 抽出独立 core 层 |
| VT parser | VT 序列解析 | `AnsiParser` | 部分覆盖 | 优先增强或重构 |
| terminal input | 键盘/鼠标/粘贴/模式输入协议 | `TerminalWidget::keyPressEvent` 等 | 弱实现 | 抽出 `InputTranslator` |
| text buffer | 文本缓冲区、属性、scrollback、viewport 关联 | `QTextDocument` + `m_screenBufferStartRow` | 语义不稳 | 新建 `TerminalBuffer` |
| viewport/scroll model | 跟随输出、浏览历史、scroll offset | `m_screenBufferStartRow` + scrollbar 值 | 脆弱 | 新建 `ViewportModel` |
| cursor model | 光标位置、可见性、样式、相对视口位置 | `m_cursorRow/m_cursorCol/m_cursorVisible` | 有基础，无独立层 | 新建 `CursorModel` |
| selection model | 选择区、复制语义、与光标/历史的关系 | `QPlainTextEdit` 默认选区 + 少量事件覆盖 | 弱实现 | 新建 `SelectionModel` |
| renderer | 文本/选择/光标分层渲染 | `QPlainTextEdit/QTextDocument` 默认渲染 | 不可控 | 先抽离模型，再评估自绘层 |
| terminal app/settings | 应用层、设置体系、窗口管理 | 无 | 非目标 | 不接入 |
| conhost/OpenConsole infra | 控制台宿主/兼容/生态层 | `src/pty/*` 仅触及 PTY 封装 | 不对应 | 只参考，不集成 |

---

## 5. 详细映射

### 5.1 `TerminalControl` ↔ `TerminalWidget`

**微软侧职责**

- 控件宿主
- 用户输入进入点
- 与 terminal core 协作
- 控制 scrollbar、appearance、focus、selection markers
- 宿主 UI 线程与核心之间的桥接

**当前项目对应**

- [terminalwidget.h](</F:\B_My_Document\GitHub\qt-terminal-widget\src\terminal\terminalwidget.h:9>)
- [terminalwidget.cpp](</F:\B_My_Document\GitHub\qt-terminal-widget\src\terminal\terminalwidget.cpp:85>)

**当前状态判断**

- 有对应物
- 但当前 `TerminalWidget` 同时扮演：
  - 控件
  - core
  - input translator
  - viewport manager
  - cursor manager
  - selection glue

这是当前项目最典型的过载类。

**推荐动作**

- 不复用微软的 `TerminalControl`
- 保留 `TerminalWidget` 作为 Qt 宿主壳
- 逐步把内部职责下放给：
  - `TerminalBuffer`
  - `ViewportModel`
  - `CursorModel`
  - `SelectionModel`
  - `InputTranslator`

**结论**

- 当前项目的 `TerminalWidget` 不应继续扩张
- 它应该变薄，而不是继续变聪明

---

### 5.2 `TerminalCore` ↔ 当前“隐式核心”

**微软侧职责**

- 终端主状态
- 写入 PTY 输出后的解析/缓冲/渲染数据组织
- scrollback、viewport、cursor、input mode 协调

**当前项目对应**

- 没有独立 core 类
- 主要散落在：
  - `TerminalWidget::handleToken`
  - `TerminalWidget::syncCursor`
  - `TerminalWidget::resizeEvent`
  - `TerminalWidget::wheelEvent`
  - `TerminalWidget::checkHeuristicAlternateScreen`

**当前状态判断**

- 属于“隐式核心”
- 能工作，但没有清晰边界
- 难以保证能力扩展时不回归

**推荐动作**

- 不要试图直接整块嵌入官方 `TerminalCore`
- 先建立本项目自己的显式 core 分层
- 建议新增一个协调层，例如：
  - `TerminalSessionCore`
  - 或 `TerminalEngine`

该层至少负责：

- 接收 parser token
- 更新 buffer
- 更新 viewport/cursor state
- 向 render layer 提供只读视图

**结论**

- 当前项目最缺的不是“更多逻辑”，而是“显式 core”

---

### 5.3 VT parser ↔ `AnsiParser`

**微软侧职责**

- VT/ANSI 序列解析
- 状态机维护
- 为 core 提供稳定的语义输入

**当前项目对应**

- [ansiparser.h](</F:\B_My_Document\GitHub\qt-terminal-widget\src\terminal\ansiparser.h:57>)
- `AnsiParser::parse`

**当前状态判断**

- 当前已具备基础 parser
- 已支持部分 cursor/erase/alt-buffer/mouse/device-attributes
- 但仍然是轻量实现

**主要缺口**

- 协议覆盖面不足
- 与终端模型解耦程度不足
- 缺少系统化兼容性回归矩阵

**推荐动作**

- 保留 `AnsiParser` 名称和仓库局部边界可接受
- 但应按官方 parser 能力面补齐
- parser 的输出应尽量保持“只负责 token/语义”，不要夹带 UI 决策

**结论**

- 这是最适合“增强或重构”的现有模块之一

---

### 5.4 terminal input ↔ `keyPressEvent` / `wheelEvent` / `inputMethodEvent`

**微软侧职责**

- 输入协议编码
- 键盘模式
- 鼠标模式
- 粘贴模式
- IME 相关交互边界

**当前项目对应**

- `TerminalWidget::keyPressEvent`
- `TerminalWidget::wheelEvent`
- `TerminalWidget::insertFromMimeData`
- `TerminalWidget::inputMethodEvent`
- `mousePressEvent` / `mouseReleaseEvent` / `contextMenuEvent`

**当前状态判断**

- 已有统一 translator 层
- 当前核心输入路径已由 `InputTranslator` 统一承接
- 但 `TerminalWidget` 中仍残留与视口、备用屏、IME 状态相关的少量策略逻辑

**主要缺口**

- `Alt/Meta` 语义仍需继续验证
- 真实程序级 keyboard compatibility 仍需扩大回归覆盖
- IME 虽然已具备 preedit 模型，但仍未完全达到“终端原生级”状态
- 选择/粘贴/焦点仍有部分策略留在宿主控件层

**推荐动作**

- 继续保留并扩展 `InputTranslator`
- 让 Qt 事件先进入 translator，再转为：
  - VT 输入流
  - 模式切换
  - 选择行为命令

**结论**

- 这是 P0/P1 的关键拆分点

---

### 5.5 text buffer ↔ `QTextDocument` + 行号偏移

**微软侧职责**

- 存储文本单元与属性
- scrollback
- viewport 关联
- 插入、删除、擦除、重排

**当前项目对应**

- `QTextDocument`
- `QTextBlock`
- `m_screenBufferStartRow`
- 各类 `write*`、`erase*`、`insert/delete line` 逻辑

**当前状态判断**

- 有“存储体”
- 但不是明确终端缓冲区
- 现在更像在富文本结构上模拟终端网格

**主要缺口**

- 不是终端第一真相源
- 与 Qt 文本结构耦合
- reflow、复杂字符、scrollback trimming 风险高

**推荐动作**

- 新建 `TerminalBuffer`
- 让 `QTextDocument` 从“状态源”降级为“渲染承载”或被替换

建议 `TerminalBuffer` 最低职责：

- primary/alternate buffer
- logical rows/cells
- attributes
- scrollback
- overwrite/erase/delete/insert semantics

**结论**

- 这是当前项目最重要的结构性缺口

---

### 5.6 viewport/scroll model ↔ `m_screenBufferStartRow` + scrollbar

**微软侧职责**

- FollowOutput / BrowseHistory
- scroll offset
- visible region
- cursor 相对视口位置

**当前项目对应**

- `m_screenBufferStartRow`
- `m_followTerminalOutput`
- `verticalScrollBar()->value()`
- `syncCursor()` 内的强制滚动策略

**当前状态判断**

- 最近已从“总是抢到底部”修到：
  - 浏览历史时保位
  - 持续追加输出时跟随到底部
  - 主缓冲区 clear/home 重绘时保持顶部可见
- 但仍未形成独立模型

**主要缺口**

- 状态定义仍散落
- 选择、单击、输入、输出与视口关系未完全统一
- 仍需继续验证 `Claude Code`、`PSReadLine` 这类主缓冲区局部重绘程序的长会话行为

**推荐动作**

- 新建 `ViewportModel`
- 让它成为以下状态唯一真相源：
  - current top row
  - follow output
  - browse history
  - scroll restoration policy

**结论**

- 这是光标与滚动体验稳定的前提

---

### 5.7 cursor model ↔ `m_cursorRow/m_cursorCol/m_cursorVisible`

**微软侧职责**

- 光标位置
- 光标形态
- 可见性
- 与 viewport 的相对关系

**当前项目对应**

- `m_cursorRow`
- `m_cursorCol`
- `m_cursorVisible`
- `syncCursor()` 中的可视同步

**当前状态判断**

- 有基础状态
- 无独立模型
- 视觉上仍依赖编辑器插入点语义

**主要缺口**

- 浏览历史时的隐藏/冻结策略未完全终端化
- IME 锚点虽已显式化，但仍与主缓冲区模型耦合
- 视觉光标与逻辑光标未彻底分离

**推荐动作**

- 新建 `CursorModel`
- 分离：
  - logical cursor state
  - visual cursor rendering state
  - IME anchor state

**结论**

- 这是从“文本框伪装终端”转向“真正终端”的标志点

---

### 5.8 selection model ↔ `QPlainTextEdit` 默认选区

**微软侧职责**

- 选择起点/终点
- 复制序列化规则
- 与滚动历史的关系
- 与光标的关系

**当前项目对应**

- `QPlainTextEdit` 默认选择能力
- `mouseReleaseEvent`
- `contextMenuEvent`

**当前状态判断**

- 有基础可用性
- 但目前仍然更像编辑器选择，不是终端选择模型

**主要缺口**

- 复制后是否清除选区是策略问题，不应只靠当前事件拼接
- 历史区选择与提示符区域关系不清晰
- 缺少独立复制序列化策略

**推荐动作**

- 新建 `SelectionModel`
- 把以下逻辑移出 UI 事件处理器：
  - selection lifecycle
  - copy policy
  - right-click policy

**结论**

- 这是 P1 体验项，但越晚拆成本越高

---

### 5.9 renderer ↔ Qt 富文本默认渲染

**微软侧职责**

- 文本绘制
- 选择层绘制
- 光标层绘制
- 性能优化

**当前项目对应**

- `QPlainTextEdit` + `QTextDocument` 默认渲染

**当前状态判断**

- 目前足够支撑基本显示
- 但不利于完全掌控终端视觉语义

**主要缺口**

- 光标不是独立层
- 选择和文本层耦合
- 未来复杂字符/性能优化空间有限

**推荐动作**

- 当前阶段先不急着完全替换
- 先完成：
  - core 分层
  - buffer/view/cursor 独立
- 然后再评估：
  - 是否做 Qt 自绘控件
  - 是否保留 QTextDocument 只作临时渲染层

**结论**

- 渲染层不是第一个该重写的层
- 但最终大概率不能永远依赖默认富文本渲染

---

### 5.10 conhost/OpenConsole infra ↔ `src/pty/*`

**微软侧职责**

- 控制台宿主/兼容/翻译基础设施
- server / client / terminal 生态中的中间层职责

**当前项目对应**

- `IPtyProcess`
- `ConPtyProcess`
- `UnixPtyProcess`

**当前状态判断**

- 这里只是 PTY 封装层，不是 OpenConsole 的等价物
- 对当前项目来说已经足够扮演“进程与字节流桥梁”

**主要缺口**

- 不是 conhost 兼容生态能力缺口
- 主要还是上层终端语义缺口

**推荐动作**

- 保持 `src/pty/*` 边界清晰
- 不要把终端体验问题错误地下沉到 PTY 层

**结论**

- PTY 层不是当前体验差距的主战场

---

## 6. 推荐的新模块落点

为了让映射可执行，建议在当前项目里新增或显式化以下模块：

| 建议模块 | 当前来源 | 主要替代/承接什么 |
|---|---|---|
| `TerminalSessionCore` 或 `TerminalEngine` | 从 `TerminalWidget` 中抽出 | 隐式核心协调层 |
| `TerminalBuffer` | 从 `QTextDocument` 语义中抽离 | 主/备用缓冲区、属性、scrollback |
| `ViewportModel` | 从 `m_screenBufferStartRow` / scrollbar 逻辑中抽离 | Follow/Browse/visible range |
| `CursorModel` | 从 `m_cursorRow/m_cursorCol/m_cursorVisible` 中抽离 | 逻辑光标与视觉光标 |
| `SelectionModel` | 从默认选区与事件补丁中抽离 | 选择生命周期与复制策略 |
| `InputTranslator` | 从各类 Qt 事件处理器中抽离 | 键鼠/IME/粘贴 → VT 语义 |
| `RenderLayer` | 后续再定 | 文本/选择/光标分层绘制 |

---

## 7. 优先级映射

### 必须先处理的映射断层

1. `TerminalCore` 在本项目中没有显式对应物
2. text buffer 在本项目中被 `QTextDocument` 伪承担
3. viewport model 没有独立层
4. terminal input 没有独立 translator

### 可以稍后处理的映射断层

1. 独立 selection model
2. render layer 重构
3. 更复杂的 renderer 选择

### 当前不应优先处理的映射

1. settings model
2. app layer
3. OpenConsole 整体生态复用

---

## 8. 推荐执行顺序

1. 先让 `TerminalWidget` 停止继续变胖
2. 先抽 `InputTranslator`
3. 再抽 `ViewportModel`
4. 再抽 `CursorModel`
5. 再建立真正的 `TerminalBuffer`
6. 最后再决定是否进入自绘 renderer 路线

原因：

- 输入、滚动、光标是当前最直接影响体验的层
- buffer 是最大工程改造，但也是真正的长期解法
- renderer 只有在模型稳定后改才不会反复返工

---

## 9. 一页式总结

如果只保留一句话：

**微软的模块化终端体系，在当前项目里大多被 `TerminalWidget` 单类吞掉了；后续工作的本质，是把这些职责重新拆回独立终端模块。**

如果只保留行动项：

1. 不再把新能力直接塞进 `TerminalWidget`
2. 先补独立 `InputTranslator`
3. 再补独立 `ViewportModel` 与 `CursorModel`
4. 再从 `QTextDocument` 里迁出真正的 `TerminalBuffer`
5. 再决定渲染层重构

---

## 10. 参考

- 官方模块复用判断  
  [MICROSOFT_TERMINAL_REUSE_MATRIX.md](./MICROSOFT_TERMINAL_REUSE_MATRIX.md)

- 本仓库体验目标  
  [WINDOWS_TERMINAL_EXPERIENCE_PARITY_SPEC.md](./WINDOWS_TERMINAL_EXPERIENCE_PARITY_SPEC.md)

- 当前项目主要模块  
  [terminalwidget.h](</F:\B_My_Document\GitHub\qt-terminal-widget\src\terminal\terminalwidget.h:9>)  
  [ansiparser.h](</F:\B_My_Document\GitHub\qt-terminal-widget\src\terminal\ansiparser.h:11>)  
  [iptyprocess.h](</F:\B_My_Document\GitHub\qt-terminal-widget\src\pty\iptyprocess.h:10>)

- 微软关键模块  
  <https://github.com/microsoft/terminal/blob/main/src/cascadia/TerminalControl/TermControl.cpp>  
  <https://github.com/microsoft/terminal/blob/main/src/cascadia/TerminalCore/Terminal.hpp>
