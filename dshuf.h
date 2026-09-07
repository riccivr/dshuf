/*
 * dshuf - Suckless multi-key low-discrepancy shuffler
 * See LICENSE file for copyright and license details.
 *
 * Single-header library. In exactly one C/C++ source file, define:
 *   #define DSHUF_IMPLEMENTATION
 * before including this header.
 */

#ifndef DSHUF_H
#define DSHUF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#define DSHUF_DEFAULT_WINDOW    256
#define DSHUF_DEFAULT_JITTER    0.20f
#define DSHUF_DEFAULT_BETA      0.50f
#define DSHUF_MAX_KEYS          16

/* PRNG state (SplitMix64) */
typedef struct {
    uint64_t state;
} dshuf_rng_t;

static inline void dshuf_rng_seed(dshuf_rng_t *rng, uint64_t seed) {
    rng->state = seed ? seed : 0x853c49e6748fea9bULL;
}

static inline uint64_t dshuf_rng_next(dshuf_rng_t *rng) {
    uint64_t z = (rng->state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static inline float dshuf_rng_float(dshuf_rng_t *rng) {
    return (float)((dshuf_rng_next(rng) >> 40) * (1.0 / 16777216.0));
}

/* FNV-1a 32-bit string hash */
static inline uint32_t dshuf_hash_bytes(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static inline uint32_t dshuf_hash_str(const char *s) {
    uint32_t h = 2166136261u;
    if (!s) return h;
    while (*s) {
        h ^= (uint8_t)(*s++);
        h *= 16777619u;
    }
    return h;
}

/* Item tracked inside streaming window */
typedef struct {
    uint32_t keys[DSHUF_MAX_KEYS];
    void *user_data;
    uint32_t age;
} dshuf_item_t;

/* Streaming shuffler context */
typedef struct {
    size_t window_cap;
    size_t window_len;
    size_t num_keys;
    float weights[DSHUF_MAX_KEYS];
    float jitter;
    float beta;
    dshuf_rng_t rng;

    dshuf_item_t *window;

    /* Emitted history ring buffer */
    size_t history_cap;
    size_t history_len;
    size_t history_head;
    uint32_t *history_keys; /* [history_cap * num_keys] */
} dshuf_stream_t;

/* Initialize a streaming shuffler */
int dshuf_stream_init(dshuf_stream_t *s,
                      size_t window_cap,
                      size_t num_keys,
                      const float *weights,
                      float jitter,
                      uint64_t seed);

/* Push an item into the streaming window. Returns 0 on success, -1 on allocation failure. */
int dshuf_stream_push(dshuf_stream_t *s, const uint32_t *keys, void *user_data);

/*
 * Select and pop the next best item from the window.
 * Returns 1 if an item was popped and sets *out_user_data.
 * Returns 0 if the window is empty.
 */
int dshuf_stream_pop(dshuf_stream_t *s, void **out_user_data);

/* Number of items currently queued in the window */
size_t dshuf_stream_count(const dshuf_stream_t *s);

/* Free internal buffers of streaming shuffler */
void dshuf_stream_free(dshuf_stream_t *s);

/*
 * Batch shuffle:
 * Reorders `indices` array of size `n` in place.
 * If `keys` is NULL or `num_keys` == 0, performs standard Fisher-Yates shuffle.
 * Otherwise, performs multi-key low-discrepancy shuffle with starvation protection.
 *
 * keys: flat array of [n * num_keys] hashes, or NULL
 * weights: array of [num_keys] floats, or NULL (defaults to 1.0, 0.5, 0.25...)
 * window_size: internal window size for distance evaluation (0 for auto: min(n, 256))
 */
int dshuf_batch(size_t *indices,
                const uint32_t *keys,
                size_t n,
                size_t num_keys,
                const float *weights,
                float jitter,
                size_t window_size,
                uint64_t seed);

#ifdef __cplusplus
}
#endif

#endif /* DSHUF_H */

#ifdef DSHUF_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>

int dshuf_stream_init(dshuf_stream_t *s,
                      size_t window_cap,
                      size_t num_keys,
                      const float *weights,
                      float jitter,
                      uint64_t seed)
{
    if (!s) return -1;
    if (window_cap == 0) window_cap = DSHUF_DEFAULT_WINDOW;
    if (num_keys > DSHUF_MAX_KEYS) num_keys = DSHUF_MAX_KEYS;

    memset(s, 0, sizeof(*s));
    s->window_cap = window_cap;
    s->window_len = 0;
    s->num_keys = num_keys;
    s->jitter = (jitter < 0.0f) ? 0.0f : ((jitter > 1.0f) ? 1.0f : jitter);
    s->beta = DSHUF_DEFAULT_BETA;
    dshuf_rng_seed(&s->rng, seed);

    for (size_t k = 0; k < num_keys; k++) {
        if (weights && weights[k] >= 0.0f) {
            s->weights[k] = weights[k];
        } else {
            /* Geometric decay for successive keys: 1.0, 0.5, 0.25 ... */
            float w = 1.0f;
            for (size_t j = 0; j < k; j++) w *= 0.5f;
            s->weights[k] = w;
        }
    }

    s->window = (dshuf_item_t *)malloc(s->window_cap * sizeof(dshuf_item_t));
    if (!s->window) return -1;

    /* History depth matches window capacity */
    s->history_cap = s->window_cap;
    s->history_len = 0;
    s->history_head = 0;
    if (s->num_keys > 0) {
        s->history_keys = (uint32_t *)malloc(s->history_cap * s->num_keys * sizeof(uint32_t));
        if (!s->history_keys) {
            free(s->window);
            s->window = NULL;
            return -1;
        }
    } else {
        s->history_keys = NULL;
    }

    return 0;
}

int dshuf_stream_push(dshuf_stream_t *s, const uint32_t *keys, void *user_data)
{
    if (!s || !s->window) return -1;

    if (s->window_len >= s->window_cap) {
        /* Expand window capacity if full */
        size_t new_cap = s->window_cap * 2;
        dshuf_item_t *new_win = (dshuf_item_t *)realloc(s->window, new_cap * sizeof(dshuf_item_t));
        if (!new_win) return -1;
        s->window = new_win;
        s->window_cap = new_cap;
    }

    dshuf_item_t *item = &s->window[s->window_len++];
    item->user_data = user_data;
    item->age = 0;
    if (keys && s->num_keys > 0) {
        memcpy(item->keys, keys, s->num_keys * sizeof(uint32_t));
    } else {
        memset(item->keys, 0, sizeof(item->keys));
    }

    return 0;
}

size_t dshuf_stream_count(const dshuf_stream_t *s)
{
    return s ? s->window_len : 0;
}

static void dshuf_record_history(dshuf_stream_t *s, const uint32_t *keys)
{
    if (!s->history_keys || s->num_keys == 0 || s->history_cap == 0) return;

    size_t idx = s->history_head;
    memcpy(&s->history_keys[idx * s->num_keys], keys, s->num_keys * sizeof(uint32_t));
    s->history_head = (idx + 1) % s->history_cap;
    if (s->history_len < s->history_cap) {
        s->history_len++;
    }
}

/*
 * Find distance (1-based) to most recent occurrence of key at level k in history.
 * Returns 0 if not found in history.
 */
static size_t dshuf_find_history_dist(const dshuf_stream_t *s, size_t key_level, uint32_t key_val)
{
    if (s->history_len == 0 || !s->history_keys) return 0;

    size_t head = s->history_head;
    for (size_t d = 1; d <= s->history_len; d++) {
        size_t slot = (head >= d) ? (head - d) : (s->history_cap + head - d);
        if (s->history_keys[slot * s->num_keys + key_level] == key_val) {
            return d;
        }
    }
    return 0;
}

int dshuf_stream_pop(dshuf_stream_t *s, void **out_user_data)
{
    if (!s || s->window_len == 0) return 0;

    /* If only 1 item in window, emit directly */
    if (s->window_len == 1) {
        if (out_user_data) *out_user_data = s->window[0].user_data;
        dshuf_record_history(s, s->window[0].keys);
        s->window_len = 0;
        return 1;
    }

    /* If no keys specified, simple uniform random pick */
    if (s->num_keys == 0) {
        size_t pick = (size_t)(dshuf_rng_next(&s->rng) % s->window_len);
        if (out_user_data) *out_user_data = s->window[pick].user_data;
        s->window[pick] = s->window[s->window_len - 1];
        s->window_len--;
        return 1;
    }

    /*
     * Compute key counts in current window to determine ideal spacing S_k.
     * S_k = window_len / count_in_window.
     */
    size_t best_idx = 0;
    float best_score = 1e30f;

    for (size_t i = 0; i < s->window_len; i++) {
        dshuf_item_t *it = &s->window[i];
        float penalty = 0.0f;

        for (size_t k = 0; k < s->num_keys; k++) {
            uint32_t kval = it->keys[k];

            /* Count occurrences of this key inside current window */
            size_t count_k = 0;
            for (size_t j = 0; j < s->window_len; j++) {
                if (s->window[j].keys[k] == kval) count_k++;
            }

            float ideal_spacing = (float)s->window_len / (float)(count_k > 0 ? count_k : 1);
            if (ideal_spacing < 1.0f) ideal_spacing = 1.0f;

            size_t dist = dshuf_find_history_dist(s, k, kval);
            float dev = 0.0f;
            if (dist > 0) {
                dev = (ideal_spacing - (float)dist) / ideal_spacing;
                if (dev < -1.0f) dev = -1.0f;
            }
            penalty += s->weights[k] * dev;
        }

        /* Starvation bonus: older items get score reduction */
        float age_bonus = s->beta * ((float)it->age / (float)(s->window_cap > 0 ? s->window_cap : 1));

        /* Noise perturbation controlled by jitter */
        float noise = dshuf_rng_float(&s->rng);
        float score = penalty - age_bonus + (s->jitter * noise);

        if (score < best_score) {
            best_score = score;
            best_idx = i;
        }
    }

    /* Age surviving items in window */
    for (size_t i = 0; i < s->window_len; i++) {
        if (i != best_idx) {
            s->window[i].age++;
        }
    }

    /* Record chosen item to history */
    if (out_user_data) *out_user_data = s->window[best_idx].user_data;
    dshuf_record_history(s, s->window[best_idx].keys);

    /* Swap with last element and shrink */
    s->window[best_idx] = s->window[s->window_len - 1];
    s->window_len--;

    return 1;
}

void dshuf_stream_free(dshuf_stream_t *s)
{
    if (!s) return;
    free(s->window);
    free(s->history_keys);
    s->window = NULL;
    s->history_keys = NULL;
    s->window_len = 0;
    s->window_cap = 0;
}

int dshuf_batch(size_t *indices,
                const uint32_t *keys,
                size_t n,
                size_t num_keys,
                const float *weights,
                float jitter,
                size_t window_size,
                uint64_t seed)
{
    if (!indices || n == 0) return 0;

    dshuf_rng_t rng;
    dshuf_rng_seed(&rng, seed);

    /* Initialize identity indices */
    for (size_t i = 0; i < n; i++) {
        indices[i] = i;
    }

    /* Step 1: Initial Fisher-Yates shuffle */
    for (size_t i = n - 1; i > 0; i--) {
        size_t j = (size_t)(dshuf_rng_next(&rng) % (i + 1));
        size_t tmp = indices[i];
        indices[i] = indices[j];
        indices[j] = tmp;
    }

    /* If no keys or single item, Fisher-Yates is sufficient */
    if (!keys || num_keys == 0 || n <= 2) {
        return 0;
    }

    /* Step 2: Feed through low-discrepancy window buffer */
    if (window_size == 0) {
        window_size = (n < DSHUF_DEFAULT_WINDOW) ? n : DSHUF_DEFAULT_WINDOW;
    }

    dshuf_stream_t stream;
    if (dshuf_stream_init(&stream, window_size, num_keys, weights, jitter, dshuf_rng_next(&rng)) != 0) {
        return -1;
    }

    size_t *result = (size_t *)malloc(n * sizeof(size_t));
    if (!result) {
        dshuf_stream_free(&stream);
        return -1;
    }

    size_t in_pos = 0;
    size_t out_pos = 0;

    /* Prime the window */
    while (in_pos < n && stream.window_len < stream.window_cap) {
        size_t orig_idx = indices[in_pos++];
        const uint32_t *item_keys = &keys[orig_idx * num_keys];
        dshuf_stream_push(&stream, item_keys, (void *)(uintptr_t)orig_idx);
    }

    /* Stream through window */
    while (out_pos < n) {
        void *p = NULL;
        if (!dshuf_stream_pop(&stream, &p)) break;
        result[out_pos++] = (size_t)(uintptr_t)p;

        if (in_pos < n) {
            size_t orig_idx = indices[in_pos++];
            const uint32_t *item_keys = &keys[orig_idx * num_keys];
            dshuf_stream_push(&stream, item_keys, (void *)(uintptr_t)orig_idx);
        }
    }

    memcpy(indices, result, n * sizeof(size_t));
    free(result);
    dshuf_stream_free(&stream);

    return 0;
}

uint32_t dshuf_hash(const void *data, size_t len) { return dshuf_hash_bytes(data, len); }

#endif /* DSHUF_IMPLEMENTATION */