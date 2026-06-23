# Microsoft Terminal Reuse Matrix

## 1. 文档目的

本文档回答一个非常具体的问题：

**为了让 `qt-terminal-widget` 获得接近 Microsoft Windows Terminal 的体验，微软官方仓库中哪些代码或模块值得直接复用，哪些只适合选择性移植，哪些只适合作为参考，哪些不应直接接入 Qt。**

本文档是与 [WINDOWS_TERMINAL_EXPERIENCE_PARITY_SPEC.md](./WINDOWS_TERMINAL_EXPERIENCE_PARITY_SPEC.md) 配套的工程决策文档。

它不讨论“功能是否重要”，而讨论：

- 能不能直接拿
- 拿过来以后是否真的省事
- 是否会把项目绑死在微软自己的 UI/平台栈上
- 对 Qt 集成的实际成本是什么

---

## 2. 结论先行

最重要的结论只有三条：

1. **不是只有 Qt 衔接部分需要自己写。**
   即便复用微软官方核心，也仍然需要自己承担宿主层、输入层、滚动层、选择层、IME 层、剪贴板层、窗口 resize 行为以及大量 Qt 事件集成工作。

2. **最不适合直接拿来接入 Qt 的，是 `TerminalControl`。**
   它是 WinUI/XAML 控件，不是一个可直接塞进 Qt 的通用终端控件。

3. **最值得借鉴或选择性移植的，是微软的“终端语义层”，不是它的“应用控件层”。**
   换句话说，优先考虑：
   - 状态模型
   - text buffer / viewport / scrollback 语义
   - VT parser / input 语义
   - 行为测试与验收逻辑
   
   而不是优先考虑：
   - WinUI 控件
   - App 层
   - 设置 UI

---

## 3. 判定标准

本文档将微软仓库中的模块分为四类：

### A. 可直接复用

满足以下条件才算：

- 与 Qt UI 层弱耦合或无耦合
- 平台依赖可接受
- 拿来后确实能减少大量重复工作
- 不会把当前项目拖入完整的 OpenConsole/WinRT/WinUI 依赖体系

### B. 可选择性移植

满足以下条件：

- 设计与实现非常有价值
- 但直接链接或直接编译接入成本过高
- 更适合抽取思想、接口形状、状态模型、测试方法，按本项目重写或局部移植

### C. 只适合参考

满足以下条件：

- 主要价值在于帮助理解成熟终端如何组织逻辑
- 但工程集成收益小于成本
- 直接复用会引入不必要复杂度

### D. 不建议直接接入 Qt

满足以下条件之一：

- 明确依赖 WinUI / XAML / Windows 应用宿主
- 与 Qt 事件系统、渲染系统、控件体系正面冲突
- 为了“复用”反而会增加总复杂度

---

## 4. 许可证与复用边界

微软官方 `terminal` 仓库本身采用 MIT License，法律上允许复用、修改、分发，但工程上仍需注意：

- 仓库内存在大量内部依赖和第三方依赖
- 某些模块并不是独立可消费库，而是 OpenConsole 工程体系的一部分
- “法律允许”不等于“集成成本低”

参考：

- 仓库根 README：<https://github.com/microsoft/terminal>
- LICENSE：<https://github.com/microsoft/terminal/blob/main/LICENSE>

---

## 5. 复用决策矩阵

| 模块/目录 | 分类 | 结论 | 原因 | 对 Qt 项目的建议 |
|---|---|---|---|---|
| `src/cascadia/TerminalControl` | D. 不建议直接接入 Qt | 不要直接接 | 这是 WinUI/XAML 控件层，直接操作 UI 外观、滚动条、面板、线程上下文，不是通用控件内核 | 不要尝试“桥接一下就用” |
| `src/cascadia/TerminalCore` | B. 可选择性移植 | 高价值，但不要整库生搬 | 它承载大量非 UI 终端语义，但依赖 OpenConsole 自己的 buffer、renderer、parser、WIL、WinRT 类型 | 研究其状态模型、接口边界和滚动语义，选择性移植 |
| `src/terminal/parser` / VT parser 相关 | A/B 之间，倾向 B | 值得优先吸收 | 这是成熟终端的核心能力之一，直接决定兼容性；但需要评估依赖链与本项目现有 parser 的替换成本 | 优先研究；能独立剥离则直接复用，否则按其语义重构现有 parser |
| `src/terminal/input` / terminal input 相关 | B. 可选择性移植 | 非常值得借鉴 | 终端输入语义是体验关键，但通常会和平台事件系统深度耦合，不能无脑直接接 | 参考其键盘模式、bracketed paste、mouse mode、Alt/Ctrl 语义 |
| `src/buffer/out/textBuffer*` | B. 可选择性移植 | 高价值 | 终端缓冲区是体验核心；这是比“继续堆在 QTextDocument 上”更接近正确方向的现成模型 | 重点研究其数据模型、viewport 关系、scrollback 处理 |
| `src/renderer/*` | C/B 之间 | 只在 Windows-only 目标下值得深看 | 官方 renderer 明显服务于其自己的渲染栈；如果项目继续走 Qt 自绘或跨平台路线，直接接入收益不高 | 参考渲染分层；不建议优先直接接 |
| DirectWrite / AtlasEngine 路线 | C. 只适合参考 | 不是当前首要复用目标 | 即便官方 renderer 很强，也不意味着适合嵌入 Qt；先解决终端语义比先追官方绘制引擎更重要 | 先不碰，除非项目明确转向 Windows-only 高性能渲染 |
| `src/cascadia/TerminalSettingsModel` | D/C | 不建议直接接入 | 这是官方设置与配置体系的一部分，不直接提高终端核心体验 | 不要优先复用 |
| `src/cascadia/TerminalApp` | D. 不建议直接接入 Qt | 完全不建议 | 这是应用层，不是终端核心；与 Qt 控件目标无关 | 不要接 |
| `conhost` / `OpenConsole` 宿主相关 | C. 只适合参考 | 可学习，不宜直接搬 | 它解决的是微软控制台/伪控制台生态中的服务端与兼容问题，不等于 Qt 宿主层可以直接复用 | 参考其职责边界，不要整块集成 |
| 官方测试与 issue 讨论 | A. 可直接吸收方法 | 强烈建议复用其思路 | 即使不复用代码，也应复用其行为定义、问题分类、回归思路 | 直接转化为本项目测试矩阵和回归集 |

---

## 6. 分项说明

### 6.1 `TerminalControl`：不应作为 Qt 复用目标

判定：**不建议直接接入 Qt**

原因：

- 它是官方的控件层，不是纯终端内核
- 它明显依赖 WinUI/XAML 宿主能力
- 它管理滚动条、外观、背景、面板、UI 线程调用等宿主细节
- 对 Qt 来说，这不是“包一层适配器”的问题，而是“两套 UI 世界观”的冲突

参考：

- `TermControl.cpp`：<https://github.com/microsoft/terminal/blob/main/src/cascadia/TerminalControl/TermControl.cpp>

落地建议：

- 不要把“能不能直接吃 `TerminalControl`”当成主方案
- 如果未来真要用它，那实际上意味着你在做 WinUI 宿主，不再是 Qt 原生控件方案

### 6.2 `TerminalCore`：是最值得吸收的中层，但不适合整块硬接

判定：**高价值，可选择性移植**

原因：

- 官方 issue 和源码都表明，它承载大量 non-UI terminal logic
- 它比 `TerminalControl` 更接近你真正需要的东西
- 但它不是一个轻量、纯净、跨平台、零依赖内核
- 它仍然依赖微软自己的类型系统和工程结构

参考：

- `Terminal.hpp`：<https://github.com/microsoft/terminal/blob/main/src/cascadia/TerminalCore/Terminal.hpp>
- `terminalcore-lib.vcxproj`：<https://github.com/microsoft/terminal/blob/main/src/cascadia/TerminalCore/lib/terminalcore-lib.vcxproj>

从公开信息能看到的依赖特征包括：

- text buffer
- renderer interfaces
- terminal parser
- terminal input
- WIL / WinRT / OpenConsole 自有 types

落地建议：

- 不要尝试“一次性把 TerminalCore 编进这个 Qt 项目”
- 优先提取：
  - 状态机设计
  - viewport / scroll offset 语义
  - 输入输出边界
  - 备用屏、scrollback、buffer/view 分离逻辑

### 6.3 VT parser：是优先级最高的复用候选之一

判定：**优先研究，优先吸收**

原因：

- VT parser 直接决定兼容性下限
- 当前项目已经有自己的 `AnsiParser`，但能力仍较轻量
- 在体验对标目标下，parser 是最不适合长期“自己边修边猜”的部分之一

落地建议：

- 先比对官方 parser 能力范围与当前 `src/terminal/ansiparser.*`
- 评估能否局部替换
- 若直接替换成本过高，则至少按官方 parser 的能力面重构当前实现

### 6.4 terminal input：必须深度借鉴，但通常要本地接管

判定：**可选择性移植**

原因：

- 输入体验是用户体感最强的层
- 但这层天然需要与 Qt 的 `QKeyEvent`、`QInputMethodEvent`、鼠标事件、剪贴板、焦点模型做深度集成
- 所以很难“原样拿来”

落地建议：

- 把官方 input 层当作协议语义来源
- Qt 端自己实现 `InputTranslator`
- 重点对齐：
  - Alt / Ctrl / Meta 组合语义
  - bracketed paste
  - mouse mode
  - application cursor keys
  - IME preedit / commit

当前状态更新：

- 本项目已经引入独立的 `InputTranslator`
- 键盘、粘贴、Win32 input mode、鼠标模式与 IME 提交路径已不再直接散落在单一事件分支里
- 后续重点不再是“是否要抽 translator”，而是“如何继续扩展其协议覆盖面并减少 `TerminalWidget` 中残余输入策略逻辑”

### 6.5 text buffer：这是最该优先学习甚至借鉴设计的部分

判定：**高优先级选择性移植**

原因：

- 当前项目最大的长期风险，是把终端主状态压在 `QTextDocument` 上
- 官方 text buffer 明显更接近真正终端模型
- 如果你的目标是“功能都达到”，那么 buffer/view/cursor 的模型正确性比 UI 涂层更关键

落地建议：

- 优先研究它如何处理：
  - buffer 与 viewport 分离
  - scrollback
  - cursor relative position
  - reflow / resize 相关状态
- 不要求直接源码级复制，但必须把模型思路吸收到本项目的新 `TerminalBuffer`

### 6.6 renderer：不要把它当成当前第一优先级

判定：**只适合参考，Windows-only 时再深入**

原因：

- 官方 README 明确提到它们有 DirectWrite-based text layout/rendering engine，官方概览也明确提到 GPU accelerated text rendering
- 但对当前项目最致命的问题不是“画得不够像”，而是“终端语义还未稳定”
- 在终端模型不稳时先追官方 renderer，只会更复杂

参考：

- README：<https://github.com/microsoft/terminal>
- Overview：<https://learn.microsoft.com/en-us/windows/terminal/>

落地建议：

- 先解决 buffer / viewport / cursor / input / alternate screen
- renderer 分层可以参考，但不要优先试图接入 AtlasEngine 或整套微软 renderer

### 6.7 Settings / App 层：不要碰

判定：**不建议直接复用**

原因：

- 它们不直接解决当前体验痛点
- 引入后只会增加依赖与维护面积
- 会让项目偏离“嵌入式 Qt 终端控件”目标

落地建议：

- 完全后置
- 在核心终端体验稳定前，不为这类模块投入时间

### 6.8 conhost / OpenConsole：理解它，不要把它搬进来

判定：**只适合参考**

原因：

- 这是微软控制台生态中的 server / translator / compatibility infrastructure
- 你的项目目标是 Qt 终端控件，不是重建微软整套 console host 体系

落地建议：

- 学它的职责边界
- 不要试图把它整合成你的宿主层

### 6.9 官方测试思路：这是应当直接吸收的高价值资产

判定：**最值得直接借鉴的方法资产**

原因：

- 即使不用他们的代码，测试模型和行为定义也能直接提高项目质量
- 你当前最需要的，不只是更强代码，而是更强的“功能完成判定方式”

落地建议：

- 把官方 issue 里的高频终端行为问题转成自己的测试矩阵
- 建立：
  - parser regression
  - alternate screen regression
  - scrollback / viewport regression
  - input compatibility regression

---

## 7. 回答原问题：是不是只有和 Qt 衔接的部分才是我们需要的？

不是。

更准确的说法是：

- **Qt 宿主层必须自己做**
- **但终端语义层不能只靠自己猜**

你需要的不是“微软 UI 控件”，而是“微软已经验证过的终端行为模型”。

真正应该自己做的部分：

- Qt 事件接入
- Qt 绘制或 Qt 自定义渲染宿主
- Qt 剪贴板、右键、选择、IME、焦点集成
- 跨平台封装边界

真正不应该完全闭门自己发明的部分：

- buffer / viewport / scrollback 模型
- alternate screen 语义
- terminal input 语义
- VT parser 能力面
- Unicode / width / cursor 相对关系

---

## 8. 推荐复用策略

### 策略 1：不要整仓搬运

不建议：

- 直接把微软工程作为子模块后强行接进当前 Qt 项目
- 试图把 `TerminalControl` 直接包一层用于 Qt
- 试图在当前阶段完整编入 `TerminalCore`

### 策略 2：优先吸收“模型”，再吸收“代码”

优先级建议：

1. 研究官方 parser / input / text buffer / viewport 语义
2. 用这些语义重构当前 spec 与能力矩阵
3. 再评估哪些底层代码块可以局部移植

### 策略 3：如果只做 Windows，可以更激进

如果未来项目明确只做 Windows，可以考虑：

- 深入复用 `TerminalCore` 相关逻辑
- 研究接入部分官方 renderer
- 接受更多微软生态依赖

但前提是：

- 你愿意显著增加构建复杂度
- 你接受项目从“Qt 终端控件”向“Windows 终端宿主”偏移

### 策略 4：如果继续追求跨平台，Qt 只应是宿主，不应是终端真相源

推荐做法：

- Qt 负责宿主与界面接入
- 终端真实状态由你自己的 `TerminalBuffer + ViewportModel + CursorModel + InputTranslator` 管理
- 这些模型尽量向微软成熟终端语义靠拢

---

## 9. 推荐执行顺序

### 第一阶段：只做研究与映射

- 梳理微软官方模块与当前项目模块的一一映射
- 标出当前项目哪些能力完全空缺，哪些只是轻量实现
- 输出能力矩阵

### 第二阶段：先借鉴行为模型，不急着搬代码

- 先按官方模型重构本项目 spec 与状态机
- 修正当前最影响体验的 P0 项

### 第三阶段：再评估局部移植

优先评估：

- parser 相关能力
- input 相关能力
- buffer / viewport 相关实现思路

### 第四阶段：最后才考虑 renderer 与更深复用

前提：

- P0/P1 行为已经稳定
- 终端语义已从 `QTextDocument` 里剥离

---

## 10. 一页式决策摘要

如果只允许一句话总结：

**不要直接复用微软的控件层；优先复用或借鉴它的终端语义层；Qt 只负责宿主，不应再承担终端真相源。**

如果只允许给出行动建议：

1. 不碰 `TerminalControl`
2. 深读 `TerminalCore`
3. 优先研究 parser / input / text buffer / viewport
4. 把这些语义转成你自己的 Qt 终端模型
5. 最后再考虑 renderer

---

## 11. 参考

- Windows Terminal 仓库根 README  
  <https://github.com/microsoft/terminal>

- `TerminalControl`  
  <https://github.com/microsoft/terminal/blob/main/src/cascadia/TerminalControl/TermControl.cpp>

- `TerminalCore` 头文件  
  <https://github.com/microsoft/terminal/blob/main/src/cascadia/TerminalCore/Terminal.hpp>

- `terminalcore-lib.vcxproj`  
  <https://github.com/microsoft/terminal/blob/main/src/cascadia/TerminalCore/lib/terminalcore-lib.vcxproj>

- Windows Terminal 官方概览  
  <https://learn.microsoft.com/en-us/windows/terminal/>

- 本仓库体验目标文档  
  [WINDOWS_TERMINAL_EXPERIENCE_PARITY_SPEC.md](./WINDOWS_TERMINAL_EXPERIENCE_PARITY_SPEC.md)
