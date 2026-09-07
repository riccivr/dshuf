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
    printf("  [PASS] Hash integrity\n");
}

static void test_batch_permutation_validity(void) {
    const size_t n = 200;
    size_t indices[200];
    uint32_t keys[200];

    /* Assign 5 clusters (40 items each) */
    for (size_t i = 0; i < n; i++) {
        keys[i] = (uint32_t)(i % 5);
    }

    int ret = dshuf_batch(indices, keys, n, 1, NULL, 0.1f, 64, 42);
    assert(ret == 0);

    /* Verify permutation completeness */
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

    /* 10 artists, 10 tracks each */
    for (size_t i = 0; i < n; i++) {
        keys[i] = (uint32_t)(i / 10);
    }

    /* Shuffle with low jitter (0.05) */
    dshuf_batch(indices, keys, n, 1, NULL, 0.05f, 64, 999);

    /* Measure minimum distance between tracks of the same artist */
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

    /* With 10 artists of 10 tracks, ideal spacing is 10.
       Under low jitter, minimum distance should easily be >= 4. */
    assert(min_dist >= 4);
    printf("  [PASS] Cluster spacing separation (min-distance: %zu, ideal: 10)\n", min_dist);
}

static void test_multi_key_clustering(void) {
    const size_t n = 120;
    size_t indices[120];
    uint32_t keys[120 * 2]; /* 2 keys: key 0 = artist, key 1 = album */

    /* 4 artists, each has 3 albums, each album has 10 tracks */
    for (size_t i = 0; i < n; i++) {
        uint32_t artist = (uint32_t)(i / 30);
        uint32_t album = (uint32_t)(i / 10);
        keys[i * 2 + 0] = artist;
        keys[i * 2 + 1] = album;
    }

    float weights[2] = {1.0f, 0.5f};
    dshuf_batch(indices, keys, n, 2, weights, 0.05f, 64, 1234);

    /* Verify that identical album tracks are never adjacent */
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

    /* 50 tracks by Artist 1 (dominant), 50 tracks by unique artists (2..51) */
    for (size_t i = 0; i < 50; i++) {
        keys[i] = 1;
    }
    for (size_t i = 50; i < 100; i++) {
        keys[i] = (uint32_t)(i + 1);
    }

    dshuf_batch(indices, keys, n, 1, NULL, 0.1f, 64, 777);

    /* Count occurrences of dominant artist in first 20 and last 20 slots */
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

static void test_streaming_api(void) {
    dshuf_stream_t stream;
    int ret = dshuf_stream_init(&stream, 32, 2, NULL, 0.2f, 54321);
    assert(ret == 0);

    /* Feed 500 items into stream, pulling after priming */
    const size_t total = 500;
    size_t pushed = 0;
    size_t popped = 0;

    while (popped < total) {
        if (pushed < total) {
            uint32_t k[2];
            k[0] = (uint32_t)(pushed % 10);  /* 10 artists */
            k[1] = (uint32_t)(pushed % 30);  /* 30 albums */
            dshuf_stream_push(&stream, k, (void *)(uintptr_t)(pushed + 1));
            pushed++;
        }

        /* Pop when primed or when input is exhausted */
        if (dshuf_stream_count(&stream) >= 32 || pushed == total) {
            void *item = NULL;
            int ok = dshuf_stream_pop(&stream, &item);
            assert(ok == 1);
            assert(item != NULL);
            popped++;
        }
    }

    assert(pushed == total);
    assert(popped == total);
    assert(dshuf_stream_count(&stream) == 0);

    dshuf_stream_free(&stream);
    printf("  [PASS] Streaming API lifecycle and drain\n");
}

static void test_edge_cases(void) {
    size_t idx[1] = {0};
    uint32_t k[1] = {123};
    assert(dshuf_batch(NULL, NULL, 0, 0, NULL, 0.0f, 0, 0) == 0);
    assert(dshuf_batch(idx, k, 1, 1, NULL, 0.0f, 0, 0) == 0);
    assert(idx[0] == 0);
    printf("  [PASS] Edge cases (n=0, n=1, null buffers)\n");
}

static void test_performance_benchmark(void) {
    const size_t n = 50000;
    size_t *indices = (size_t *)malloc(n * sizeof(size_t));
    uint32_t *keys = (uint32_t *)malloc(n * 2 * sizeof(uint32_t));
    assert(indices && keys);

    for (size_t i = 0; i < n; i++) {
        keys[i * 2 + 0] = (uint32_t)(i % 500);  /* 500 artists */
        keys[i * 2 + 1] = (uint32_t)(i % 2500); /* 2500 albums */
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
    test_streaming_api();
    test_edge_cases();
    test_performance_benchmark();
    printf("All unit tests passed successfully!\n");
    return 0;
}