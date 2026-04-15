cmake_minimum_required(VERSION 3.22)
#最起码要3.22版本的cmake
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
#C11标准编译
#必须使用真正的C11,不可以自动降级
#允许编译器使用GUN拓展，比如一下GCC额外语法

# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()
#如果没有手动指定构建类型，默认使用Debug


# Set the project name
#项目名称，必须唯一，不能与其它项目名称冲突
set(CMAKE_PROJECT_NAME FreeRTOS_TETS)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
#导出编译命令，生成compile_commands.json,便于IDE更加准确理解工程编译参数

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})
#这是一个正式工程，工程名字叫做CMAKE_PROJECT
#配置阶段输出一段信息，”当前构建的类型是什么“

# Enable CMake support for ASM and C languages
enable_language(C ASM)
#会用到C语言和汇编语言

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})
#创建一个名叫做FreeRTOS_TETS的可执行文件
#指的是最后生成的elf,bin,hex文件

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)
#添加子目录，子目录的路径cmake/stm32cubemx

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)
#给连接器添加库搜索路径，连接器会在这些路径中查找库文件

file(GLOB APP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/app/src/*.c
)

file(GLOB BSP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp/src/*.c
)

file(GLOB COMMON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/common/src/*.c
)

file(GLOB MIDDLEWARE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/middleware/src/*.c
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${APP_SOURCES}
    ${BSP_SOURCES}
    ${COMMON_SOURCES}
    ${MIDDLEWARE_SOURCES}
)
#给可执行文件添加源文件，把自己写的源文件添加到工程进行编译

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app/include
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/common/include
    ${CMAKE_CURRENT_SOURCE_DIR}/middleware/include
)
#添加头文件搜索路径，编译器会在这些路径中查找头文件

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)
#添加编译宏，定义预处理宏，相当于给编译器加-Dxxx

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)
#清掉一个不该自动出现的链接库依赖，ob是object文件，不应该出现在链接库中

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
#目标链接库，之后自己有新的库也要添加进来

#最后打印一下工程的名称
message("Project name: " ${CMAKE_PROJECT_NAME})
