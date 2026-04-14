#include "unity.h"
#include "mem_pool.h"

#include <stdint.h>

static MemPool_t pool;
static MpsHandle_t handles[MPS_BLOCK_COUNT];

void setUp(void)
{
    mps_init(&pool);
}

void tearDown(void)
{
}

void test_alloc_and_free_basic(void)
{
    MpsHandle_t h = {0};

    TEST_ASSERT_EQUAL(MPS_OK, mps_alloc(&pool, &h));
    TEST_ASSERT_NOT_NULL(h.ptr);
    TEST_ASSERT_TRUE(h.idx >= 0);

    TEST_ASSERT_EQUAL(MPS_OK, mps_free(&pool, &h));
    TEST_ASSERT_NULL(h.ptr);
}

void test_alloc_unique_pointers(void)
{
    MpsHandle_t a = {0};
    MpsHandle_t b = {0};

    TEST_ASSERT_EQUAL(MPS_OK, mps_alloc(&pool, &a));
    TEST_ASSERT_EQUAL(MPS_OK, mps_alloc(&pool, &b));

    TEST_ASSERT_NOT_EQUAL(a.ptr, b.ptr);
}

void test_alloc_returns_full_when_pool_full(void)
{
    for (uint32_t i = 0; i < MPS_BLOCK_COUNT; i++) {
        TEST_ASSERT_EQUAL(MPS_OK, mps_alloc(&pool, &handles[i]));
    }

    MpsHandle_t h = {0};
    TEST_ASSERT_EQUAL(MPS_ERR_FULL, mps_alloc(&pool, &h));
}

void test_free_count_tracks_alloc_and_free(void)
{
    TEST_ASSERT_EQUAL_UINT32(MPS_BLOCK_COUNT, mps_free_count(&pool));

    MpsHandle_t h = {0};
    TEST_ASSERT_EQUAL(MPS_OK, mps_alloc(&pool, &h));
    TEST_ASSERT_EQUAL_UINT32(MPS_BLOCK_COUNT - 1u, mps_free_count(&pool));

    TEST_ASSERT_EQUAL(MPS_OK, mps_free(&pool, &h));
    TEST_ASSERT_EQUAL_UINT32(MPS_BLOCK_COUNT, mps_free_count(&pool));
}

void test_alloc_is_4byte_aligned(void)
{
    for (uint32_t i = 0; i < MPS_BLOCK_COUNT; i++) {
        MpsHandle_t h = {0};
        TEST_ASSERT_EQUAL(MPS_OK, mps_alloc(&pool, &h));
        TEST_ASSERT_EQUAL_UINT32(0u, ((uintptr_t)h.ptr) % 4u);
    }
}

void test_free_rejects_invalid_handle_or_index(void)
{
    TEST_ASSERT_EQUAL(MPS_ERR_INVALID, mps_free(&pool, NULL));

    MpsHandle_t h = {0};
    TEST_ASSERT_EQUAL(MPS_OK, mps_alloc(&pool, &h));

    h.idx += 1;
    TEST_ASSERT_EQUAL(MPS_ERR_INVALID, mps_free(&pool, &h));
}

void test_generation_detects_stale_handle(void)
{
    MpsHandle_t old_h = {0};
    MpsHandle_t stale_h = {0};
    MpsHandle_t new_h = {0};

    TEST_ASSERT_EQUAL(MPS_OK, mps_alloc(&pool, &old_h));
    stale_h = old_h; /* 保存释放前快照，避免 mps_free 将 ptr 置 NULL */

    TEST_ASSERT_EQUAL(MPS_OK, mps_free(&pool, &old_h));
    TEST_ASSERT_NULL(old_h.ptr);

    TEST_ASSERT_EQUAL(MPS_OK, mps_alloc(&pool, &new_h));

    TEST_ASSERT_EQUAL(MPS_ERR_STALE, mps_free(&pool, &stale_h));
}

void test_add_ref_and_free_reference_counting(void)
{
    MpsHandle_t owner = {0};
    TEST_ASSERT_EQUAL(MPS_OK, mps_alloc(&pool, &owner));

    MpsHandle_t sub1 = owner;
    MpsHandle_t sub2 = owner;

    TEST_ASSERT_EQUAL(MPS_OK, mps_add_ref(&pool, &sub1));
    TEST_ASSERT_EQUAL(MPS_OK, mps_add_ref(&pool, &sub2));

    uint32_t free_before_release = mps_free_count(&pool);
    TEST_ASSERT_EQUAL(MPS_BLOCK_COUNT - 1u, free_before_release);

    TEST_ASSERT_EQUAL(MPS_OK, mps_free(&pool, &sub1));
    TEST_ASSERT_EQUAL(MPS_BLOCK_COUNT - 1u, mps_free_count(&pool));

    TEST_ASSERT_EQUAL(MPS_OK, mps_free(&pool, &sub2));
    TEST_ASSERT_EQUAL(MPS_BLOCK_COUNT - 1u, mps_free_count(&pool));

    TEST_ASSERT_EQUAL(MPS_OK, mps_free(&pool, &owner));
    TEST_ASSERT_EQUAL(MPS_BLOCK_COUNT, mps_free_count(&pool));
}

void test_add_ref_overflow_returns_error(void)
{
    MpsHandle_t h = {0};
    TEST_ASSERT_EQUAL(MPS_OK, mps_alloc(&pool, &h));

    for (uint32_t i = 0; i < 254u; i++) {
        TEST_ASSERT_EQUAL(MPS_OK, mps_add_ref(&pool, &h));
    }

    TEST_ASSERT_EQUAL(MPS_ERR_REF_OVERFLOW, mps_add_ref(&pool, &h));
}

void test_add_ref_rejects_stale_generation(void)
{
    MpsHandle_t h = {0};
    TEST_ASSERT_EQUAL(MPS_OK, mps_alloc(&pool, &h));

    h.generation++;
    TEST_ASSERT_EQUAL(MPS_ERR_STALE, mps_add_ref(&pool, &h));
}

void test_isr_variants_work(void)
{
    MpsHandle_t owner = {0};
    TEST_ASSERT_EQUAL(MPS_OK, mps_alloc(&pool, &owner));

    MpsHandle_t sub = owner;

    TEST_ASSERT_EQUAL(MPS_OK, mps_add_ref_isr(&pool, &sub));
    TEST_ASSERT_EQUAL(MPS_OK, mps_free_isr(&pool, &sub));

    /* sub 是“最后一个释放该句柄”的副本，因此 ptr 会被置 NULL */
    TEST_ASSERT_NULL(sub.ptr);

    /* owner 仍然持有自己的副本，应可正常释放 */
    TEST_ASSERT_NOT_NULL(owner.ptr);
    TEST_ASSERT_EQUAL(MPS_OK, mps_free_isr(&pool, &owner));
    TEST_ASSERT_NULL(owner.ptr);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_alloc_and_free_basic);
    RUN_TEST(test_alloc_unique_pointers);
    RUN_TEST(test_alloc_returns_full_when_pool_full);
    RUN_TEST(test_free_count_tracks_alloc_and_free);
    RUN_TEST(test_alloc_is_4byte_aligned);
    RUN_TEST(test_free_rejects_invalid_handle_or_index);
    RUN_TEST(test_generation_detects_stale_handle);
    RUN_TEST(test_add_ref_and_free_reference_counting);
    RUN_TEST(test_add_ref_overflow_returns_error);
    RUN_TEST(test_add_ref_rejects_stale_generation);
    RUN_TEST(test_isr_variants_work);

    return UNITY_END();
}
