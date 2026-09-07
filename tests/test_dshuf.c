#define DSHUF_IMPLEMENTATION
#include "../dshuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>

static void test_rng_determinism(void) {
    dshuf_rng_t rng1, rng2;
    dshuf_rng_seed(&rng1, 123456789ULL);
    dshuf_rng_seed(&rng2, 123456789ULL);

    for (int i = 0; i < 1000; i++) {
        uint64_t v1 = dshuf_rng_next(&rng1);
        uint64_t v2 = dshuf_rng_next(&rng2);
        assert(v1 == v2);
    }
    printf("  [PASS] PRNG determinism\n");
}

static void test_hash_integrity(void) {
    uint32_t h1 = dshuf_hash_str("Radiohead");
    uint32_t h2 = dshuf_hash_str("Radiohead");
    uint32_t h3 = dshuf_hash_str("Daft Punk");
    assert(h1 == h2);
    assert(h1 != h3);
    assert(dshuf_hash_str("") == 2166136261u);
    assert(dshuf_hash("Radiohead", 9) == h1);
    printf("  [PASS] Hash integrity\n");
}

static void test_batch_permutation_validity(void) {
    const size_t n = 200;
    size_t indices[200];
    uint32_t keys[200];

    for (size_t i = 0; i < n; i++) {
        keys[i] = (uint32_t)(i % 5);
    }

    int ret = dshuf_batch(indices, keys, n, 1, NULL, 0.1f, 64, 42);
    assert(ret == 0);

    int seen[200] = {0};
    for (size_t i = 0; i < n; i++) {
        assert(indices[i] < n);
        seen[indices[i]]++;
    }
    for (size_t i = 0; i < n; i++) {
        assert(seen[i] == 1);
    }
    printf("  [PASS] Batch permutation completeness\n");
}

static void test_cluster_spacing(void) {
    const size_t n = 100;
    size_t indices[100];
    uint32_t keys[100];

    for (size_t i = 0; i < n; i++) {
        keys[i] = (uint32_t)(i / 10);
    }

    dshuf_batch(indices, keys, n, 1, NULL, 0.05f, 64, 999);

    size_t min_dist = 9999;
    size_t last_pos[10];
    for (int a = 0; a < 10; a++) last_pos[a] = 9999;

    for (size_t pos = 0; pos < n; pos++) {
        uint32_t artist = keys[indices[pos]];
        if (last_pos[artist] != 9999) {
            size_t dist = pos - last_pos[artist];
            if (dist < min_dist) min_dist = dist;
        }
        last_pos[artist] = pos;
    }

    assert(min_dist >= 4);
    printf("  [PASS] Cluster spacing separation (min-distance: %zu, ideal: 10)\n", min_dist);
}

static void test_multi_key_clustering(void) {
    const size_t n = 120;
    size_t indices[120];
    uint32_t keys[120 * 2];

    for (size_t i = 0; i < n; i++) {
        uint32_t artist = (uint32_t)(i / 30);
        uint32_t album = (uint32_t)(i / 10);
        keys[i * 2 + 0] = artist;
        keys[i * 2 + 1] = album;
    }

    float weights[2] = {1.0f, 0.5f};
    dshuf_batch(indices, keys, n, 2, weights, 0.05f, 64, 1234);

    size_t min_album_dist = 9999;
    size_t last_album_pos[12];
    for (int a = 0; a < 12; a++) last_album_pos[a] = 9999;

    for (size_t pos = 0; pos < n; pos++) {
        size_t orig = indices[pos];
        uint32_t album = keys[orig * 2 + 1];
        if (last_album_pos[album] != 9999) {
            size_t dist = pos - last_album_pos[album];
            if (dist < min_album_dist) min_album_dist = dist;
        }
        last_album_pos[album] = pos;
    }

    assert(min_album_dist >= 4);
    printf("  [PASS] Multi-key hierarchical separation (min-album-dist: %zu)\n", min_album_dist);
}

static void test_starvation_resistance(void) {
    const size_t n = 100;
    size_t indices[100];
    uint32_t keys[100];

    for (size_t i = 0; i < 50; i++) {
        keys[i] = 1;
    }
    for (size_t i = 50; i < 100; i++) {
        keys[i] = (uint32_t)(i + 1);
    }

    dshuf_batch(indices, keys, n, 1, NULL, 0.1f, 64, 777);

    int first_20_dominant = 0;
    int last_20_dominant = 0;

    for (size_t i = 0; i < 20; i++) {
        if (keys[indices[i]] == 1) first_20_dominant++;
    }
    for (size_t i = 80; i < 100; i++) {
        if (keys[indices[i]] == 1) last_20_dominant++;
    }

    assert(first_20_dominant >= 6 && first_20_dominant <= 14);
    assert(last_20_dominant >= 6 && last_20_dominant <= 14);
    printf("  [PASS] Starvation resistance under skewed distribution (%d/20 at start, %d/20 at end)\n",
           first_20_dominant, last_20_dominant);
}

static void test_bounded_window_and_clear(void) {
    dshuf_stream_t stream;
    int ret = dshuf_stream_init(&stream, 4, 1, NULL, 0.2f, 101);
    assert(ret == 0);

    uint32_t k = 42;
    assert(dshuf_stream_push(&stream, &k, (void *)1) == 1);
    assert(dshuf_stream_push(&stream, &k, (void *)2) == 1);
    assert(dshuf_stream_push(&stream, &k, (void *)3) == 1);
    assert(dshuf_stream_push(&stream, &k, (void *)4) == 1);

    /* 5th push must be rejected due to strictly bounded window */
    assert(dshuf_stream_push(&stream, &k, (void *)5) == 0);
    assert(dshuf_stream_count(&stream) == 4);

    dshuf_stream_clear(&stream, NULL);
    assert(dshuf_stream_count(&stream) == 0);

    /* After clear, pushing is accepted again */
    assert(dshuf_stream_push(&stream, &k, (void *)10) == 1);
    assert(dshuf_stream_count(&stream) == 1);

    dshuf_stream_free(&stream);
    printf("  [PASS] Strictly bounded window overflow rejection and clear\n");
}

static void test_jitter_1_pure_random(void) {
    const size_t n = 50;
    size_t indices[50];
    uint32_t keys[50];
    for (size_t i = 0; i < n; i++) keys[i] = 1; // all same cluster!

    // At jitter 1.0, should execute pure unbiased Fisher-Yates without hanging
    int ret = dshuf_batch(indices, keys, n, 1, NULL, 1.0f, 16, 888);
    assert(ret == 0);

    int seen[50] = {0};
    for (size_t i = 0; i < n; i++) seen[indices[i]]++;
    for (size_t i = 0; i < n; i++) assert(seen[i] == 1);

    printf("  [PASS] Pure random shuffle at jitter 1.0\n");
}

static void test_map_eviction_unique_keys(void) {
    /* Window 8, thousands of unique keys: map must not lock up or drop items. */
    dshuf_stream_t stream;
    assert(dshuf_stream_init(&stream, 8, 1, NULL, 0.1f, 1) == 0);

    const size_t total = 4000;
    size_t pushed = 0, popped = 0;
    while (popped < total) {
        if (pushed < total) {
            uint32_t k = (uint32_t)(pushed + 1);
            int pr = dshuf_stream_push(&stream, &k, (void *)(uintptr_t)(pushed + 1));
            if (pr == 1) {
                pushed++;
            } else if (pr == 0) {
                void *item = NULL;
                assert(dshuf_stream_pop(&stream, &item) == 1);
                popped++;
            } else {
                assert(0 && "push failed");
            }
        } else {
            void *item = NULL;
            assert(dshuf_stream_pop(&stream, &item) == 1);
            popped++;
        }
    }
    assert(pushed == total);
    assert(popped == total);
    assert(dshuf_stream_count(&stream) == 0);
    dshuf_stream_free(&stream);
    printf("  [PASS] Map eviction under unique-key flood (%zu items)\n", total);
}

static size_t map_live_count(const dshuf_stream_t *s) {
    size_t sum = 0;
    if (!s->map) return 0;
    for (size_t i = 0; i < s->map_cap; i++) {
        if (s->map[i].occupied && s->map[i].key_level == 0) {
            sum += s->map[i].count;
        }
    }
    return sum;
}

static int map_has_duplicate_keys(const dshuf_stream_t *s) {
    if (!s->map) return 0;
    for (size_t i = 0; i < s->map_cap; i++) {
        if (!s->map[i].occupied) continue;
        for (size_t j = i + 1; j < s->map_cap; j++) {
            if (s->map[j].occupied &&
                s->map[j].key == s->map[i].key &&
                s->map[j].key_level == s->map[i].key_level) {
                return 1;
            }
        }
    }
    return 0;
}

static void test_map_invariants_after_eviction(void) {
    dshuf_stream_t stream;
    assert(dshuf_stream_init(&stream, 16, 1, NULL, 0.05f, 7) == 0);

    for (size_t i = 0; i < 2000; i++) {
        uint32_t k = (uint32_t)(i + 1);
        if (dshuf_stream_push(&stream, &k, (void *)(uintptr_t)(i + 1)) == 0) {
            void *item = NULL;
            assert(dshuf_stream_pop(&stream, &item) == 1);
            assert(dshuf_stream_push(&stream, &k, (void *)(uintptr_t)(i + 1)) == 1);
        }
        assert(map_live_count(&stream) == stream.window_len);
        assert(!map_has_duplicate_keys(&stream));
    }

    /* After unique flood, a tight cluster must still be tracked as one key. */
    dshuf_stream_clear(&stream, NULL);
    uint32_t cluster = 42;
    for (int i = 0; i < 8; i++) {
        assert(dshuf_stream_push(&stream, &cluster, (void *)(uintptr_t)(100 + i)) == 1);
    }
    assert(map_live_count(&stream) == 8);
    assert(!map_has_duplicate_keys(&stream));

    dshuf_stream_free(&stream);
    printf("  [PASS] Map counts stay consistent after eviction\n");
}

static void test_hash_collision_is_stable(void) {
    /* Same bytes must alias; distinct short strings almost never collide. */
    assert(dshuf_hash_str("Radiohead") == dshuf_hash_str("Radiohead"));
    assert(dshuf_hash_bytes("aa", 2) == dshuf_hash_bytes("aa", 2));
    assert(dshuf_hash_str("Radiohead") != dshuf_hash_str("Beatles"));
    printf("  [PASS] Hash stability and trivial non-collision\n");
}

static void test_bounded64_small_ranges(void) {
    dshuf_rng_t rng;
    dshuf_rng_seed(&rng, 99);
    for (uint64_t r = 1; r <= 17; r++) {
        for (int i = 0; i < 200; i++) {
            uint64_t v = dshuf_rng_bounded64(&rng, r);
            assert(v < r || r == 0);
        }
    }
    printf("  [PASS] bounded64 stays in range\n");
}

static void test_performance_benchmark(void) {
    const size_t n = 50000;
    size_t *indices = (size_t *)malloc(n * sizeof(size_t));
    uint32_t *keys = (uint32_t *)malloc(n * 2 * sizeof(uint32_t));
    assert(indices && keys);

    for (size_t i = 0; i < n; i++) {
        keys[i * 2 + 0] = (uint32_t)(i % 500);
        keys[i * 2 + 1] = (uint32_t)(i % 2500);
    }

    clock_t t0 = clock();
    int ret = dshuf_batch(indices, keys, n, 2, NULL, 0.2f, 128, 42);
    clock_t t1 = clock();
    assert(ret == 0);

    double elapsed = (double)(t1 - t0) / CLOCKS_PER_SEC;
    printf("  [PASS] Performance benchmark: %zu items shuffled in %.3f sec (%.0f items/sec)\n",
           n, elapsed, (double)n / (elapsed > 0.0 ? elapsed : 0.0001));

    free(indices);
    free(keys);
}

int main(void) {
    printf("Running dshuf unit tests:\n");
    test_rng_determinism();
    test_hash_integrity();
    test_batch_permutation_validity();
    test_cluster_spacing();
    test_multi_key_clustering();
    test_starvation_resistance();
    test_bounded_window_and_clear();
    test_jitter_1_pure_random();
    test_map_eviction_unique_keys();
    test_map_invariants_after_eviction();
    test_hash_collision_is_stable();
    test_bounded64_small_ranges();
    test_performance_benchmark();
    printf("All unit tests passed successfully!\n");
    return 0;
}