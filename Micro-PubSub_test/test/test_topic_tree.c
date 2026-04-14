#include "unity.h"
#include "topic_tree.h"
#include "mem_pool.h"

#include <string.h>

static MemPool_t test_pool;

#define MOCK_QUEUE_SIZE 8

typedef struct {
    MpsHandle_t buffer[MOCK_QUEUE_SIZE];
    int head;
    int tail;
    int count;
    int fail_after; /* <0: never fail; >=0: 在第 fail_after 次发送后开始失败 */
    int send_count;
} MockQueueObj_t;

static MockQueueObj_t q1;
static MockQueueObj_t q2;

int mps_os_queue_send(MpsQueue_t q, void *item, uint32_t timeout)
{
    (void)timeout;
    MockQueueObj_t *mq = (MockQueueObj_t *)q;

    if (mq == NULL) {
        return 0;
    }

    if (mq->fail_after >= 0 && mq->send_count >= mq->fail_after) {
        mq->send_count++;
        return 0;
    }

    if (mq->count >= MOCK_QUEUE_SIZE) {
        mq->send_count++;
        return 0;
    }

    mq->buffer[mq->tail] = *(MpsHandle_t *)item;
    mq->tail = (mq->tail + 1) % MOCK_QUEUE_SIZE;
    mq->count++;
    mq->send_count++;
    return MPS_PASS;
}

int mps_os_queue_send_isr(MpsQueue_t q, void *item, BaseType_t *woken)
{
    (void)woken;
    return mps_os_queue_send(q, item, 0u);
}

void setUp(void)
{
    mps_init(&test_pool);
    TopicTree_Init(&test_pool);

    memset(&q1, 0, sizeof(q1));
    memset(&q2, 0, sizeof(q2));
    q1.fail_after = -1;
    q2.fail_after = -1;
}

void tearDown(void)
{
}

void test_register_idempotent_and_invalid_name(void)
{
    TEST_ASSERT_TRUE(Topic_Register("/sensor/temp", QOS_LOG));
    TEST_ASSERT_TRUE(Topic_Register("/sensor/temp", QOS_SENSOR));

    TEST_ASSERT_FALSE(Topic_Register(NULL, QOS_LOG));
    TEST_ASSERT_FALSE(Topic_Register("", QOS_LOG));

    char long_name[TOPIC_NAME_MAX_LEN + 8];
    memset(long_name, 'a', sizeof(long_name));
    long_name[sizeof(long_name) - 1] = '\0';
    TEST_ASSERT_FALSE(Topic_Register(long_name, QOS_LOG));
}

void test_subscribe_requires_registered_topic_and_is_idempotent(void)
{
    TEST_ASSERT_FALSE(Topic_Subscribe("/missing", (MpsQueue_t)&q1));

    TEST_ASSERT_TRUE(Topic_Register("/t", QOS_LOG));
    TEST_ASSERT_TRUE(Topic_Subscribe("/t", (MpsQueue_t)&q1));
    TEST_ASSERT_TRUE(Topic_Subscribe("/t", (MpsQueue_t)&q1));

    TEST_ASSERT_FALSE(Topic_Subscribe("/t", NULL));
}

void test_subscribe_fails_when_subscribers_full(void)
{
    TEST_ASSERT_TRUE(Topic_Register("/full", QOS_LOG));

    MockQueueObj_t queues[MAX_SUBSCRIBERS_PER_TOPIC + 1];
    memset(queues, 0, sizeof(queues));
    for (int i = 0; i < MAX_SUBSCRIBERS_PER_TOPIC; i++) {
        TEST_ASSERT_TRUE(Topic_Subscribe("/full", (MpsQueue_t)&queues[i]));
    }

    TEST_ASSERT_FALSE(Topic_Subscribe("/full", (MpsQueue_t)&queues[MAX_SUBSCRIBERS_PER_TOPIC]));
}

void test_alloc_payload_not_found_and_qos_log_full(void)
{
    MpsHandle_t h = {0};
    TEST_ASSERT_EQUAL(MPS_ERR_NOT_FOUND, Topic_AllocPayload("/not_exist", &h));

    TEST_ASSERT_TRUE(Topic_Register("/log", QOS_LOG));
    for (uint32_t i = 0; i < MPS_BLOCK_COUNT; i++) {
        TEST_ASSERT_EQUAL(MPS_OK, mps_alloc(&test_pool, &h));
    }

    TEST_ASSERT_EQUAL(MPS_ERR_FULL, Topic_AllocPayload("/log", &h));
}

void test_alloc_payload_qos_ctrl_returns_full_after_retry(void)
{
    TEST_ASSERT_TRUE(Topic_Register("/ctrl", QOS_CTRL));

    MpsHandle_t hs[MPS_BLOCK_COUNT];
    for (uint32_t i = 0; i < MPS_BLOCK_COUNT; i++) {
        TEST_ASSERT_EQUAL(MPS_OK, mps_alloc(&test_pool, &hs[i]));
    }

    MpsHandle_t out = {0};
    TEST_ASSERT_EQUAL(MPS_ERR_FULL, Topic_AllocPayload("/ctrl", &out));
}

void test_publish_dispatch_and_reference_reclaim(void)
{
    TEST_ASSERT_TRUE(Topic_Register("/pub", QOS_LOG));
    TEST_ASSERT_TRUE(Topic_Subscribe("/pub", (MpsQueue_t)&q1));
    TEST_ASSERT_TRUE(Topic_Subscribe("/pub", (MpsQueue_t)&q2));

    uint32_t free_before = mps_free_count(&test_pool);

    MpsHandle_t h = {0};
    TEST_ASSERT_EQUAL(MPS_OK, Topic_AllocPayload("/pub", &h));
    *(int *)h.ptr = 1234;

    TEST_ASSERT_TRUE(Topic_Publish("/pub", &h));

    TEST_ASSERT_EQUAL(1, q1.count);
    TEST_ASSERT_EQUAL(1, q2.count);
    TEST_ASSERT_EQUAL(1234, *(int *)q1.buffer[q1.head].ptr);
    TEST_ASSERT_EQUAL(1234, *(int *)q2.buffer[q2.head].ptr);

    /* Publish 内部会释放发布者初始引用 */
    TEST_ASSERT_NULL(h.ptr);

    MpsHandle_t c1 = q1.buffer[q1.head];
    MpsHandle_t c2 = q2.buffer[q2.head];
    TEST_ASSERT_EQUAL(MPS_OK, mps_free(&test_pool, &c1));
    TEST_ASSERT_EQUAL(MPS_OK, mps_free(&test_pool, &c2));

    TEST_ASSERT_EQUAL_UINT32(free_before, mps_free_count(&test_pool));
}

void test_publish_send_fail_rolls_back_ref(void)
{
    TEST_ASSERT_TRUE(Topic_Register("/rollback", QOS_LOG));
    TEST_ASSERT_TRUE(Topic_Subscribe("/rollback", (MpsQueue_t)&q1));
    TEST_ASSERT_TRUE(Topic_Subscribe("/rollback", (MpsQueue_t)&q2));

    q2.fail_after = 0; /* q2 首次发送即失败 */

    uint32_t free_before = mps_free_count(&test_pool);

    MpsHandle_t h = {0};
    TEST_ASSERT_EQUAL(MPS_OK, Topic_AllocPayload("/rollback", &h));

    TEST_ASSERT_TRUE(Topic_Publish("/rollback", &h));

    TEST_ASSERT_EQUAL(1, q1.count);
    TEST_ASSERT_EQUAL(0, q2.count);

    /* Publish 内部会释放发布者初始引用 */
    TEST_ASSERT_NULL(h.ptr);

    MpsHandle_t c1 = q1.buffer[q1.head];
    TEST_ASSERT_EQUAL(MPS_OK, mps_free(&test_pool, &c1));

    TEST_ASSERT_EQUAL_UINT32(free_before, mps_free_count(&test_pool));
}

void test_publish_from_isr_dispatch(void)
{
    TEST_ASSERT_TRUE(Topic_Register("/isr", QOS_LOG));
    TEST_ASSERT_TRUE(Topic_Subscribe("/isr", (MpsQueue_t)&q1));

    MpsHandle_t h = {0};
    TEST_ASSERT_EQUAL(MPS_OK, Topic_AllocPayload("/isr", &h));

    BaseType_t woken = 0;
    TEST_ASSERT_TRUE(Topic_PublishFromISR("/isr", &h, &woken));

    TEST_ASSERT_EQUAL(1, q1.count);

    /* PublishFromISR 内部会释放发布者初始引用 */
    TEST_ASSERT_NULL(h.ptr);
    MpsHandle_t c1 = q1.buffer[q1.head];
    TEST_ASSERT_EQUAL(MPS_OK, mps_free(&test_pool, &c1));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_register_idempotent_and_invalid_name);
    RUN_TEST(test_subscribe_requires_registered_topic_and_is_idempotent);
    RUN_TEST(test_subscribe_fails_when_subscribers_full);
    RUN_TEST(test_alloc_payload_not_found_and_qos_log_full);
    RUN_TEST(test_alloc_payload_qos_ctrl_returns_full_after_retry);
    RUN_TEST(test_publish_dispatch_and_reference_reclaim);
    RUN_TEST(test_publish_send_fail_rolls_back_ref);
    RUN_TEST(test_publish_from_isr_dispatch);

    return UNITY_END();
}
