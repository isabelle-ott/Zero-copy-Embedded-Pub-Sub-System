#include "topic_tree.h"

#include <assert.h>
#include <string.h>

#ifdef TEST
    #include <unistd.h>
#else
    #include "FreeRTOS.h"
    #include "task.h"
#endif

/* 全局静态分配池，内存占用绝对可预测 */
static TopicNode_t g_topic_pool[MAX_TOPICS];
static MemPool_t *g_topic_mem_pool = NULL;

static int _find_topic_index(const char *topic_name)
{
    if (topic_name == NULL || topic_name[0] == '\0') {
        return -1;
    }

    for (int i = 0; i < MAX_TOPICS; i++) {
        if (g_topic_pool[i].is_used &&
            strncmp(topic_name, g_topic_pool[i].name, TOPIC_NAME_MAX_LEN) == 0) {
            return i;
        }
    }

    return -1;
}

void TopicTree_Init(MemPool_t *pool_ptr)
{
    assert(pool_ptr != NULL);
    memset(g_topic_pool, 0, sizeof(g_topic_pool));
    g_topic_mem_pool = pool_ptr;
}

bool Topic_Register(const char *topic_name, QoS_Level_t qos)
{
    int free_index = -1;

    if (topic_name == NULL || topic_name[0] == '\0') {
        return false;
    }

    if (strnlen(topic_name, TOPIC_NAME_MAX_LEN) >= TOPIC_NAME_MAX_LEN) {
        return false;
    }

    int existing = _find_topic_index(topic_name);
    if (existing >= 0) {
        return true; /* 幂等 */
    }

    for (int i = 0; i < MAX_TOPICS; i++) {
        if (!g_topic_pool[i].is_used) {
            free_index = i;
            break;
        }
    }

    if (free_index < 0) {
        return false;
    }

    TopicNode_t *topic = &g_topic_pool[free_index];
    strncpy(topic->name, topic_name, TOPIC_NAME_MAX_LEN);
    topic->name[TOPIC_NAME_MAX_LEN - 1] = '\0';
    topic->qos = qos;
    topic->is_used = true;

    return true;
}

bool Topic_Subscribe(const char *topic_name, MpsQueue_t queue)
{
    /* queue 判空在此保留：
     * 1) TEST 环境下 MpsQueue_t=void*，NULL 检查有效；
     * 2) 作为中间件边界防御，即使上层已校验也可防误用。
     */
    if (topic_name == NULL || queue == NULL) {
        return false;
    }

    int topic_index = _find_topic_index(topic_name);
    if (topic_index < 0) {
        return false;
    }

    TopicNode_t *topic = &g_topic_pool[topic_index];
    int free_sub_index = -1;

    for (int j = 0; j < MAX_SUBSCRIBERS_PER_TOPIC; j++) {
        if (topic->subscribers[j].is_used) {
            if (topic->subscribers[j].queue == queue) {
                return true; /* 重复订阅幂等 */
            }
        } else if (free_sub_index < 0) {
            free_sub_index = j;
        }
    }

    if (free_sub_index < 0) {
        return false;
    }

    topic->subscribers[free_sub_index].queue = queue;
    topic->subscribers[free_sub_index].is_used = true;

    return true;
}

bool Topic_Publish(const char *topic_name, MpsHandle_t *payload)
{
    /* 合约：payload 必须来自 Topic_AllocPayload/mps_alloc，且当前持有发布者引用。 */
    if (topic_name == NULL || payload == NULL || payload->ptr == NULL || g_topic_mem_pool == NULL) {
        return false;
    }

    int topic_index = _find_topic_index(topic_name);
    if (topic_index < 0) {
        return false;
    }

    TopicNode_t *topic = &g_topic_pool[topic_index];
    MpsQueue_t local_queues[MAX_SUBSCRIBERS_PER_TOPIC];
    int count = 0;

    Topic_Lock(topic);
    for (int i = 0; i < MAX_SUBSCRIBERS_PER_TOPIC; i++) {
        if (topic->subscribers[i].is_used && topic->subscribers[i].queue != NULL) {
            local_queues[count++] = topic->subscribers[i].queue;
        }
    }
    Topic_Unlock(topic);

    for (int i = 0; i < count; i++) {
        if (mps_add_ref(g_topic_mem_pool, payload) != MPS_OK) {
            continue;
        }

        MpsHandle_t sub_handle = *payload;
        if (mps_os_queue_send(local_queues[i], &sub_handle, 0u) != MPS_PASS) {
            (void)mps_free(g_topic_mem_pool, &sub_handle);
        }
    }

    /* 释放发布者初始持有的引用，避免零拷贝分发后遗留 ref_count=1。 */
    if (mps_free(g_topic_mem_pool, payload) != MPS_OK) {
        return false; /* 违反 payload 来源合约或句柄已失效 */
    }

    return true;
}

bool Topic_PublishFromISR(const char *topic_name, MpsHandle_t *payload, BaseType_t *pxHigherPriorityTaskWoken)
{
    /* 合约：payload 必须来自 Topic_AllocPayload/mps_alloc，且当前持有发布者引用。 */
    if (topic_name == NULL || payload == NULL || payload->ptr == NULL || g_topic_mem_pool == NULL) {
        return false;
    }

    int topic_index = _find_topic_index(topic_name);
    if (topic_index < 0) {
        return false;
    }

    TopicNode_t *topic = &g_topic_pool[topic_index];
    MpsQueue_t local_queues[MAX_SUBSCRIBERS_PER_TOPIC];
    int count = 0;

#ifdef TEST
    unsigned long saved = Mps_EnterCriticalFromISR();
#else
    UBaseType_t saved = Mps_EnterCriticalFromISR();
#endif
    for (int i = 0; i < MAX_SUBSCRIBERS_PER_TOPIC; i++) {
        if (topic->subscribers[i].is_used && topic->subscribers[i].queue != NULL) {
            local_queues[count++] = topic->subscribers[i].queue;
        }
    }
    Mps_ExitCriticalFromISR(saved);

    for (int i = 0; i < count; i++) {
        if (mps_add_ref_isr(g_topic_mem_pool, payload) != MPS_OK) {
            continue;
        }

        MpsHandle_t sub_handle = *payload;
        if (mps_os_queue_send_isr(local_queues[i], &sub_handle, pxHigherPriorityTaskWoken) != MPS_PASS) {
            (void)mps_free_isr(g_topic_mem_pool, &sub_handle);
        }
    }

    /* 释放发布者初始持有的引用，避免 ISR 零拷贝分发后遗留 ref_count=1。 */
    if (mps_free_isr(g_topic_mem_pool, payload) != MPS_OK) {
        return false; /* 违反 payload 来源合约或句柄已失效 */
    }

    return true;
}

MpsStatus_t Topic_AllocPayload(const char *topic_name, MpsHandle_t *out)
{
    if (g_topic_mem_pool == NULL || topic_name == NULL || out == NULL) {
        return MPS_ERR_INVALID;
    }

    if (strnlen(topic_name, TOPIC_NAME_MAX_LEN) >= TOPIC_NAME_MAX_LEN) {
        return MPS_ERR_INVALID;
    }

    int topic_index = _find_topic_index(topic_name);
    if (topic_index < 0) {
        return MPS_ERR_NOT_FOUND;
    }

    TopicNode_t *topic = &g_topic_pool[topic_index];
    int retry_count = 0;

    while (1) {
        MpsStatus_t s = mps_alloc(g_topic_mem_pool, out);
        if (s == MPS_OK) {
            return MPS_OK;
        }

        switch (topic->qos) {
            case QOS_LOG:
            case QOS_SENSOR:
                return MPS_ERR_FULL;

            case QOS_CTRL:
                if (retry_count >= MAX_CTRL_ALLOC_RETRY) {
                    return MPS_ERR_FULL;
                }
                retry_count++;
#ifdef TEST
                usleep(1000); /* 1ms，避免 Linux 测试环境空转占满 CPU */
#else
                vTaskDelay(pdMS_TO_TICKS(1));
#endif
                break;

            default:
                return MPS_ERR_INVALID;
        }
    }
}
