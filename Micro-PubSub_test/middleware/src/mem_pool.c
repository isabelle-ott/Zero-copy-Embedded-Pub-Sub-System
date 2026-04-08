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
    /*
     * TODO:
     *  设置 pool->bitmap，使得每个有效块的位都是 1（空闲）。
     *
     *  关键见解: 如果 MPS_BLOCK_COUNT == 32，你需要设置所有 32 位
     *  （0xFFFFFFFF）。如果 MPS_BLOCK_COUNT < 32，则只应设置低 N 位
     *  —— 未使用的高位必须保持 0，以便 __builtin_clz() 找到
     *  真实块，而不是虚幻的块。
     *
     *  提示: (1u << MPS_BLOCK_COUNT) - 1u 适用于 N < 32，
     *        但 N == 32 时会溢出。处理两种情况。
     */
    if (MPS_BLOCK_COUNT == 32) {
        pool->bitmap = 0xFFFFFFFFu;
    } else {
        pool->bitmap = (1u << MPS_BLOCK_COUNT) - 1u;
    }

    memset(pool->ref_count, 0, sizeof(pool->ref_count));
}

/* -----------------------------------------------------------------------
 * mps_alloc
 * --------------------------------------------------------------------- */

void *mps_alloc(MemPool_t *pool)
{
    /*1表示空闲块*/
    /* ---- 必须首先执行: 防护 __builtin_clz(0) 未定义行为 ---- */
    if (pool->bitmap == 0u) {
        return NULL;    /* 池已满 */
    }

    /*
     * TODO:
     *  1. 使用 __builtin_clz(pool->bitmap) 查找最高设置位的索引（= 空闲块）。
     *     __builtin_clz 返回前导零的数量，因此：
     *         idx = 31 - __builtin_clz(pool->bitmap)
     *
     *  2. 清除 pool->bitmap 中该位以标记块正在使用中。
     *     提示: pool->bitmap &= ~(1u << idx)
     *
     *  3. 返回该块索引的指针。
     */
    uint32_t idx = 31 - __builtin_clz(pool->bitmap);
    pool->bitmap &= ~(1u << idx);

    pool->ref_count[idx] = 1;

    return _block_ptr(pool, idx);
}

/* -----------------------------------------------------------------------
 * mps_free
 * --------------------------------------------------------------------- */

MpsStatus_t mps_free(MemPool_t *pool, void *ptr)
{
    int32_t idx = _ptr_to_idx(pool, ptr);
    if (idx == -1) {
        return MPS_ERR_INVALID;
    }
    
    // 检查块是否已在使用中（位应为 0）
    if ((pool->bitmap & (1u << idx)) == 0) {
        
        // [修改核心逻辑]：递减引用计数
        if (pool->ref_count[idx] > 0) {
            pool->ref_count[idx]--;
        }
        
        // 只有当所有订阅者和发布者都调用了 free，计数归 0 时，才真正回收物理内存块
        if (pool->ref_count[idx] == 0) {
            pool->bitmap |= (1u << idx);
        }
        
        return MPS_OK;
    } else {
        // 块已经是空闲状态，属于异常的双重释放
        return MPS_ERR_INVALID;
    }
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
void mps_add_ref(MemPool_t *pool, void *ptr)
{
    int32_t idx = _ptr_to_idx(pool, ptr);
    if (idx == -1) {
        return; // 无效指针，静默防御
    }

    // 必须确保该块确实正在被使用中 (对应位为0)
    if ((pool->bitmap & (1u << idx)) == 0) {
        // 防止 uint8_t 溢出（虽然你的系统最多 8 个订阅者，但防御要严密）
        if (pool->ref_count[idx] < 255) {
            pool->ref_count[idx]++;
        }
    }
}