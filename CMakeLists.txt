cmake_minimum_required(VERSION 3.16)

project(qt-terminal-widget VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 开启 Qt 文件的自动编译
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 寻找 Qt 依赖
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
include(CTest)

# 定义 Widget 库目标
add_library(qtterminalwidget
    src/pty/iptyprocess.h
    src/pty/ptyqt.h
    src/pty/ptyqt.cpp
    src/terminal/ansiparser.h
    src/terminal/ansiparser.cpp
    src/terminal/inputtranslator.h
    src/terminal/inputtranslator.cpp
    src/terminal/terminal_global.h
    src/terminal/terminalwidget.h
    src/terminal/terminalwidget.cpp
)

# 平台特定的 PTY 通信源码
if(WIN32)
    target_sources(qtterminalwidget PRIVATE
        src/pty/conptyprocess.h
        src/pty/conptyprocess.cpp
    )
else()
    target_sources(qtterminalwidget PRIVATE
        src/pty/unixptyprocess.h
        src/pty/unixptyprocess.cpp
    )
endif()

# 设置包含路径：
# PUBLIC 保证外部项目链接该库时能够直接使用 #include <terminal/terminalwidget.h>
# PRIVATE 供库自身编译使用
target_include_directories(qtterminalwidget
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/pty
        ${CMAKE_CURRENT_SOURCE_DIR}/src/terminal
)

# 定义编译宏。如果是静态库，不定义 SHARED；如果是动态库，则定义并以 PUBLIC 方式传播
target_compile_definitions(qtterminalwidget PRIVATE QT_TERMINAL_WIDGET_LIBRARY)
get_target_property(LIBRARY_TYPE qtterminalwidget TYPE)
if(LIBRARY_TYPE STREQUAL "SHARED_LIBRARY")
    target_compile_definitions(qtterminalwidget PUBLIC QT_TERMINAL_WIDGET_SHARED)
endif()

# 链接 Qt 依赖
# 因为 TerminalWidget 继承自 QPlainTextEdit，所以 Qt6::Widgets 必须作为 PUBLIC 传递给链接者
target_link_libraries(qtterminalwidget
    PUBLIC
        Qt6::Widgets
    PRIVATE
        Qt6::Core
        Qt6::Gui
)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    add_executable(terminalwidget_wheel_test
        tests/terminalwidget_wheel_test.cpp
    )
    target_link_libraries(terminalwidget_wheel_test PRIVATE
        qtterminalwidget
        Qt6::Test
    )
    add_test(NAME terminalwidget_wheel_test COMMAND terminalwidget_wheel_test)
endif()

# 独立编译 Demo 测试程序
option(BUILD_EXAMPLES "Build example applications" ON)
if(BUILD_EXAMPLES)
    add_executable(qt-terminal-widget-demo WIN32
        examples/main.cpp
    )
    target_link_libraries(qt-terminal-widget-demo PRIVATE
        qtterminalwidget
    )
endif()
