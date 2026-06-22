cmake_minimum_required(VERSION 3.16)

project(qt-terminal-widget VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable automatic compilation of Qt files
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6 modules
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# Include directories
include_directories(src)
include_directories(src/pty)
include_directories(src/terminal)

# Source files
set(SOURCES
    src/main.cpp
    src/pty/iptyprocess.h
    src/pty/ptyqt.h
    src/pty/ptyqt.cpp
    src/terminal/ansiparser.h
    src/terminal/ansiparser.cpp
    src/terminal/terminalwidget.h
    src/terminal/terminalwidget.cpp
)

# Platform-specific files
if(WIN32)
    list(APPEND SOURCES
        src/pty/conptyprocess.h
        src/pty/conptyprocess.cpp
    )
else()
    list(APPEND SOURCES
        src/pty/unixptyprocess.h
        src/pty/unixptyprocess.cpp
    )
endif()

# Define the executable (using WIN32 to run as a standalone window without console)
add_executable(qt-terminal-widget WIN32 ${SOURCES})

# Link libraries
target_link_libraries(qt-terminal-widget PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
