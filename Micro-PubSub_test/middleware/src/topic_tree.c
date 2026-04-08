#include "topic_tree.h"
#include "mem_pool.h"
#include <string.h>

#include "topic_tree.h"
#include <string.h>

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

void TopicTree_Init(MemPool_t *pool_ptr) {
    memset(g_topic_pool, 0, sizeof(g_topic_pool));
    g_topic_mem_pool = pool_ptr; // 绑定内存池
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

bool Topic_Subscribe(const char* topic_name, PubSubCallback_t callback) {
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

bool Topic_Publish(const char* topic_name, const void* payload) {
    if (topic_name == NULL || payload == NULL || g_topic_mem_pool == NULL) return false;

    // 1. 查找 Topic
    int topic_index = -1;
    for (int i = 0; i < MAX_TOPICS; i++) {
        if (g_topic_pool[i].is_used && 
            strncmp(topic_name, g_topic_pool[i].name, TOPIC_NAME_MAX_LEN) == 0) {
            topic_index = i;
            break;
        }
    }
    if (topic_index == -1) return false; 
    TopicNode_t *topic = &g_topic_pool[topic_index];

    // 2. 精准遍历并分发
    if (topic->subscriber_count > 0) {
        for (int j = 0; j < MAX_SUBSCRIBERS_PER_TOPIC; j++) {
            if (topic->subscribers[j].is_used && topic->subscribers[j].callback != NULL) {
                // 每分发给一个订阅者，就增加一次这个 payload 的引用计数
                mps_add_ref(g_topic_mem_pool, (void*)payload); 
                
                // 触发回调
                topic->subscribers[j].callback(payload); 
            }
        }
    }
    
    // 3. 发布者完成发布，剥离自身对该 payload 的所有权
    // （如果没有任何订阅者，这步操作会直接让 ref_count 归零并回收内存，防止内存泄漏）
    mps_free(g_topic_mem_pool, (void*)payload);

    return true; 
}


void* Topic_AllocPayload(const char* topic_name) {
    if (g_topic_mem_pool == NULL || topic_name == NULL) return NULL;

    // 1. 查找到对应的 Topic，获取它的 QoS 策略
    int topic_index = -1;
    for (int i = 0; i < MAX_TOPICS; i++) {
        if (g_topic_pool[i].is_used && 
            strncmp(topic_name, g_topic_pool[i].name, TOPIC_NAME_MAX_LEN) == 0) {
            topic_index = i;
            break;
        }
    }
    
    if (topic_index == -1) return NULL; // 未注册的 Topic 直接拒绝
    TopicNode_t *topic = &g_topic_pool[topic_index];

    // 2. 尝试分配内存，并执行 QoS 拦截
    void* ptr = NULL;
    while (1) {
        // 注意：分配成功时，mps_alloc 内部已经将 ref_count 置为 1 了
        ptr = mps_alloc(g_topic_mem_pool); 
        
        if (ptr != NULL) {
            return ptr; // 分配成功，直接交还给发布者
        }

        // 分配失败（内存池被打满），触发 QoS 调度
        // 分配失败（内存池被打满），触发 QoS 调度
        switch (topic->qos) {
            case QOS_LOG:
            case QOS_SENSOR:
                // 临时降级策略：MVP 阶段内存满时一律直接丢弃。
                // 保证 CPU 控制权交还给 RTOS，防止高频传感器卡死系统。
                return NULL; 
                
            case QOS_CTRL:
                // 阻塞策略：控制指令（如 OTA）挂起当前任务 1 个 Tick
                // 注意：如果整个系统都没人去 free，这里依然可能卡很久，
                // 但至少它会让出 CPU，不会引发系统级死锁 (Deadly Embrace)。
                vTaskDelay(pdMS_TO_TICKS(1)); 
                continue; 
        }
    }
}
//目前先把QoS最简化，之后阶段再完善复杂的算法
