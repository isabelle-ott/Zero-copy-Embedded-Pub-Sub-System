#include "unity.h"
#include "mem_pool.h"
#include <stdint.h>
#include <stddef.h>

/* One pool shared across tests */
static MemPool_t pool;
static MpsHandle_t handles[MPS_BLOCK_COUNT];

/* Unity hooks */
void setUp(void)    { mps_init(&pool); }
void tearDown(void) {}

/* ===================================================================
 * A. Normal alloc / free
 * ================================================================= */

void test_alloc_returns_non_null(void)
{
    MpsHandle_t h;
    TEST_ASSERT_EQUAL(MPS_OK, mps_alloc(&pool, &h));
    TEST_ASSERT_NOT_NULL(h.ptr);
}

void test_alloc_returns_unique_pointers(void)
{
    MpsHandle_t a, b;

    mps_alloc(&pool, &a);
    mps_alloc(&pool, &b);

    TEST_ASSERT_NOT_NULL(a.ptr);
    TEST_ASSERT_NOT_NULL(b.ptr);
    TEST_ASSERT_NOT_EQUAL(a.ptr, b.ptr);
}

void test_free_allows_realloc(void)
{
    MpsHandle_t h;

    mps_alloc(&pool, &h);
    TEST_ASSERT_EQUAL(MPS_OK, mps_free(&pool, &h));

    MpsHandle_t h2;
    TEST_ASSERT_EQUAL(MPS_OK, mps_alloc(&pool, &h2));
    TEST_ASSERT_NOT_NULL(h2.ptr);
}

void test_write_and_read_block(void)
{
    MpsHandle_t h;
    mps_alloc(&pool, &h);

    uint32_t *p = (uint32_t *)h.ptr;
    *p = 0xDEADBEEFu;

    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, *p);

    mps_free(&pool, &h);
}

/* ===================================================================
 * B. Pool full
 * ================================================================= */

void test_alloc_returns_full_when_pool_full(void)
{
    for (uint32_t i = 0; i < MPS_BLOCK_COUNT; i++) {
        TEST_ASSERT_EQUAL(MPS_OK, mps_alloc(&pool, &handles[i]));
    }

    MpsHandle_t h;
    TEST_ASSERT_EQUAL(MPS_ERR_FULL, mps_alloc(&pool, &h));
}

void test_free_one_then_alloc_from_full_pool(void)
{
    for (uint32_t i = 0; i < MPS_BLOCK_COUNT; i++) {
        mps_alloc(&pool, &handles[i]);
    }

    MpsHandle_t h;
    TEST_ASSERT_EQUAL(MPS_ERR_FULL, mps_alloc(&pool, &h));

    TEST_ASSERT_EQUAL(MPS_OK, mps_free(&pool, &handles[0]));

    TEST_ASSERT_EQUAL(MPS_OK, mps_alloc(&pool, &h));
}

/* ===================================================================
 * C. Alignment
 * ================================================================= */

void test_alloc_4byte_aligned(void)
{
    for (uint32_t i = 0; i < MPS_BLOCK_COUNT; i++) {
        MpsHandle_t h;
        mps_alloc(&pool, &h);

        uintptr_t addr = (uintptr_t)h.ptr;
        TEST_ASSERT_EQUAL_UINT32(0u, addr % 4u);
    }
}

/* ===================================================================
 * D. Invalid free
 * ================================================================= */

void test_free_null_handle(void)
{
    TEST_ASSERT_EQUAL(MPS_ERR_INVALID, mps_free(&pool, NULL));
}

void test_free_invalid_idx(void)
{
    MpsHandle_t h;
    mps_alloc(&pool, &h);

    h.idx += 1;

    TEST_ASSERT_EQUAL(MPS_ERR_INVALID, mps_free(&pool, &h));
}

void test_free_invalid_generation(void)
{
    MpsHandle_t h;
    mps_alloc(&pool, &h);

    h.generation++;

    TEST_ASSERT_EQUAL(MPS_ERR_STALE, mps_free(&pool, &h));
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
    MpsHandle_t h;
    mps_alloc(&pool, &h);

    TEST_ASSERT_EQUAL_UINT32(MPS_BLOCK_COUNT - 1u, mps_free_count(&pool));
}

void test_free_count_increments_on_free(void)
{
    MpsHandle_t h;
    mps_alloc(&pool, &h);
    mps_free(&pool, &h);

    TEST_ASSERT_EQUAL_UINT32(MPS_BLOCK_COUNT, mps_free_count(&pool));
}

void test_free_count_zero_when_pool_full(void)
{
    for (uint32_t i = 0; i < MPS_BLOCK_COUNT; i++) {
        mps_alloc(&pool, &handles[i]);
    }

    TEST_ASSERT_EQUAL_UINT32(0u, mps_free_count(&pool));
}

/* ===================================================================
 * F. ABA / safety (核心)
 * ================================================================= */

void test_free_detects_stale_handle(void)
{
    MpsHandle_t h1, h2;

    mps_alloc(&pool, &h1);
    mps_free(&pool, &h1);

    mps_alloc(&pool, &h2);

    TEST_ASSERT_EQUAL(MPS_ERR_STALE, mps_free(&pool, &h1));
}

void test_double_free(void)
{
    MpsHandle_t h;

    mps_alloc(&pool, &h);
    mps_free(&pool, &h);

    TEST_ASSERT_EQUAL(MPS_ERR_INVALID, mps_free(&pool, &h));
}

void test_handle_ptr_null_after_free(void)
{
    MpsHandle_t h;

    mps_alloc(&pool, &h);
    TEST_ASSERT_NOT_NULL(h.ptr);

    mps_free(&pool, &h);

    TEST_ASSERT_NULL(h.ptr);
}

/* ===================================================================
 * Runner
 * ================================================================= */

int main(void)
{
    UNITY_BEGIN();

    /* A */
    RUN_TEST(test_alloc_returns_non_null);
    RUN_TEST(test_alloc_returns_unique_pointers);
    RUN_TEST(test_free_allows_realloc);
    RUN_TEST(test_write_and_read_block);

    /* B */
    RUN_TEST(test_alloc_returns_full_when_pool_full);
    RUN_TEST(test_free_one_then_alloc_from_full_pool);

    /* C */
    RUN_TEST(test_alloc_4byte_aligned);

    /* D */
    RUN_TEST(test_free_null_handle);
    RUN_TEST(test_free_invalid_idx);
    RUN_TEST(test_free_invalid_generation);

    /* E */
    RUN_TEST(test_free_count_starts_at_block_count);
    RUN_TEST(test_free_count_decrements_on_alloc);
    RUN_TEST(test_free_count_increments_on_free);
    RUN_TEST(test_free_count_zero_when_pool_full);

    /* F */
    RUN_TEST(test_free_detects_stale_handle);
    RUN_TEST(test_double_free);
    RUN_TEST(test_handle_ptr_null_after_free);

    return UNITY_END();
}