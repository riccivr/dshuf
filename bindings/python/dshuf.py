"""
dshuf - Multi-key low-discrepancy shuffler for Python.
Zero external dependencies, powered by pure C99 dshuf.h via ctypes.
"""

import ctypes
import os
import sys
from typing import Any, Callable, Iterable, Iterator, List, Optional, Sequence, Union

# Locate shared library
_lib_names = ["libdshuf.so", "dshuf.dll", "libdshuf.dylib"]
_search_dirs = [
    os.path.dirname(__file__),
    os.path.join(os.path.dirname(__file__), "..", ".."),
    os.getcwd(),
]

_lib = None
for sdir in _search_dirs:
    for name in _lib_names:
        p = os.path.abspath(os.path.join(sdir, name))
        if os.path.isfile(p):
            try:
                _lib = ctypes.CDLL(p)
                break
            except OSError:
                continue
    if _lib:
        break

if _lib:
    # Function prototypes
    _lib.dshuf_batch.argtypes = [
        ctypes.POINTER(ctypes.c_size_t),
        ctypes.POINTER(ctypes.c_uint32),
        ctypes.c_size_t,
        ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_float),
        ctypes.c_float,
        ctypes.c_size_t,
        ctypes.c_uint64,
    ]
    _lib.dshuf_batch.restype = ctypes.c_int

    if hasattr(_lib, "dshuf_hash"):
        _lib.dshuf_hash.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
        _lib.dshuf_hash.restype = ctypes.c_uint32


def fnv1a(val: Any) -> int:
    """Hash an arbitrary python object into 32-bit FNV-1a uint32."""
    if isinstance(val, int):
        return val & 0xFFFFFFFF
    b = str(val).encode("utf-8")
    if _lib and hasattr(_lib, "dshuf_hash"):
        return _lib.dshuf_hash(b, len(b))
    # Pure Python fallback for hash
    h = 2166136261
    for byte in b:
        h = ((h ^ byte) * 16777619) & 0xFFFFFFFF
    return h


def shuffle(
    items: Sequence[Any],
    key: Optional[Callable[[Any], Any]] = None,
    keys: Optional[Union[Callable[[Any], Sequence[Any]], Sequence[Callable[[Any], Any]]]] = None,
    weights: Optional[Sequence[float]] = None,
    jitter: float = 0.20,
    window_size: int = 0,
    seed: Optional[int] = None,
) -> List[Any]:
    """
    Perform a multi-key low-discrepancy shuffle on a sequence of items.

    Args:
        items: List or sequence of items to shuffle.
        key: Function mapping an item to its cluster attribute (e.g. lambda x: x.artist).
        keys: Function mapping an item to multiple cluster attributes, or list of extractor functions.
        weights: Hierarchy weights for multi-key levels.
        jitter: Randomness factor in [0.0, 1.0]. Default 0.20.
        window_size: Sliding window size (0 for auto).
        seed: Optional integer seed for reproducibility.

    Returns:
        A new list containing the items in shuffled order.
    """
    n = len(items)
    if n <= 1:
        return list(items)

    if not _lib:
        raise RuntimeError("libdshuf shared library not found. Compile it with 'make' first.")

    # Determine multi-key hashes
    num_keys = 0
    key_hashes: List[int] = []

    if keys is not None:
        if callable(keys):
            # keys is a function returning [k1, k2, ...]
            sample_keys = keys(items[0])
            num_keys = len(sample_keys)
            for it in items:
                for k in keys(it):
                    key_hashes.append(fnv1a(k))
        else:
            # keys is a list of functions [fn1, fn2, ...]
            num_keys = len(keys)
            for it in items:
                for fn in keys:
                    key_hashes.append(fnv1a(fn(it)))
    elif key is not None:
        num_keys = 1
        for it in items:
            key_hashes.append(fnv1a(key(it)))

    # Allocate ctypes arrays
    indices_arr = (ctypes.c_size_t * n)()
    keys_arr = (ctypes.c_uint32 * (n * num_keys))(*key_hashes) if num_keys > 0 else None

    weights_arr = None
    if weights and num_keys > 0:
        weights_arr = (ctypes.c_float * num_keys)(*weights)

    c_seed = seed if seed is not None else int.from_bytes(os.urandom(8), "little")

    ret = _lib.dshuf_batch(
        indices_arr,
        keys_arr,
        n,
        num_keys,
        weights_arr,
        ctypes.c_float(jitter),
        window_size,
        ctypes.c_uint64(c_seed),
    )

    if ret != 0:
        raise RuntimeError("dshuf_batch returned failure code %d" % ret)

    return [items[indices_arr[i]] for i in range(n)]


def stream(
    iterable: Iterable[Any],
    key: Optional[Callable[[Any], Any]] = None,
    window_size: int = 256,
    jitter: float = 0.20,
    seed: Optional[int] = None,
) -> Iterator[Any]:
    """
    Lazily stream items through a bounded sliding window buffer.
    """
    # Simple Python generator wrapper over window buffer
    # When feeding infinite generators
    buf: List[Any] = []
    it = iter(iterable)

    # Prime window
    for _ in range(window_size):
        try:
            buf.append(next(it))
        except StopIteration:
            break

    if not buf:
        return

    # Shuffle initial batch and stream
    while buf:
        shuffled = shuffle(buf, key=key, jitter=jitter, window_size=min(len(buf), window_size), seed=seed)
        yield shuffled[0]
        buf = shuffled[1:]
        try:
            buf.append(next(it))
        except StopIteration:
            pass