#include "topic_tree.h"
#include "mem_pool.h"
#include <string.h>
#include <assert.h>

/* ================== 跨平台 RTOS 接口适配 ================== */
#ifdef TEST  // 如果是 Unity 单元测试环境
    #include <unistd.h>
    // 在 Linux 测试环境下，将 FreeRTOS 的 Tick 转换宏 mock 掉
    #define pdMS_TO_TICKS(ms) (ms)
    
    // 在 Linux 测试环境下，用 usleep 替代 vTaskDelay
    // 注意：入参是毫秒，usleep 需要微秒
    #define vTaskDelay(ms) usleep((ms) * 1000) 


#else       // 真实的单片机环境
    #include "FreeRTOS.h"
    #include "task.h"
#endif
/* ========================================================= */

// ... 下面是你原本的代码

/* 全局静态分配池，内存占用绝对可预测  */
static TopicNode_t g_topic_pool[MAX_TOPICS];
static MemPool_t *g_topic_mem_pool = NULL;


static int _find_topic_index(const char* topic_name) {
    for (int i = 0; i < MAX_TOPICS; i++) { // 确保 MAX_TOPICS 已在你的头文件中定义
        if (g_topic_pool[i].is_used && 
            strncmp(topic_name, g_topic_pool[i].name, TOPIC_NAME_MAX_LEN) == 0) {
            return i;
        }
    }
    return -1;
    
}

void TopicTree_Init(MemPool_t *pool_ptr) {
    assert(pool_ptr != NULL);
    memset(g_topic_pool, 0, sizeof(g_topic_pool));
    g_topic_mem_pool = pool_ptr;
}

bool Topic_Register(const char* topic_name, QoS_Level_t qos) {
    int free_index = -1;

    // 参数校验：统一放在最前面
    if (topic_name == NULL || topic_name[0] == '\0') return false;
    if (strnlen(topic_name, TOPIC_NAME_MAX_LEN) >= TOPIC_NAME_MAX_LEN) return false;

    for (int i = 0; i < MAX_TOPICS; i++) {
        if (g_topic_pool[i].is_used) {
            if (strncmp(topic_name, g_topic_pool[i].name, TOPIC_NAME_MAX_LEN) == 0) {
                return true; // 已存在，幂等成功
            }
        } else if (free_index == -1) {
            free_index = i;
        }
    }

    if (free_index == -1) return false; // 池已满

    strncpy(g_topic_pool[free_index].name, topic_name, TOPIC_NAME_MAX_LEN);
    g_topic_pool[free_index].name[TOPIC_NAME_MAX_LEN - 1] = '\0';
    g_topic_pool[free_index].qos = qos;
    g_topic_pool[free_index].is_used = true;
    g_topic_pool[free_index].subscriber_count = 0;

    return true;
}

bool Topic_Subscribe(const char* topic_name, TopicCallback_t callback) {
    int topic_index = -1;
    int free_sub_index = -1;

    if (topic_name == NULL || callback == NULL) return false;

    for (int i = 0; i < MAX_TOPICS; i++) {
        if (g_topic_pool[i].is_used &&
            strncmp(topic_name, g_topic_pool[i].name, TOPIC_NAME_MAX_LEN) == 0) {
            topic_index = i;
            break;
        }
    }  // ← 花括号正确闭合

    if (topic_index == -1) return false;

    TopicNode_t *topic = &g_topic_pool[topic_index];

    for (int j = 0; j < MAX_SUBSCRIBERS_PER_TOPIC; j++) {
        if (topic->subscribers[j].is_used) {
            if (topic->subscribers[j].callback == callback) return true; // 重复订阅
        } else if (free_sub_index == -1) {
            free_sub_index = j;
        }
    }

    if (free_sub_index == -1) return false; // 订阅者已满

    topic->subscribers[free_sub_index].callback = callback;
    topic->subscribers[free_sub_index].is_used = true;
    topic->subscriber_count++;

    return true;
}

bool Topic_Publish(const char* topic_name, MpsHandle_t* payload)
{
    /* === 1. 参数与状态校验 === */
    /* 错误时不调用 mps_free！失败时所有权退回给调用者，由调用者负责兜底释放 */
    if (payload == NULL || payload->ptr == NULL) {
        return false;
    }

    if (topic_name == NULL || g_topic_mem_pool == NULL) {
        return false;
    }

    int topic_index = _find_topic_index(topic_name);
    if (topic_index == -1) {
        return false;
    }

    TopicNode_t *topic = &g_topic_pool[topic_index];

    TopicCallback_t local_callbacks[MAX_SUBSCRIBERS_PER_TOPIC];
    int count = 0;

    /* === 2. 锁内：只做快照 === */
    Topic_Lock(topic);
    for (int i = 0; i < MAX_SUBSCRIBERS_PER_TOPIC; i++) {
        if (topic->subscribers[i].is_used &&
            topic->subscribers[i].callback != NULL) {

            local_callbacks[count++] = topic->subscribers[i].callback;
        }
    }
    Topic_Unlock(topic);

    /* === 3. 锁外：统一加引用 === */
    for (int i = 0; i < count; i++) {
        if (mps_add_ref(g_topic_mem_pool, payload) != MPS_OK) {
            /* 回滚已加的引用 */
            for (int k = 0; k < i; k++) {
                /* 【注意】：回滚时必须使用副本，防止原始 payload 的 ptr 被意外置空 */
                MpsHandle_t rollback_handle = *payload; 
                (void)mps_free(g_topic_mem_pool, &rollback_handle);
            }
            return false; /* 致命错误：回滚后返回 false，原 payload 仍由发布者负责 */
        }
    }

    /* === 4. 调用回调 (零拷贝分发) === */
    for (int i = 0; i < count; i++) {
        /* * 【核心修复】：为每个订阅者生成独立的 Handle 副本。
         * 物理内存块(ptr)是共享的(零拷贝)，但句柄(Handle)是隔离的。
         * 这样订阅者调用 mps_free 置空 sub_handle.ptr 时，不会影响其他人和发布者。
         */
        MpsHandle_t sub_handle = *payload;
        local_callbacks[i](&sub_handle);
    }

    /* === 5. 发布者释放初始引用 === */
    /* * 走到这里意味着分发成功。发布者最初 alloc 产生的 ref_count=1 已经完成使命。
     * 此时调用 mps_free，如果没有任何订阅者 (count == 0)，这块内存会被真正回收；
     * 如果有订阅者，它仅仅是 ref_count 减 1，内存仍由存活的订阅者持有。
     */
    return (mps_free(g_topic_mem_pool, payload) == MPS_OK);
}


MpsStatus_t Topic_AllocPayload(const char* topic_name, MpsHandle_t *out)
{
    /* 1. 参数校验 */
    if (g_topic_mem_pool == NULL || topic_name == NULL || out == NULL) {
        return MPS_ERR_INVALID;
    }
    
    if (strlen(topic_name) >= TOPIC_NAME_MAX_LEN) {
        return MPS_ERR_INVALID;
    }
    
    /* 2. 查找 Topic */
    int topic_index = -1;
    for (int i = 0; i < MAX_TOPICS; i++) {
        if (g_topic_pool[i].is_used &&
            strncmp(topic_name, g_topic_pool[i].name, TOPIC_NAME_MAX_LEN) == 0) {  // ← 改用 strncmp
            topic_index = i;
            break;
        }
    }
    
    if (topic_index == -1) {
        return MPS_ERR_NOT_FOUND;  // ← 更明确的错误码
    }
    
    TopicNode_t *topic = &g_topic_pool[topic_index];
    int retry_count = 0;
    
    /* 3. 分配内存与 QoS 重试逻辑 */
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
//                TickType_t delay_ticks = pdMS_TO_TICKS(1);
//                vTaskDelay(delay_ticks > 0 ? delay_ticks : 1);
//之后引入RTOS再加吧

                retry_count++;
                break;
            default:
                return MPS_ERR_INVALID;
        }
    }
}
//目前先把QoS最简化，之后阶段再完善复杂的算法
