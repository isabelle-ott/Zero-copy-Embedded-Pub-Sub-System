/**
 * @file  test_mem_pool.c
 * @brief Unity test suite for mem_pool.
 *
 * Test groups:
 *  A. Normal alloc/free
 *  B. Pool-full edge case (the __builtin_clz(0) guard)
 *  C. 4-byte alignment
 *  D. Invalid-pointer free
 *  E. Free-count accounting
 *
 * Build (Linux, Unity as sibling directory):
 *   gcc -Wall -O2 \
 *       mem_pool.c test_mem_pool.c \
 *       Unity/src/unity.c -IUnity/src \
 *       -o test_mem_pool && ./test_mem_pool
 *
 * Then verify with Valgrind:
 *   valgrind --leak-check=full ./test_mem_pool
 */

#include "unity.h"
#include "mem_pool.h"
#include <stdint.h>
#include <stddef.h>

/* One pool shared across tests — re-initialised in setUp(). */
static MemPool_t pool;

/* Unity mandatory hooks */
void setUp(void)    { mps_init(&pool); }
void tearDown(void) { /* nothing — storage is static */ }

/* ===================================================================
 * A. Normal alloc / free
 * ================================================================= */

void test_alloc_returns_non_null(void)
{
    void *ptr = mps_alloc(&pool);
    TEST_ASSERT_NOT_NULL(ptr);
}

void test_alloc_returns_unique_pointers(void)
{
    void *a = mps_alloc(&pool);
    void *b = mps_alloc(&pool);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_EQUAL(a, b);
}

void test_free_allows_realloc(void)
{
    void *a = mps_alloc(&pool);
    TEST_ASSERT_NOT_NULL(a);

    MpsStatus_t s = mps_free(&pool, a);
    TEST_ASSERT_EQUAL_INT(MPS_OK, s);

    /* After freeing, alloc must succeed again */
    void *b = mps_alloc(&pool);
    TEST_ASSERT_NOT_NULL(b);
}

void test_write_and_read_block(void)
{
    uint32_t *ptr = (uint32_t *)mps_alloc(&pool);
    TEST_ASSERT_NOT_NULL(ptr);

    *ptr = 0xDEADBEEFu;
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, *ptr);

    mps_free(&pool, ptr);
}

/* ===================================================================
 * B. Pool-full / __builtin_clz(0) guard
 *
 * This is the critical edge-case from the project spec.
 * When every block is in use, bitmap == 0.
 * Calling __builtin_clz(0) is undefined behaviour — especially
 * nasty under -O2/-O3.  mps_alloc() MUST return NULL before
 * ever reaching that call.
 * ================================================================= */

void test_alloc_returns_null_when_pool_full(void)
{
    /* Drain every block */
    for (uint32_t i = 0; i < MPS_BLOCK_COUNT; i++) {
        void *p = mps_alloc(&pool);
        /* All MPS_BLOCK_COUNT allocs must succeed */
        TEST_ASSERT_NOT_NULL_MESSAGE(p, "Alloc failed before pool was full");
    }

    /* Pool is now full — bitmap == 0 */
    /* This call MUST NOT crash / invoke UB, and MUST return NULL */
    void *overflow = mps_alloc(&pool);
    TEST_ASSERT_NULL_MESSAGE(overflow,
        "__builtin_clz(0) guard failed: expected NULL on full pool");
}

void test_alloc_null_again_after_pool_full(void)
{
    /* Exhaust pool */
    for (uint32_t i = 0; i < MPS_BLOCK_COUNT; i++) {
        mps_alloc(&pool);
    }
    /* Call twice to be sure the guard is idempotent */
    TEST_ASSERT_NULL(mps_alloc(&pool));
    TEST_ASSERT_NULL(mps_alloc(&pool));
}

void test_free_one_then_alloc_from_full_pool(void)
{
    void *saved = NULL;

    for (uint32_t i = 0; i < MPS_BLOCK_COUNT; i++) {
        void *p = mps_alloc(&pool);
        TEST_ASSERT_NOT_NULL(p);
        if (i == 0) saved = p;
    }

    /* Pool full → overflow must be NULL */
    TEST_ASSERT_NULL(mps_alloc(&pool));

    /* Free one block → one more alloc must succeed */
    TEST_ASSERT_EQUAL_INT(MPS_OK, mps_free(&pool, saved));
    TEST_ASSERT_NOT_NULL(mps_alloc(&pool));
}

/* ===================================================================
 * C. 4-byte alignment
 *
 * ARM Cortex-M raises HardFault on unaligned uint32_t / uint16_t
 * access. Verify every returned pointer is 4-byte aligned.
 * ================================================================= */

void test_alloc_4byte_aligned(void)
{
    for (uint32_t i = 0; i < MPS_BLOCK_COUNT; i++) {
        void *p = mps_alloc(&pool);
        TEST_ASSERT_NOT_NULL(p);
        uintptr_t addr = (uintptr_t)p;
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, addr % 4u,
            "Block is not 4-byte aligned — will HardFault on Cortex-M");
    }
}

/* ===================================================================
 * D. Invalid-pointer free
 * ================================================================= */

void test_free_null_returns_invalid(void)
{
    MpsStatus_t s = mps_free(&pool, NULL);
    TEST_ASSERT_EQUAL_INT(MPS_ERR_INVALID, s);
}

void test_free_out_of_range_ptr_returns_invalid(void)
{
    uint8_t external[MPS_BLOCK_SIZE] __attribute__((aligned(4)));
    MpsStatus_t s = mps_free(&pool, external);
    TEST_ASSERT_EQUAL_INT(MPS_ERR_INVALID, s);
}

void test_free_mid_block_ptr_returns_invalid(void)
{
    /*
     * Freeing a pointer that is inside a block but not at its start
     * should be caught. This validates the range + alignment check
     * in _ptr_to_idx().
     *
     * Note: this test only works if your _ptr_to_idx() checks that
     * (offset % MPS_BLOCK_SIZE == 0). Implement that check and this
     * test becomes a regression guard.
     */
    void *p = mps_alloc(&pool);
    TEST_ASSERT_NOT_NULL(p);

    void *mid = (uint8_t *)p + 1;   /* deliberately misaligned within block */
    MpsStatus_t s = mps_free(&pool, mid);
    TEST_ASSERT_EQUAL_INT(MPS_ERR_INVALID, s);

    /* Clean up the legitimately allocated block */
    mps_free(&pool, p);
}

/* ===================================================================
 * E. Free-count accounting
 * ================================================================= */

void test_free_count_starts_at_block_count(void)
{
    TEST_ASSERT_EQUAL_UINT32(MPS_BLOCK_COUNT, mps_free_count(&pool));
}

void test_free_count_decrements_on_alloc(void)
{
    mps_alloc(&pool);
    TEST_ASSERT_EQUAL_UINT32(MPS_BLOCK_COUNT - 1u, mps_free_count(&pool));
}

void test_free_count_increments_on_free(void)
{
    void *p = mps_alloc(&pool);
    mps_free(&pool, p);
    TEST_ASSERT_EQUAL_UINT32(MPS_BLOCK_COUNT, mps_free_count(&pool));
}

void test_free_count_zero_when_pool_full(void)
{
    for (uint32_t i = 0; i < MPS_BLOCK_COUNT; i++) {
        mps_alloc(&pool);
    }
    TEST_ASSERT_EQUAL_UINT32(0u, mps_free_count(&pool));
}

/* ===================================================================
 * Runner
 * ================================================================= */

int main(void)
{
    UNITY_BEGIN();

    /* A. Normal alloc/free */
    RUN_TEST(test_alloc_returns_non_null);
    RUN_TEST(test_alloc_returns_unique_pointers);
    RUN_TEST(test_free_allows_realloc);
    RUN_TEST(test_write_and_read_block);

    /* B. Pool-full / __builtin_clz(0) guard — THE critical tests */
    RUN_TEST(test_alloc_returns_null_when_pool_full);
    RUN_TEST(test_alloc_null_again_after_pool_full);
    RUN_TEST(test_free_one_then_alloc_from_full_pool);

    /* C. 4-byte alignment */
    RUN_TEST(test_alloc_4byte_aligned);

    /* D. Invalid-pointer free */
    RUN_TEST(test_free_null_returns_invalid);
    RUN_TEST(test_free_out_of_range_ptr_returns_invalid);
    RUN_TEST(test_free_mid_block_ptr_returns_invalid);

    /* E. Free-count accounting */
    RUN_TEST(test_free_count_starts_at_block_count);
    RUN_TEST(test_free_count_decrements_on_alloc);
    RUN_TEST(test_free_count_increments_on_free);
    RUN_TEST(test_free_count_zero_when_pool_full);

    return UNITY_END();
}