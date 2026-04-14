#ifndef TOPIC_TREE_H
#define TOPIC_TREE_H

#include "mem_pool.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef Topic_Lock
    #define Topic_Lock(topic)   do {} while(0)
#endif

#ifndef Topic_Unlock
    #define Topic_Unlock(topic) do {} while(0)
#endif

/* ================== 跨平台 OS 抽象层 (OSAL) ================== */
#ifdef TEST  // Unity 单元测试环境 (Linux/PC)
    // 模拟 FreeRTOS 的数据类型
    typedef void* MpsQueue_t;       // 用万能指针模拟队列句柄
    typedef long  BaseType_t;
    #define MPS_PASS 1              // 模拟 pdPASS

    // 声明供测试使用的模拟队列发送函数 (在 test 文件中实现)
    int mps_os_queue_send(MpsQueue_t q, void* item, uint32_t timeout);
    int mps_os_queue_send_isr(MpsQueue_t q, void* item, BaseType_t* woken);
    
    // 模拟中断锁
    #define Mps_EnterCriticalFromISR()  (0)
    #define Mps_ExitCriticalFromISR(x)  do { (void)(x); } while(0)

#else       // 真实的单片机环境
    #include "FreeRTOS.h"
    #include "queue.h"
    #include "task.h"

    typedef QueueHandle_t MpsQueue_t;
    #define MPS_PASS pdPASS

    // 直接映射为 FreeRTOS API
    #define mps_os_queue_send(q, item, timeout)      xQueueSend(q, item, timeout)
    #define mps_os_queue_send_isr(q, item, woken)    xQueueSendFromISR(q, item, woken)
    #define Mps_EnterCriticalFromISR()               taskENTER_CRITICAL_FROM_ISR()
    #define Mps_ExitCriticalFromISR(x)               taskEXIT_CRITICAL_FROM_ISR(x)
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
/*实际上一个订阅者只是一个节点，发布的时候不是直接调用订阅者回调，而是把payload句柄投递到队列*/
typedef struct {
    bool is_used;
    MpsQueue_t queue;  // 订阅任务对应的消息队列
} SubscriberNode_t;

/* Topic 节点结构 */
typedef struct {
    bool is_used;
    char name[TOPIC_NAME_MAX_LEN];
    QoS_Level_t qos;
    
    // 静态订阅者列表
    SubscriberNode_t subscribers[MAX_SUBSCRIBERS_PER_TOPIC];

    // TODO: 如果需要在 Topic 之间建立静态链表或树，在此添加指针/索引
} TopicNode_t;

/* 核心 API 声明 */
void TopicTree_Init(MemPool_t *pool_ptr);
bool Topic_Subscribe(const char* topic_name, MpsQueue_t queue);
bool Topic_Publish(const char* topic_name, MpsHandle_t* payload);
bool Topic_PublishFromISR(const char* topic_name, MpsHandle_t* payload, BaseType_t *pxHigherPriorityTaskWoken);
bool Topic_Register(const char* topic_name, QoS_Level_t qos);

MpsStatus_t Topic_AllocPayload(const char* topic_name, MpsHandle_t *out);

#endif // TOPIC_TREE_H