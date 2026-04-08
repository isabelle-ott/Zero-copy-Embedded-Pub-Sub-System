/**
 * @file  mem_pool.h
 * @brief 基于位图的静态内存池 — O(1) 分配/释放
 *
 * 设计约束 (阶段 0, PC/Linux TDD):
 *  - 最多 32 个块 (单个 uint32_t 位图)
 *  - 块大小在编译时通过 MPS_BLOCK_SIZE 固定
 *  - 通过 __attribute__((aligned(4))) 强制 4 字节对齐
 *  - 通过显式的池满检查保护 __builtin_clz(0) 的未定义行为
 */

#ifndef MEM_POOL_H
#define MEM_POOL_H

#include <stdint.h>
#include <stddef.h>

/* -----------------------------------------------------------------------
 * 编译时配置
 * --------------------------------------------------------------------- */

/** 每个块的大小（字节）。必须是 4 的倍数。 */
#ifndef MPS_BLOCK_SIZE
#define MPS_BLOCK_SIZE  64u
#endif

/** 块的数量。最大 32（适合一个 uint32_t 位图）。 */
#ifndef MPS_BLOCK_COUNT
#define MPS_BLOCK_COUNT 16u
#endif

#if MPS_BLOCK_COUNT > 32
#error "MPS_BLOCK_COUNT 必须 <= 32（单个 uint32_t 位图）"
#endif

#if (MPS_BLOCK_SIZE % 4) != 0
#error "MPS_BLOCK_SIZE 必须是 4 的倍数（ARM 对齐）"
#endif

/* -----------------------------------------------------------------------
 * 池句柄
 * --------------------------------------------------------------------- */

typedef struct {
    /**
     * 空闲块的位图。
     * 位 N == 1  → 块 N 可用。
     * 位 N == 0  → 块 N 正在使用中。
     *
     * 初始完全设置：所有块都空闲。
     * 通过 __builtin_clz() 在强制的池满保护（位图 == 0）后找到 1 位位置。
     */
    uint32_t bitmap;

    /** 后备存储。aligned(4) 防止 ARM HardFault 在 uint32 访问上。 */
    uint8_t  storage[MPS_BLOCK_COUNT][MPS_BLOCK_SIZE]   //二维数组
                    __attribute__((aligned(4)));        //强制整个数组u在内存中按4字节边界对齐
                    //如果不四字节对齐，有可能会触发HardFault或者有不必要的周期
    uint8_t  ref_count[MPS_BLOCK_COUNT]; //用来感知这一块内存有多少人在使用

} MemPool_t;

/* -----------------------------------------------------------------------
 * 错误代码
 * --------------------------------------------------------------------- */

typedef enum {
    MPS_OK          =  0,
    MPS_ERR_FULL    = -1,   /**< 池已耗尽 — 分配返回 NULL */
    MPS_ERR_INVALID = -2,   /**< 指针不在池存储范围内 */
} MpsStatus_t;

/* -----------------------------------------------------------------------
 * 公共 API
 * --------------------------------------------------------------------- */

/**
 * @brief 初始化池。设置位图使所有块都空闲。
 * @param pool  指向 MemPool_t 的指针（调用者分配，可以是静态的）。
 */
void mps_init(MemPool_t *pool);

/**
 * @brief 在 O(1) 时间内分配一个块。
 *
 * 池满时（位图 == 0）必须返回 NULL。
 * 绝不能调用 __builtin_clz(0) — 必须先进行保护。
 *
 * @param pool  已初始化的池句柄。
 * @return      指向 MPS_BLOCK_SIZE 字节、4 字节对齐块的指针，
 *              或者如果池满则返回 NULL。
 */
void *mps_alloc(MemPool_t *pool);

/**
 * @brief 在 O(1) 时间内释放先前分配的块。
 *
 * @param pool  从中分配块的池。
 * @param ptr   由 mps_alloc() 返回的指针。
 * @return      成功时返回 MPS_OK，如果 ptr 超出范围则返回 MPS_ERR_INVALID。
 */
MpsStatus_t mps_free(MemPool_t *pool, void *ptr);

/**
 * @brief 返回剩余空闲块的数量。
 * 使用 __builtin_popcount() — 也是 O(1)。
 */
uint32_t mps_free_count(const MemPool_t *pool);

/**
 * @brief 增加指定内存块的引用计数。
 * 在零拷贝分发给多个订阅者时调用。
 * @param pool  从中分配块的池。
 * @param ptr   由 mps_alloc() 返回的有效指针。
 */
void mps_add_ref(MemPool_t *pool, void *ptr);


#endif /* MEM_POOL_H */