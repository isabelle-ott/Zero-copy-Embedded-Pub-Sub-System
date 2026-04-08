cmake_minimum_required(VERSION 3.10)

project(MicroPubSub C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# =========================================================================
# 【核心修改点】全局注入 TEST 宏
# 这样不仅你的测试文件，连同打包成 middleware_lib 的源文件也会带着 TEST 编译
# 完美解决 undefined reference to `pdMS_TO_TICKS` 报错
# =========================================================================
add_compile_definitions(TEST)

# 1. 编译中间件静态库
set(MIDDLEWARE_SRCS
    middleware/src/mem_pool.c
    middleware/src/topic_tree.c
)
add_library(middleware_lib STATIC ${MIDDLEWARE_SRCS})
target_include_directories(middleware_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/middleware/include
)

# 2. 编译 Unity 测试框架静态库
add_library(unity_lib STATIC third_party/Unity/src/unity.c)
target_include_directories(unity_lib PUBLIC third_party/Unity/src)

# 3. 内存池测试目标
add_executable(test_mem_pool test/test_mem_pool.c)
target_link_libraries(test_mem_pool PRIVATE middleware_lib unity_lib)

# 4. 路由树测试目标 (删除了你原本在底部重复写的 add_executable)
add_executable(test_topic_tree test/test_topic_tree.c)
target_link_libraries(test_topic_tree PRIVATE middleware_lib unity_lib)

# 5. 注册 CTest
enable_testing()
add_test(NAME MemPoolTest COMMAND test_mem_pool)
add_test(NAME TopicTreeTest COMMAND test_topic_tree)