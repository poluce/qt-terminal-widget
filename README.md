# Qt 6 嵌入式终端控件 (qt-terminal-widget)

一个基于 Qt 6 (C++) 编写的跨平台、轻量级嵌入式终端模拟器控件。支持在 Windows (ConPTY) 和 Linux/Unix (PTY) 上无缝嵌入本地系统 Shell（如 PowerShell, cmd, bash 等）。

## 🚀 核心特性

- **跨平台 PTY 支持**：
  - **Windows**：原生支持现代微软 **ConPTY**（Pseudo Console）接口。
  - **Linux/Unix**：支持标准系统的 **Unix PTY** 伪终端。
- **输入协议翻译层**：
  - 已引入独立的 `InputTranslator`，统一处理键盘、鼠标、Win32 input mode、bracketed paste 与 IME 提交路径。
- **主缓冲区滚动体验修正**：
  - 用户未手动滚动时，输出会持续跟随到底部；
  - 主缓冲区 `clear/home` 重绘场景会保持顶部可见，避免 PowerShell 启动页被错误顶走；
  - 用户滚动到历史区后，新的输出不会无条件抢回底部。
- **鼠标协议支持**：
  - 支持 `1000/1002/1003` 鼠标追踪模式；
  - 支持 `1005/1006` 鼠标编码模式；
  - 备用屏仅在程序显式声明接管鼠标时才转发滚轮与鼠标事件。
- **IME 基础支持**：
  - 支持 `commit` 提交；
  - 已接管 `preedit` 预编辑串绘制与候选框锚点定位；
  - 适合继续朝“终端原生级 IME 体验”演进。
- **高精度光标渲染与定位**：通过对 `QTextBlock` 以及 `QTextFragment` 的 `fontStretch` 遍历，精确计算字符宽度，彻底消除由于字符水平缩放导致的光标定位与网格对齐偏差。
- **物理光标隐藏与虚拟反显光标**：通过将系统自带竖线光标设为透明来彻底隐藏硬件光标；利用 ANSI 反显序列（`SGR 7`/`SGR 27`）完美渲染出方块形的虚拟光标。
- **满行自动折行**：支持终端标准的悬挂折行行为（`DECAWM`），在输入字符超出列宽 `m_cols` 时自动移至下一行首。
- **删除无残留白条**：重构了清除（Erase）和填充空白符的文本格式逻辑，避免继承当前光标的反显色彩，彻底根治了退格删除字符时在终端背景留下反显“白条”的严重缺陷。

## 🛠️ 构建与运行

### 环境依赖
- **Qt 6.0+** (Core, Gui, Widgets)
- 支持 C++17 的编译器 (如 MSVC, MinGW, GCC 等)
- **CMake** 3.16+

### 构建步骤
1. 克隆本项目：
   ```bash
   git clone https://github.com/poluce/qt-terminal-widget.git
   cd qt-terminal-widget
   ```
2. 使用 CMake 独立目录编译：
   ```bash
   mkdir build
   cd build
   cmake ..
   cmake --build .
   ```
3. 运行程序：
   编译成功后，在 `build` 目录下生成可执行程序 `qt-terminal-widget`，直接运行即可启动嵌入式终端窗口。

## 🧪 调试日志

项目默认只打开用于视口/跟随输出诊断的 `[VIEWPORT]` 日志，普通 `[TRACE]` 与高频字节流日志默认关闭。

当前日志开关硬编码在 [terminal_global.h](/abs/F:/B_My_Document/GitHub/qt-terminal-widget/src/terminal/terminal_global.h)：

- `TraceEnabled`
- `VerboseTraceEnabled`
- `ViewportTraceEnabled`

建议：

- 排查普通行为问题时，只保留 `ViewportTraceEnabled = true`
- 只有在需要看原始 PTY 字节流、键盘逐事件或 parser 细节时，才临时打开另外两级日志

## 💡 借鉴与参考

- **[ptyqt](https://github.com/kafeg/ptyqt)**：本项目的伪终端底层通信桥接代码借鉴并使用了该项目（基于 MIT 协议开源），其源码整体托管在 `3rdparty/ptyqt` 下，保证了项目的独立性与易克隆性。
- **[Windows Terminal](https://github.com/microsoft/terminal)**：本项目在满行悬挂自动折行（DECAWM）和虚拟/物理光标重构逻辑的设计上，参考了微软官方 Windows Terminal 的文本缓冲区折行与视口对齐机制。

## 📄 开源许可证

本项目基于 **MIT License** 协议开源。详细内容请参阅 [LICENSE](LICENSE) 文件。
