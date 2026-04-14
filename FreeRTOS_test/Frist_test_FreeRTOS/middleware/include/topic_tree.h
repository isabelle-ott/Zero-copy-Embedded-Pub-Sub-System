#ifndef TOPIC_TREE_H
#define TOPIC_TREE_H

#include "mem_pool.h"

#include <stdint.h>
#include <stdbool.h>

#ifndef Topic_Lock
    #define Topic_Lock(topic)   do {} while(0)
#endif

#ifndef Topic_Unlock
    #define Topic_Unlock(topic) do {} while(0)
#endif

/* 系统级容量限制（体现内存可预测性） */
#define MAX_TOPICS 16
#define MAX_SUBSCRIBERS_PER_TOPIC 8
#define TOPIC_NAME_MAX_LEN 32
/* 控制流申请内存的最大阻塞超时时间 (Ticks) */
#define MAX_CTRL_ALLOC_RETRY 10

/* QoS 等级枚举：完全对应你的设计需求 */
typedef enum {
    QOS_LOG = 0,    // 内存满时直接丢弃 [cite: 11]
    QOS_SENSOR,     // 内存满时覆盖最旧区块 [cite: 11]
    QOS_CTRL        // 内存满时阻塞发送方 [cite: 11]
} QoS_Level_t;

/* 订阅者回调函数签名，基于零拷贝设计，直接传递内存池区块指针 [cite: 25] */
typedef void (*TopicCallback_t)(MpsHandle_t* payload);

/* 订阅者节点（静态链表元素） */
typedef struct {
    bool is_used;
    TopicCallback_t callback;
    // TODO: 如果你需要维护链表关系，可以在此添加 next 索引 (uint8_t next_idx)
} SubscriberNode_t;

/* Topic 节点结构 */
typedef struct {
    bool is_used;
    char name[TOPIC_NAME_MAX_LEN];
    QoS_Level_t qos;
    
    // 静态订阅者列表
    SubscriberNode_t subscribers[MAX_SUBSCRIBERS_PER_TOPIC];
    uint8_t subscriber_count;
    
    // TODO: 如果需要在 Topic 之间建立静态链表或树，在此添加指针/索引
} TopicNode_t;

/* 核心 API 声明 */
void TopicTree_Init(MemPool_t *pool_ptr);
bool Topic_Register(const char* topic_name, QoS_Level_t qos);
bool Topic_Subscribe(const char* topic_name, TopicCallback_t callback);
bool Topic_Publish(const char* topic_name, MpsHandle_t* payload);

// 将外部实例化好的内存池指针传入路由树


// 新增：向指定 Topic 申请 Payload 内存的 API
MpsStatus_t Topic_AllocPayload(const char* topic_name, MpsHandle_t *out);

#endif // TOPIC_TREE_H