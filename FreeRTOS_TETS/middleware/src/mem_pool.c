#include "mem_pool.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ================== 跨平台 RTOS 接口适配 ================== */
#ifdef TEST
    #define taskENTER_CRITICAL() do {} while (0)
    #define taskEXIT_CRITICAL()  do {} while (0)

    typedef unsigned long UBaseType_t;
    #define taskENTER_CRITICAL_FROM_ISR() (0)
    #define taskEXIT_CRITICAL_FROM_ISR(x) do { (void)(x); } while (0)
#else
    #include "FreeRTOS.h"
    #include "task.h"
#endif
/* ========================================================= */

/*把索引变成可用地址*/
static inline void *_block_ptr(MemPool_t *pool, uint32_t idx)
{
    return (void *)pool->storage[idx];
}

static inline int32_t _ptr_to_idx(MemPool_t *pool, void *ptr)
{
    if (pool == NULL || ptr == NULL) {
        return -1;
    }

    uintptr_t ptr_addr = (uintptr_t)ptr;
    uintptr_t start = (uintptr_t)pool->storage;
    uintptr_t end = start + sizeof(pool->storage);

    if (ptr_addr < start || ptr_addr >= end) {
        return -1;
    }

    uintptr_t offset = ptr_addr - start;
    if ((offset % MPS_BLOCK_SIZE) != 0u) {
        return -1;
    }

    uint32_t idx = (uint32_t)(offset / MPS_BLOCK_SIZE);
    if (idx >= MPS_BLOCK_COUNT) {
        return -1;
    }

    return (int32_t)idx;
}

/*真正的释放逻辑*/
static inline MpsStatus_t _mps_free_core(MemPool_t *pool, MpsHandle_t *handle, int32_t idx)
{
    /*防止ABA问题*/
    if (pool->generation[idx] != handle->generation) {
        return MPS_ERR_STALE;
    }

    if ((pool->bitmap & (1u << (uint32_t)idx)) != 0u) {
        return MPS_ERR_INVALID; /* 双重释放 */
    }

    if (pool->ref_count[idx] == 0u) {
        return MPS_ERR_INVALID; /* 状态撕裂 */
    }

    pool->ref_count[idx]--;
    if (pool->ref_count[idx] == 0u) {
        pool->bitmap |= (1u << (uint32_t)idx);
    }

    /* 调用方释放后本句柄立即失效，尽早暴露误用 */
    handle->ptr = NULL;

    return MPS_OK;
}

static inline MpsStatus_t _mps_add_ref_core(MemPool_t *pool, MpsHandle_t *handle, int32_t idx)
{
    if (pool->generation[idx] != handle->generation) {
        return MPS_ERR_STALE;
    }

    if ((pool->bitmap & (1u << (uint32_t)idx)) != 0u) {
        return MPS_ERR_INVALID;
    }

    if (pool->ref_count[idx] == 255u) {
        return MPS_ERR_REF_OVERFLOW;
    }

    pool->ref_count[idx]++;
    return MPS_OK;
}

/* 初始化内存池，设置位图使所有块都空闲 */
void mps_init(MemPool_t *pool)
{
    if (pool == NULL) {
        return;
    }

    memset(pool, 0, sizeof(MemPool_t));

    if (MPS_BLOCK_COUNT == 32u) {
        pool->bitmap = 0xFFFFFFFFu;
    } else {
        pool->bitmap = (1u << MPS_BLOCK_COUNT) - 1u;
    }
}

MpsStatus_t mps_alloc(MemPool_t *pool, MpsHandle_t *out)
{
    if (pool == NULL || out == NULL) {
        return MPS_ERR_INVALID;
    }

    MpsStatus_t status = MPS_ERR_FULL;

    taskENTER_CRITICAL();

    /* 防护 __builtin_clz(0) 未定义行为 */
    if (pool->bitmap != 0u) {
        uint32_t idx = 31u - (uint32_t)__builtin_clz(pool->bitmap);

        pool->bitmap &= ~(1u << idx);
        pool->ref_count[idx] = 1u;
        pool->generation[idx]++; /* uint16_t 自然溢出 */

        out->ptr = _block_ptr(pool, idx);
        out->generation = (uint16_t)pool->generation[idx];
        out->idx = (int32_t)idx;

        status = MPS_OK;
    }

    taskEXIT_CRITICAL();

    return status;
}

MpsStatus_t mps_free(MemPool_t *pool, MpsHandle_t *handle)
{
    if (pool == NULL || handle == NULL || handle->ptr == NULL) {
        return MPS_ERR_INVALID;
    }

    int32_t idx = _ptr_to_idx(pool, handle->ptr);
    if (idx < 0 || idx != handle->idx) {
        return MPS_ERR_INVALID;
    }

    taskENTER_CRITICAL();
    MpsStatus_t status = _mps_free_core(pool, handle, idx);
    taskEXIT_CRITICAL();

    return status;
}

MpsStatus_t mps_free_isr(MemPool_t *pool, MpsHandle_t *handle)
{
    if (pool == NULL || handle == NULL || handle->ptr == NULL) {
        return MPS_ERR_INVALID;
    }

    int32_t idx = _ptr_to_idx(pool, handle->ptr);
    if (idx < 0 || idx != handle->idx) {
        return MPS_ERR_INVALID;
    }

    UBaseType_t saved = taskENTER_CRITICAL_FROM_ISR();
    MpsStatus_t status = _mps_free_core(pool, handle, idx);
    taskEXIT_CRITICAL_FROM_ISR(saved);

    return status;
}

uint32_t mps_free_count(const MemPool_t *pool)
{
    if (pool == NULL) {
        return 0u;
    }

    return (uint32_t)__builtin_popcount(pool->bitmap);
}

MpsStatus_t mps_add_ref(MemPool_t *pool, MpsHandle_t *handle)
{
    if (pool == NULL || handle == NULL || handle->ptr == NULL) {
        return MPS_ERR_INVALID;
    }

    int32_t idx = _ptr_to_idx(pool, handle->ptr);
    if (idx < 0 || idx != handle->idx) {
        return MPS_ERR_INVALID;
    }

    taskENTER_CRITICAL();
    MpsStatus_t status = _mps_add_ref_core(pool, handle, idx);
    taskEXIT_CRITICAL();

    return status;
}

MpsStatus_t mps_add_ref_isr(MemPool_t *pool, MpsHandle_t *handle)
{
    if (pool == NULL || handle == NULL || handle->ptr == NULL) {
        return MPS_ERR_INVALID;
    }

    int32_t idx = _ptr_to_idx(pool, handle->ptr);
    if (idx < 0 || idx != handle->idx) {
        return MPS_ERR_INVALID;
    }

    UBaseType_t saved = taskENTER_CRITICAL_FROM_ISR();
    MpsStatus_t status = _mps_add_ref_core(pool, handle, idx);
    taskEXIT_CRITICAL_FROM_ISR(saved);

    return status;
}
