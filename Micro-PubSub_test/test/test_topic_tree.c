#include "unity.h"
#include "topic_tree.h"
#include "mem_pool.h"
#include <string.h>

/* 模拟接收数据的全局变量，用于验证回调是否被触发 */
static int g_mock_received_value = 0;
static int g_callback_call_count = 0;

static MemPool_t test_pool;

/* 测试用的回调函数 */
void DummyCallback1(MpsHandle_t *payload) {
    if (payload != NULL) {
        g_mock_received_value = *(const int*)(payload->ptr);
    }
    g_callback_call_count++;
    mps_free(&test_pool, payload); // 模拟订阅者消费完 payload 后调用 free
}

void DummyCallback2(MpsHandle_t *payload) {
    g_callback_call_count++; // 用于测试多订阅者分发
    mps_free(&test_pool, payload);
}


void setUp(void) {
    g_mock_received_value = 0;
    g_callback_call_count = 0;
    mps_init(&test_pool);
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

    g_callback_call_count = 0;
    g_mock_received_value = 0;

    Topic_Register("/test/data", QOS_LOG);
    Topic_Subscribe("/test/data", DummyCallback1);
    Topic_Subscribe("/test/data", DummyCallback2);

    uint32_t free_blocks_before = mps_free_count(&test_pool);

    MpsHandle_t h;
    TEST_ASSERT_EQUAL(MPS_OK, Topic_AllocPayload("/test/data", &h));
    TEST_ASSERT_NOT_NULL(h.ptr);

    int *payload = (int*)h.ptr;

    *payload = 1024;

    bool pub_result = Topic_Publish("/test/data", &h);

    TEST_ASSERT_TRUE(pub_result);
    TEST_ASSERT_EQUAL(2, g_callback_call_count);
    TEST_ASSERT_EQUAL(1024, g_mock_received_value);

    uint32_t free_blocks_after = mps_free_count(&test_pool);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        free_blocks_before,
        free_blocks_after,
        "内存泄漏！发布完成后内存块没有被正确回收"
    );
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