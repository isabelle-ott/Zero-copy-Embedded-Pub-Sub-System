/**
 * @file  mem_pool.c
 * @brief 基于位图的静态内存池实现.
 *
 * 填写每个 TODO 块。不要更改函数签名。
 * 每个阶段后在 Valgrind 下运行以捕获泄漏/无效读取。
 */

#include "mem_pool.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ================== 跨平台 RTOS 接口适配 ================== */
#ifdef TEST  // 如果是 Unity 单元测试环境
    #include <unistd.h>
    
    // 在 Linux 测试环境下，将 FreeRTOS 的 Tick 转换宏 mock 掉
    #define pdMS_TO_TICKS(ms) (ms)
    
    // 在 Linux 测试环境下，用 usleep 替代 vTaskDelay
    #define vTaskDelay(ms) usleep((ms) * 1000) 

    // 【新增】：PC 测试是单线程的，直接将临界区屏蔽掉
    // 使用 do {} while(0) 是为了保证宏展开时语法的绝对安全
    #define taskENTER_CRITICAL() do {} while(0)
    #define taskEXIT_CRITICAL()  do {} while(0)

#else       // 真实的单片机环境
    #include "FreeRTOS.h"
    #include "task.h"
#endif
/* ========================================================= */
/* -----------------------------------------------------------------------
 * 内部辅助函数 — 块索引 ↔ 指针转换
 * --------------------------------------------------------------------- */

/** 将块索引转换为指向存储区的指针。 */
static inline void *_block_ptr(MemPool_t *pool, uint32_t idx)
{
    return (void *)pool->storage[idx];
    //去 storage 这个二维内存池里，找到第 idx 个区块，把这个区块首字节的绝对物理地址拿出来，并抹除它的具体类型信息（变成万能指针），然后交还给用户
}

/** 将指针转换为块索引。如果超出范围则返回 -1。 */
static inline int32_t _ptr_to_idx(MemPool_t *pool, void *ptr)
{
    /*
     * TODO:
     *  1. 将 ptr 和 pool->storage 转换为 uintptr_t。
     *  2. 检查 ptr 是否在 [storage_start, storage_end) 范围内。
     *  3. 计算字节偏移量，除以 MPS_BLOCK_SIZE。
     *  4. 返回索引，如果超出范围则返回 -1。
     *
     * 提示: storage_end = (uintptr_t)pool->storage + sizeof(pool->storage)
     */
    if (ptr == NULL) {
        return -1;
    }
    
    uintptr_t ptr_addr = (uintptr_t)ptr;
    uintptr_t storage_start = (uintptr_t)pool->storage;
    uintptr_t storage_end = storage_start + sizeof(pool->storage);
    
    if (ptr_addr < storage_start || ptr_addr >= storage_end) {
        return -1;
    }
    
    // 检查指针是否在某个块的边界内
    uintptr_t offset = ptr_addr - storage_start;
    if (offset % MPS_BLOCK_SIZE != 0) {
        return -1;
    }
    
    uint32_t idx = offset / MPS_BLOCK_SIZE;
    if (idx >= MPS_BLOCK_COUNT) {
        return -1;
    }
    //真正的防御性编程
    
    return (int32_t)idx;
}

/* -----------------------------------------------------------------------
 * mps_init
 * --------------------------------------------------------------------- */

void mps_init(MemPool_t *pool)
{
    // 一次清零整个结构体
    memset(pool, 0, sizeof(MemPool_t));

    // 只设置 bitmap（所有块初始为空闲）
    if (MPS_BLOCK_COUNT == 32) {
        pool->bitmap = 0xFFFFFFFFu;
    } else {
        pool->bitmap = (1u << MPS_BLOCK_COUNT) - 1u;
    }
}

/* -----------------------------------------------------------------------
 * mps_alloc
 * --------------------------------------------------------------------- */

MpsStatus_t mps_alloc(MemPool_t *pool, MpsHandle_t *out)
{
    MpsStatus_t status = MPS_ERR_FULL;

    taskENTER_CRITICAL();

    if (pool->bitmap != 0u) {
        uint32_t idx = 31u - __builtin_clz(pool->bitmap);

        pool->bitmap          &= ~(1u << idx);
        pool->ref_count[idx]   = 1;
        pool->generation[idx]++;            /* 代数递增，uint8 自然溢出 */

        out->ptr        = _block_ptr(pool, idx);
        out->generation = pool->generation[idx]; /* 快照写入句柄 */
        out->idx        = (int32_t)idx;

        status = MPS_OK;
    }

    taskEXIT_CRITICAL();
    return status;
}

/* -----------------------------------------------------------------------
 * mps_free
 * --------------------------------------------------------------------- */

MpsStatus_t mps_free(MemPool_t *pool, MpsHandle_t *handle)
{
    if (!handle || !handle->ptr) {
        return MPS_ERR_INVALID;
    }

    int32_t idx = _ptr_to_idx(pool, handle->ptr);
    if (idx == -1 || idx != handle->idx) {   /* 地址 + 索引双重合法性 */
        return MPS_ERR_INVALID;
    }

    MpsStatus_t status = MPS_OK;
    taskENTER_CRITICAL();

    /* 第一道：代数校验 —— 你原版没有这道，这是唯一的新增逻辑 */
    if (pool->generation[idx] != handle->generation) {
        status = MPS_ERR_STALE;             /* ABA：块已被重新分配过 */
        goto done;
    }

    /* 第二道：bitmap（与你原版完全一致） */
    if ((pool->bitmap & (1u << idx)) == 0) {

        /* 第三道：引用计数健康校验（与你原版完全一致） */
        if (pool->ref_count[idx] == 0) {
            status = MPS_ERR_INVALID;       /* 状态撕裂 */
        } else {
            pool->ref_count[idx]--;
            if (pool->ref_count[idx] == 0) {
                pool->bitmap |= (1u << idx);
                handle->ptr   = NULL;       /* 主动置空，让野指针更早暴露 */
            }
        }
    } else {
        status = MPS_ERR_INVALID;           /* 双重释放 */
    }

done:
    taskEXIT_CRITICAL();
    return status;
}

/* -----------------------------------------------------------------------
 * mps_free_count
 * --------------------------------------------------------------------- */

uint32_t mps_free_count(const MemPool_t *pool)
{
    /*
     * TODO:
     *  返回 __builtin_popcount(pool->bitmap)。
     *  每个设置位 = 一个空闲块。
     */
    return __builtin_popcount(pool->bitmap);
}

/* -----------------------------------------------------------------------
 * mps_add_ref
 * --------------------------------------------------------------------- */
MpsStatus_t mps_add_ref(MemPool_t *pool, MpsHandle_t *handle)
{
    if (!handle || !handle->ptr) {
        return MPS_ERR_INVALID;
    }
    
    int32_t idx = _ptr_to_idx(pool, handle->ptr);
    if (idx == -1 || idx != handle->idx) {
        return MPS_ERR_INVALID;
    }
    
    MpsStatus_t status = MPS_OK;
    taskENTER_CRITICAL();
    
    //优先级 1：块状态检查（bitmap 是基础事实）
    if ((pool->bitmap & (1u << idx)) != 0) {
        // bitmap=1 表示块已释放
        status = MPS_ERR_INVALID;
    }
    //优先级 2：ABA 防护（块已分配，但是否是你这个版本？）
    else if (pool->generation[idx] != handle->generation) {
        status = MPS_ERR_STALE;
    }
    //优先级 3：引用计数溢出
    else if (pool->ref_count[idx] < 255u) {
        pool->ref_count[idx]++;
    }
    else {
        status = MPS_ERR_REF_OVERFLOW;
    }
    
    taskEXIT_CRITICAL();
    return status;
}