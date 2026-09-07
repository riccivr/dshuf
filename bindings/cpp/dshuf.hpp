/*
 * dshuf C++ wrapper (dshuf.hpp)
 * Header-only C++11/14/17/20 bindings for dshuf
 */

#ifndef DSHUF_HPP
#define DSHUF_HPP

#include "../../dshuf.h"
#include <vector>
#include <string>
#include <type_traits>
#include <functional>
#include <algorithm>
#include <memory>

namespace dshuf {

inline uint32_t to_key(const std::string &s) {
    return dshuf_hash_str(s.c_str());
}

inline uint32_t to_key(const char *s) {
    return dshuf_hash_str(s);
}

template <typename T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
inline uint32_t to_key(T val) {
    return static_cast<uint32_t>(val);
}

template <typename T>
class Stream {
public:
    explicit Stream(size_t window_cap = DSHUF_DEFAULT_WINDOW,
                    size_t num_keys = 1,
                    const std::vector<float> &weights = {},
                    float jitter = DSHUF_DEFAULT_JITTER,
                    uint64_t seed = 0)
        : m_num_keys(num_keys)
    {
        const float *w_ptr = weights.empty() ? nullptr : weights.data();
        if (dshuf_stream_init(&m_stream, window_cap, num_keys, w_ptr, jitter, seed) != 0) {
            m_stream.window = nullptr;
        }
    }

    ~Stream() {
        clear();
        dshuf_stream_free(&m_stream);
    }

    Stream(const Stream &) = delete;
    Stream &operator=(const Stream &) = delete;

    Stream(Stream &&other) noexcept : m_num_keys(other.m_num_keys) {
        m_stream = other.m_stream;
        other.m_stream.window = nullptr;
        other.m_stream.map = nullptr;
        other.m_stream.window_len = 0;
    }

    Stream &operator=(Stream &&other) noexcept {
        if (this != &other) {
            clear();
            dshuf_stream_free(&m_stream);
            m_num_keys = other.m_num_keys;
            m_stream = other.m_stream;
            other.m_stream.window = nullptr;
            other.m_stream.map = nullptr;
            other.m_stream.window_len = 0;
        }
        return *this;
    }

    bool push(T item, const std::vector<uint32_t> &keys) {
        if (!m_stream.window) return false;
        std::unique_ptr<T> copy(new T(std::move(item)));
        if (dshuf_stream_push(&m_stream, keys.data(), copy.get()) == 1) {
            copy.release();
            return true;
        }
        return false;
    }

    template <typename K>
    bool push(T item, K key) {
        uint32_t k = to_key(key);
        return push(std::move(item), std::vector<uint32_t>{k});
    }

    bool pop(T &out_item) {
        void *p = nullptr;
        if (dshuf_stream_pop(&m_stream, &p) && p) {
            T *val = static_cast<T *>(p);
            out_item = std::move(*val);
            delete val;
            return true;
        }
        return false;
    }

    void clear() {
        void *p = nullptr;
        while (dshuf_stream_pop(&m_stream, &p)) {
            delete static_cast<T *>(p);
        }
    }

    size_t count() const {
        return dshuf_stream_count(&m_stream);
    }

    bool empty() const {
        return count() == 0;
    }

private:
    size_t m_num_keys;
    dshuf_stream_t m_stream;
};

template <typename RandomIt, typename KeyFunc>
void shuffle(RandomIt first, RandomIt last, KeyFunc key_fn,
             float jitter = DSHUF_DEFAULT_JITTER,
             size_t window_size = 0,
             uint64_t seed = 0)
{
    size_t n = static_cast<size_t>(std::distance(first, last));
    if (n <= 1) return;

    std::vector<uint32_t> keys(n);
    size_t i = 0;
    for (auto it = first; it != last; ++it, ++i) {
        keys[i] = to_key(key_fn(*it));
    }

    std::vector<size_t> indices(n);
    dshuf_batch(indices.data(), keys.data(), n, 1, nullptr, jitter, window_size, seed);

    std::vector<typename std::iterator_traits<RandomIt>::value_type> tmp;
    tmp.reserve(n);
    for (size_t idx : indices) {
        tmp.push_back(std::move(*(first + idx)));
    }

    std::move(tmp.begin(), tmp.end(), first);
}

template <typename RandomIt, typename MultiKeyFunc>
void shuffle_multi(RandomIt first, RandomIt last, size_t num_keys,
                  MultiKeyFunc multi_key_fn,
                  const std::vector<float> &weights = {},
                  float jitter = DSHUF_DEFAULT_JITTER,
                  size_t window_size = 0,
                  uint64_t seed = 0)
{
    size_t n = static_cast<size_t>(std::distance(first, last));
    if (n <= 1) return;

    std::vector<uint32_t> keys(n * num_keys);
    size_t i = 0;
    for (auto it = first; it != last; ++it, ++i) {
        std::vector<uint32_t> item_keys = multi_key_fn(*it);
        for (size_t k = 0; k < num_keys && k < item_keys.size(); ++k) {
            keys[i * num_keys + k] = item_keys[k];
        }
    }

    const float *w_ptr = weights.empty() ? nullptr : weights.data();
    std::vector<size_t> indices(n);
    dshuf_batch(indices.data(), keys.data(), n, num_keys, w_ptr, jitter, window_size, seed);

    std::vector<typename std::iterator_traits<RandomIt>::value_type> tmp;
    tmp.reserve(n);
    for (size_t idx : indices) {
        tmp.push_back(std::move(*(first + idx)));
    }

    std::move(tmp.begin(), tmp.end(), first);
}

} // namespace dshuf

#endif // DSHUF_HPP