dshuf
=====
`dshuf` is a suckless, POSIX C99 multi-key balanced shuffler. It spaces out items
that share attributes (such as artist, album, genre, or arbitrary tags) using
a greedy windowed penalty heuristic to prevent clustering and immediate repeats
in music playlists and Unix pipelines.

It runs in standard shell pipelines without external dependencies, supports
infinite streaming with strictly bounded $O(W)$ memory, and can be embedded
directly into C, C++, Python, and TypeScript/WASM projects.

How it Works
------------
Uniform random shuffle (Fisher-Yates) frequently places tracks by the same artist
adjacent to each other. In a 100-track playlist where an artist has 10 tracks,
Poisson clumping makes back-to-back repeats likely, creating the impression of a
broken shuffle.

Streaming platforms often counter this with recommendation engines and telemetry
that repeat familiar songs while starving the rest of the library.

`dshuf` implements a transparent, attribute-aware penalty heuristic:

1. **Target Interval:** For a window of size $W$ where an attribute appears $c$ times,
   the target spacing is $S = W / c$.
2. **Signed Spacing Deviation:** As candidate items are evaluated, their distance $d$
   from their previous emission is compared against target spacing $S$:
   $$\text{deviation} = \frac{S - d}{S}$$
   - $d < S$: Positive penalty (item is premature).
   - $d = S$: Zero penalty (on schedule).
   - $d > S$: Negative penalty / urgency bonus (item is overdue).
3. **Multi-Key Weighting:** Multiple keys (e.g. artist with weight 1.0, album with
   weight 0.5) combine into a single penalty score:
   $$\text{penalty} = \sum_{k=0}^{K-1} w_k \cdot \text{deviation}_k$$
4. **Starvation Resistance & Jitter:** An age discount prevents dominant artists from
   blocking rare tracks. A tunable jitter factor $\alpha \in [0.0, 1.0]$ introduces
   random noise. At $\alpha = 1.0$, the penalty calculation is bypassed entirely
   for an unbiased uniform random shuffle.

Features
--------
* Strict C99 and standard POSIX libc headers only.
* Single-header library (`dshuf.h`): drop into any C or C++ project.
* Multi-key hierarchical clustering (`-k field[:weight]`).
* Infinite streaming mode (`-S`): runs over Unix pipes with strictly bounded $O(W)$ memory.
* Bounded sliding window with age-discount starvation resistance.
* Unbiased PRNG using SplitMix64 and Lemire's bounded random method (no modulo bias).
* Real NUL-delimited stream support (`-z`).
* Drop-in compatibility with Unix pipelines (`cut`, `sort`, `shuf`).
* Zero-dependency bindings for modern C++, Python (ctypes), and TypeScript / WASM.

Hash Considerations
-------------------
Keys are hashed to 32-bit unsigned integers using FNV-1a. For typical playlist
sizes (thousands to tens of thousands of tracks), collision probability is negligible.
For datasets with over 65,000 distinct attribute values, hash collisions may treat
distinct attributes as the same cluster.

The streaming key map is sized \(O(WK)\) and reclaims stale slots only along a
key's own probe sequence. Stealing an unrelated slot would hide keys from later
lookups. Long streams of unique keys stay within the advertised memory bound.

CLI Usage
---------
```sh
# Shuffle music playlist by artist (column 1, tab-delimited)
dshuf -k 1 playlist.tsv

# Multi-key: cluster by artist (col 1, weight 1.0) and album (col 2, weight 0.5)
dshuf -k 1:1.0 -k 2:0.5 playlist.tsv

# Infinite stream over Unix pipe with bounded memory buffer (window = 128)
cat stream.tsv | dshuf -k 1 -S -w 128

# Control randomness: 0.0 (strictly balanced) to 1.0 (unbiased uniform random)
dshuf -k 1 -j 0.05 playlist.tsv

# Deterministic shuffle using an explicit seed
dshuf -k 1 -s 1337 playlist.tsv

# NUL-delimited input and output (compatible with find -print0 / sort -z)
find music/ -type f -print0 | dshuf -z

# Custom field delimiter (comma-separated)
dshuf -d ',' -k 2 tracks.csv

# Output top 50 items
dshuf -k 1 -n 50 playlist.tsv
```

C API (`dshuf.h`)
-----------------
`dshuf.h` is an `stb`-style single-header library. In exactly one `.c` or `.cpp` file:
```c
#define DSHUF_IMPLEMENTATION
#include "dshuf.h"
```

### Batch Shuffle
```c
size_t n = 100;
size_t indices[100];
uint32_t keys[100]; // FNV-1a hashes or direct integer IDs

// Shuffles indices in place with cluster balancing
dshuf_batch(indices, keys, n, 1, NULL, 0.20f, 0, 42);
```

### Streaming State Machine
```c
dshuf_stream_t stream;
dshuf_stream_init(&stream, 256, 1, NULL, 0.20f, 42);

// Push items (returns 1 on success, 0 if window is full)
uint32_t artist_key = dshuf_hash_str("Radiohead");
dshuf_stream_push(&stream, &artist_key, my_track_pointer);

// Pop the next best item
void *item = NULL;
if (dshuf_stream_pop(&stream, &item)) {
    // Process item
}

// Clear remaining user data on teardown
dshuf_stream_clear(&stream, free);
dshuf_stream_free(&stream);
```

C++ Wrapper (`dshuf.hpp`)
-------------------------
Header-only modern C++11/14/17/20 wrapper:
```cpp
#include "bindings/cpp/dshuf.hpp"

std::vector<Track> tracks = get_tracks();

// In-place balanced shuffle by artist
dshuf::shuffle(tracks.begin(), tracks.end(), [](const Track &t) {
    return t.artist;
});

// Multi-key shuffle by artist and album
dshuf::shuffle_multi(tracks.begin(), tracks.end(), 2, [](const Track &t) {
    return std::vector<uint32_t>{dshuf::to_key(t.artist), dshuf::to_key(t.album)};
}, {1.0f, 0.5f});
```

Python Bindings (`dshuf.py`)
----------------------------
Zero third-party dependencies:
```python
import dshuf

tracks = [
    {"artist": "Radiohead", "album": "OK Computer", "title": "Airbag"},
    {"artist": "Beatles", "album": "Abbey Road", "title": "Come Together"},
    ...
]

# Single-key shuffle
shuffled = dshuf.shuffle(tracks, key=lambda t: t["artist"], jitter=0.1)

# Multi-key shuffle
shuffled = dshuf.shuffle(tracks, keys=lambda t: (t["artist"], t["album"]))

# Lazy generator stream with bounded memory
for track in dshuf.stream(large_track_generator(), key=lambda t: t["artist"], window_size=128):
    play(track)
```

Build and Install
-----------------
```sh
# Build CLI tool and shared library
make

# Run all test suites (C, CLI, C++, Python)
make test-all

# Run address and undefined behavior sanitizers
make sanitize

# Install to /usr/local
sudo make install
```

License
-------
MIT License. See [LICENSE](LICENSE) for details.