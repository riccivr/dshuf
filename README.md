dshuf
=====
`dshuf` is a suckless, POSIX C99 multi-key low-discrepancy shuffler. It spaces
out items that share attributes (such as artist, album, genre, or arbitrary
tags) to eliminate Poisson clumping and immediate repeats in music playlists
and streaming pipelines.

It runs in standard shell pipelines without external dependencies, supports
infinite streaming with bounded memory, and can be embedded directly into C,
C++, Python, and TypeScript/WASM projects.

How it Works
------------
Uniform random shuffle (Fisher-Yates) frequently places tracks by the same artist
adjacent to each other. When a band has 10 tracks in a 100-track playlist, true
randomness clumps them together, creating the illusion of a broken shuffle.

Streaming services often fix this with proprietary recommendation black boxes
that repeat the same 30 songs while starving the rest of the library.

`dshuf` provides transparent, mathematically balanced spacing:

1. **Ideal Interval:** For a window of size $W$ where an attribute appears $c$ times,
   the target spacing is $S = W / c$.
2. **Signed Spacing Deviation:** As candidate items are evaluated, their distance $d$
   from their previous play is checked against their ideal spacing:
   $$\text{deviation} = \frac{S - d}{S}$$
   - $d < S$: Positive penalty (item is premature).
   - $d = S$: Zero penalty (exact balanced interval).
   - $d > S$: Negative penalty / urgency bonus (item is overdue).
3. **Multi-Key Weighting:** Multiple keys (e.g. artist with weight 1.0, album with
   weight 0.5) combine into a single penalty score:
   $$\text{penalty} = \sum_{k=0}^{K-1} w_k \cdot \text{deviation}_k$$
4. **Starvation Control & Jitter:** An age discount prevents dominant artists from
   blocking rare tracks, while a tunable jitter factor $\alpha \in [0.0, 1.0]$ adds
   controlled noise to keep shuffle orders fresh across runs.

Features
--------
* Strict C99 and standard POSIX libc headers only.
* Single-header library (`dshuf.h`): drop into any C or C++ project.
* Multi-key hierarchical clustering (`-k field[:weight]`).
* Infinite streaming mode (`-S`): runs forever over Unix pipes with strictly bounded $O(W)$ memory.
* Bounded sliding window with zero starvation.
* Embedded SplitMix64 PRNG with explicit seed support (`-s seed`).
* Custom delimiter support (`-d delim`).
* Drop-in compatibility with Unix pipelines (`cut`, `sort`, `shuf`).
* Zero-dependency bindings for modern C++, Python (ctypes), and TypeScript / WASM.

CLI Usage
---------
```sh
# Shuffle music playlist by artist (column 1, tab-delimited)
dshuf -k 1 playlist.tsv

# Multi-key: cluster by artist (col 1, weight 1.0) and album (col 2, weight 0.5)
dshuf -k 1:1.0 -k 2:0.5 playlist.tsv

# Infinite stream over Unix pipe with bounded memory buffer (window = 128)
cat stream.tsv | dshuf -k 1 -S -w 128

# Control randomness: 0.0 (strictly balanced) to 1.0 (pure Fisher-Yates)
dshuf -k 1 -j 0.05 playlist.tsv

# Deterministic shuffle using an explicit seed
dshuf -k 1 -s 1337 playlist.tsv

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
uint32_t keys[100]; // Pre-computed FNV-1a hashes

// Shuffles indices in place with cluster balancing
dshuf_batch(indices, keys, n, 1, NULL, 0.20f, 0, 42);
```

### Streaming State Machine
```c
dshuf_stream_t stream;
dshuf_stream_init(&stream, 256, 1, NULL, 0.20f, 42);

// Push items as they arrive
uint32_t artist_key = dshuf_hash_str("Radiohead");
dshuf_stream_push(&stream, &artist_key, my_track_pointer);

// Pop the next best item
void *item = NULL;
if (dshuf_stream_pop(&stream, &item)) {
    // Process item
}

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
```

TypeScript / WASM (`dshuf.ts`)
------------------------------
Zero-dependency module running in Node, Bun, Deno, and modern browsers:
```typescript
import { shuffle, Stream } from "./dshuf";

const playlist = [
  { artist: "Radiohead", album: "OK Computer", title: "Airbag" },
  { artist: "Beatles", album: "Abbey Road", title: "Come Together" },
];

const balanced = shuffle(playlist, {
  keys: (t) => [t.artist, t.album],
  jitter: 0.15,
});
```

Build and Install
-----------------
```sh
# Build CLI tool and shared library
make

# Run all test suites (C, CLI, C++, Python)
make test-all

# Install to /usr/local
sudo make install
```

License
-------
MIT License. See [LICENSE](LICENSE) for details.