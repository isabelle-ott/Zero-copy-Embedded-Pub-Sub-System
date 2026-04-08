#include "unity.h"
#include "topic_tree.h"
#include "mem_pool.h"
#include <string.h>

/* 模拟接收数据的全局变量，用于验证回调是否被触发 */
static int g_mock_received_value = 0;
static int g_callback_call_count = 0;

static MemPool_t test_pool;

/* 测试用的回调函数 */
void DummyCallback1(const void* payload) {
    if (payload != NULL) {
        g_mock_received_value = *(const int*)payload;
    }
    g_callback_call_count++;
}

void DummyCallback2(const void* payload) {
    g_callback_call_count++; // 用于测试多订阅者分发
}

/* 每次测试前执行 */
#include "unity.h"
#include "mem_pool.h"
#include "topic_tree.h"


void setUp(void) {
    // 1. 初始化内存池（内部自带存储区并会自动清空位图和引用计数）
    mps_init(&test_pool);

    // 2. 用初始化好的内存池指针来初始化 Topic Tree
    TopicTree_Init(&test_pool);
}

void tearDown(void) {
    // 测试结束后的清理工作（如果有）
}

/* 重点测试 1：Topic 的成功注册与重复注册处理 */
void test_TopicRegistration(void) {
    // 成功注册
    TEST_ASSERT_TRUE(Topic_Register("/sys/ota/ctrl", QOS_CTRL));
    TEST_ASSERT_TRUE(Topic_Register("/sensor/imu", QOS_SENSOR));
    
    // TODO: 编写断言测试超出 MAX_TOPICS 容量时的拒绝行为
    
    // TODO: 编写断言测试相同 Topic 重复注册时的行为期望
}

/* 重点测试 2：精准遍历与分发（回调函数触发验证） */
void test_PublishDispatch(void) {
    int test_data = 1024;
    
    Topic_Register("/test/data", QOS_LOG);
    Topic_Subscribe("/test/data", DummyCallback1);
    Topic_Subscribe("/test/data", DummyCallback2);
    
    // 执行发布
    bool pub_result = Topic_Publish("/test/data", &test_data);
    
    // 验证结果
    TEST_ASSERT_TRUE(pub_result);
    // 两个订阅者，所以应该被调用两次
    TEST_ASSERT_EQUAL(2, g_callback_call_count); 
    // 验证数据正确传递（零拷贝的指针传递效果）
    TEST_ASSERT_EQUAL(1024, g_mock_received_value);
}

/* 重点测试 3：QoS 行为预演验证 */
void test_QosBehaviorMock(void) {
    // TODO: 编写测试来模拟内存池满的情况。
    // 例如：设计一个内部标志位强制开启“内存满”状态，
    // 然后分别对 QOS_LOG 发布数据，断言返回 false（丢弃）；
    // 对 QOS_CTRL 发布数据，断言返回某种阻塞状态或特定标志。
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_TopicRegistration);
    RUN_TEST(test_PublishDispatch);
    RUN_TEST(test_QosBehaviorMock);
    return UNITY_END();
}