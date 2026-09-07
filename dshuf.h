/*
 * dshuf - Suckless multi-key balanced shuffler
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

/* Daniel Lemire's nearly divisionless unbiased bounded random integer in [0, range) */
static inline uint32_t dshuf_rng_bounded(dshuf_rng_t *rng, uint32_t range) {
    if (range <= 1) return 0;
    uint64_t x = (uint32_t)dshuf_rng_next(rng);
    uint64_t m = x * (uint64_t)range;
    uint32_t l = (uint32_t)m;
    if (l < range) {
        uint32_t t = (uint32_t)(-(int32_t)range) % range;
        while (l < t) {
            x = (uint32_t)dshuf_rng_next(rng);
            m = x * (uint64_t)range;
            l = (uint32_t)m;
        }
    }
    return (uint32_t)(m >> 32);
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

/* Public hash function declaration */
uint32_t dshuf_hash(const void *data, size_t len);

/* Item tracked inside streaming window */
typedef struct {
    uint32_t keys[DSHUF_MAX_KEYS];
    void *user_data;
    uint32_t age;
} dshuf_item_t;

/* Open-addressing slot to track key frequency and last emission step in O(1) */
typedef struct {
    uint32_t key;
    uint32_t key_level;
    uint32_t count;
    size_t last_step;
    uint8_t occupied;
} dshuf_slot_t;

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

    /* O(1) frequency & distance lookup table */
    size_t map_cap;
    size_t map_mask;
    dshuf_slot_t *map;

    /* Global emission step counter for O(1) history distance */
    size_t step_count;
    size_t history_cap;
} dshuf_stream_t;

/* Initialize a streaming shuffler */
int dshuf_stream_init(dshuf_stream_t *s,
                      size_t window_cap,
                      size_t num_keys,
                      const float *weights,
                      float jitter,
                      uint64_t seed);

/*
 * Push an item into the streaming window.
 * Returns 1 on success.
 * Returns 0 if window is full (strictly bounded O(W) memory).
 * Returns -1 on invalid argument.
 */
int dshuf_stream_push(dshuf_stream_t *s, const uint32_t *keys, void *user_data);

/*
 * Select and pop the next best item from the window.
 * Returns 1 if an item was popped and sets *out_user_data.
 * Returns 0 if the window is empty.
 */
int dshuf_stream_pop(dshuf_stream_t *s, void **out_user_data);

/* Number of items currently queued in the window */
size_t dshuf_stream_count(const dshuf_stream_t *s);

/*
 * Drain and clear all remaining items in window, optionally calling free_fn on each user_data.
 */
void dshuf_stream_clear(dshuf_stream_t *s, void (*free_fn)(void *));

/* Free internal buffers of streaming shuffler */
void dshuf_stream_free(dshuf_stream_t *s);

/*
 * Batch shuffle:
 * Reorders `indices` array of size `n` in place.
 * If `keys` is NULL or `num_keys` == 0, performs unbiased Fisher-Yates shuffle.
 * Otherwise, performs multi-key balanced shuffle with starvation resistance.
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

#ifndef DSHUF_NO_STDLIB
#include <stdlib.h>
#include <string.h>
#endif

uint32_t dshuf_hash(const void *data, size_t len) {
    return dshuf_hash_bytes(data, len);
}

/* Internal hash slot lookup */
static dshuf_slot_t *dshuf_map_find(dshuf_stream_t *s, size_t key_level, uint32_t key_val, int create) {
    if (!s->map || s->map_cap == 0) return NULL;

    /* Combine key level and key value for slot hashing */
    uint32_t mix = key_val ^ (uint32_t)(key_level * 0x9e3779b9u);
    size_t idx = (size_t)(mix ^ (mix >> 16)) & s->map_mask;
    size_t first_empty = (size_t)-1;

    for (size_t probe = 0; probe < s->map_cap; probe++) {
        dshuf_slot_t *slot = &s->map[idx];
        if (!slot->occupied) {
            if (first_empty == (size_t)-1) first_empty = idx;
            break;
        }
        if (slot->key == key_val && slot->key_level == (uint32_t)key_level) {
            return slot;
        }
        idx = (idx + 1) & s->map_mask;
    }

    if (create && first_empty != (size_t)-1) {
        dshuf_slot_t *slot = &s->map[first_empty];
        slot->occupied = 1;
        slot->key = key_val;
        slot->key_level = (uint32_t)key_level;
        slot->count = 0;
        slot->last_step = 0;
        return slot;
    }

    return NULL;
}

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
    s->step_count = 1;
    s->history_cap = window_cap;
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

    if (s->num_keys > 0) {
        /* Allocate map with power-of-two size at least 4 * window_cap for <= 25% load factor */
        size_t cap = 16;
        while (cap < s->window_cap * s->num_keys * 4) {
            cap <<= 1;
        }
        s->map_cap = cap;
        s->map_mask = cap - 1;
        s->map = (dshuf_slot_t *)malloc(s->map_cap * sizeof(dshuf_slot_t));
        if (!s->map) {
            free(s->window);
            s->window = NULL;
            return -1;
        }
        memset(s->map, 0, s->map_cap * sizeof(dshuf_slot_t));
    } else {
        s->map = NULL;
        s->map_cap = 0;
        s->map_mask = 0;
    }

    return 0;
}

int dshuf_stream_push(dshuf_stream_t *s, const uint32_t *keys, void *user_data)
{
    if (!s || !s->window) return -1;

    /* Strictly bounded window: refuse push when full */
    if (s->window_len >= s->window_cap) {
        return 0;
    }

    dshuf_item_t *item = &s->window[s->window_len++];
    item->user_data = user_data;
    item->age = 0;

    if (keys && s->num_keys > 0) {
        memcpy(item->keys, keys, s->num_keys * sizeof(uint32_t));
        /* Increment key counts in map */
        for (size_t k = 0; k < s->num_keys; k++) {
            dshuf_slot_t *slot = dshuf_map_find(s, k, keys[k], 1);
            if (slot) slot->count++;
        }
    } else {
        memset(item->keys, 0, sizeof(item->keys));
    }

    return 1;
}

size_t dshuf_stream_count(const dshuf_stream_t *s)
{
    return s ? s->window_len : 0;
}

int dshuf_stream_pop(dshuf_stream_t *s, void **out_user_data)
{
    if (!s || s->window_len == 0) return 0;

    /* If only 1 item in window, emit directly */
    if (s->window_len == 1) {
        if (out_user_data) *out_user_data = s->window[0].user_data;
        if (s->num_keys > 0) {
            for (size_t k = 0; k < s->num_keys; k++) {
                dshuf_slot_t *slot = dshuf_map_find(s, k, s->window[0].keys[k], 0);
                if (slot) {
                    if (slot->count > 0) slot->count--;
                    slot->last_step = s->step_count;
                }
            }
        }
        s->step_count++;
        s->window_len = 0;
        return 1;
    }

    /* If no keys specified or jitter >= 1.0, direct unbiased uniform random pick */
    if (s->num_keys == 0 || s->jitter >= 1.0f) {
        uint32_t pick = dshuf_rng_bounded(&s->rng, (uint32_t)s->window_len);
        if (out_user_data) *out_user_data = s->window[pick].user_data;

        if (s->num_keys > 0) {
            for (size_t k = 0; k < s->num_keys; k++) {
                dshuf_slot_t *slot = dshuf_map_find(s, k, s->window[pick].keys[k], 0);
                if (slot) {
                    if (slot->count > 0) slot->count--;
                    slot->last_step = s->step_count;
                }
            }
        }
        s->step_count++;
        s->window[pick] = s->window[s->window_len - 1];
        s->window_len--;
        return 1;
    }

    /*
     * Evaluate each candidate in O(1) per key using map lookups.
     * Total pop complexity is O(W * num_keys).
     */
    size_t best_idx = 0;
    float best_score = 1e30f;

    for (size_t i = 0; i < s->window_len; i++) {
        dshuf_item_t *it = &s->window[i];
        float penalty = 0.0f;

        for (size_t k = 0; k < s->num_keys; k++) {
            uint32_t kval = it->keys[k];
            dshuf_slot_t *slot = dshuf_map_find(s, k, kval, 0);

            size_t count_k = slot ? slot->count : 1;
            float ideal_spacing = (float)s->window_len / (float)(count_k > 0 ? count_k : 1);
            if (ideal_spacing < 1.0f) ideal_spacing = 1.0f;

            float dev = 0.0f;
            if (slot && slot->last_step > 0) {
                size_t dist = s->step_count - slot->last_step;
                if (dist <= s->history_cap) {
                    dev = (ideal_spacing - (float)dist) / ideal_spacing;
                    if (dev < -1.0f) dev = -1.0f;
                }
            }
            penalty += s->weights[k] * dev;
        }

        /* Starvation resistance: older items get score reduction */
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

    /* Record chosen item and update map */
    if (out_user_data) *out_user_data = s->window[best_idx].user_data;

    for (size_t k = 0; k < s->num_keys; k++) {
        dshuf_slot_t *slot = dshuf_map_find(s, k, s->window[best_idx].keys[k], 0);
        if (slot) {
            if (slot->count > 0) slot->count--;
            slot->last_step = s->step_count;
        }
    }
    s->step_count++;

    /* Swap with last element and shrink */
    s->window[best_idx] = s->window[s->window_len - 1];
    s->window_len--;

    return 1;
}

void dshuf_stream_clear(dshuf_stream_t *s, void (*free_fn)(void *))
{
    if (!s) return;
    if (free_fn) {
        for (size_t i = 0; i < s->window_len; i++) {
            if (s->window[i].user_data) {
                free_fn(s->window[i].user_data);
                s->window[i].user_data = NULL;
            }
        }
    }
    s->window_len = 0;
    if (s->map) {
        memset(s->map, 0, s->map_cap * sizeof(dshuf_slot_t));
    }
}

void dshuf_stream_free(dshuf_stream_t *s)
{
    if (!s) return;
    free(s->window);
    free(s->map);
    s->window = NULL;
    s->map = NULL;
    s->window_len = 0;
    s->window_cap = 0;
    s->map_cap = 0;
    s->map_mask = 0;
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

    /* Step 1: Unbiased Fisher-Yates shuffle using Lemire's method */
    for (size_t i = n - 1; i > 0; i--) {
        size_t j = (size_t)dshuf_rng_bounded(&rng, (uint32_t)(i + 1));
        size_t tmp = indices[i];
        indices[i] = indices[j];
        indices[j] = tmp;
    }

    /* If no keys, single item, or jitter >= 1.0, unbiased Fisher-Yates is sufficient */
    if (!keys || num_keys == 0 || n <= 2 || jitter >= 1.0f) {
        return 0;
    }

    /* Step 2: Feed through balanced window buffer */
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

#endif /* DSHUF_IMPLEMENTATION */