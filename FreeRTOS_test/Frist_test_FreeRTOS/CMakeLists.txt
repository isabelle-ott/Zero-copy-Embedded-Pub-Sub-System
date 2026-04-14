cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME Frist_test_FreeRTOS)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# ==============================================================================
# 【核心修复部分】：告诉 CMake 这是裸机交叉编译，测试编译器时只需编译为静态库，不要尝试链接
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
# ==============================================================================

# Core project settings (把 C 和 ASM 语言声明直接放在这里更规范)
project(${CMAKE_PROJECT_NAME} C ASM)
message("Build type: " ${CMAKE_BUILD_TYPE})

# ... 之前修改过的位置 ...
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
project(${CMAKE_PROJECT_NAME} C ASM)

# ===================== 新增硬件参数注入 =====================
# 1. 定义硬件架构相关的编译选项
set(CPU_PARAMETERS
    -mcpu=cortex-m4          # 指定 Cortex-M4 内核
    -mthumb                 # 强制使用 Thumb-2 指令集，解决 "ARM mode" 报错
    -mfloat-abi=hard        # 使用硬件浮点单元
    -mfpu=fpv4-sp-d16       # 指定 FPU 型号
)

# 2. 将这些选项全局应用到所有目标（包括子目录里的库）
add_compile_options(${CPU_PARAMETERS})

# 3. 链接时也需要这些参数
add_link_options(
    ${CPU_PARAMETERS}
    -Wl,--gc-sections       # 移除未使用的函数以节省空间 [cite: 50]
    --specs=nano.specs      # 使用精简版 C 库，适合单片机
)
# =========================================================

# 继续后面的 add_executable ...

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)