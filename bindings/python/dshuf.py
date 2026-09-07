"""
dshuf - Multi-key balanced shuffler for Python.
Zero external dependencies, powered by pure C99 dshuf.h via ctypes.
"""

import ctypes
import os
import sys
from typing import Any, Callable, Dict, Iterable, Iterator, List, Optional, Sequence, Union

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
    # Pure Python fallback for FNV-1a
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
    Perform a multi-key balanced shuffle on a sequence of items.

    Args:
        items: List or sequence of items to shuffle.
        key: Function mapping an item to its cluster attribute (e.g. lambda x: x.artist).
        keys: Function mapping an item to multiple cluster attributes, or list of extractor functions.
        weights: Hierarchy weights for multi-key levels.
        jitter: Randomness factor in [0.0, 1.0]. Default 0.20. At 1.0, pure random shuffle.
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

    num_keys = 0
    key_hashes: List[int] = []

    if keys is not None:
        if callable(keys):
            sample_keys = keys(items[0])
            num_keys = len(sample_keys)
            for it in items:
                for k in keys(it):
                    key_hashes.append(fnv1a(k))
        else:
            num_keys = len(keys)
            for it in items:
                for fn in keys:
                    key_hashes.append(fnv1a(fn(it)))
    elif key is not None:
        num_keys = 1
        for it in items:
            key_hashes.append(fnv1a(key(it)))

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
        raise RuntimeError("dshuf_batch failed with code %d" % ret)

    return [items[indices_arr[i]] for i in range(n)]


class Stream:
    """
    Streaming shuffler maintaining bounded O(W) sliding window memory.
    """
    def __init__(
        self,
        window_cap: int = 256,
        weights: Optional[Sequence[float]] = None,
        jitter: float = 0.20,
        seed: Optional[int] = None,
    ):
        self.window_cap = window_cap
        self.jitter = jitter
        self.weights = list(weights) if weights else []
        self.seed = seed
        self._items: Dict[int, Any] = {}
        self._next_id = 1
        self._num_keys = len(self.weights) if self.weights else 1

        # Use Python fallback queue if shared library is not loaded
        # otherwise use native C stream API
        self._c_stream = None
        if _lib and hasattr(_lib, "dshuf_stream_init"):
            # Opaque buffer for sizeof(dshuf_stream_t) (conservatively 256 bytes)
            self._ctx = (ctypes.c_char * 512)()
            w_arr = (ctypes.c_float * len(self.weights))(*self.weights) if self.weights else None
            c_seed = seed if seed is not None else int.from_bytes(os.urandom(8), "little")
            ret = _lib.dshuf_stream_init(
                self._ctx, window_cap, self._num_keys, w_arr, ctypes.c_float(jitter), ctypes.c_uint64(c_seed)
            )
            if ret == 0:
                self._c_stream = self._ctx

    def push(self, item: Any, keys: Optional[Sequence[Any]] = None) -> bool:
        """Push an item into the stream buffer."""
        key_list: List[int] = []
        if keys:
            key_list = [fnv1a(k) for k in keys]
        elif isinstance(item, (tuple, list)) and len(item) > 1:
            key_list = [fnv1a(item[0])]
        else:
            key_list = [fnv1a(item)]

        item_id = self._next_id
        self._next_id += 1
        self._items[item_id] = item

        if self._c_stream:
            k_arr = (ctypes.c_uint32 * len(key_list))(*key_list)
            res = _lib.dshuf_stream_push(self._c_stream, k_arr, ctypes.c_void_p(item_id))
            if res <= 0:
                del self._items[item_id]
                return False
            return True
        return True

    def pop(self) -> Optional[Any]:
        """Pop the next best item from the window."""
        if not self._items:
            return None

        if self._c_stream:
            out_ptr = ctypes.c_void_p()
            ok = _lib.dshuf_stream_pop(self._c_stream, ctypes.byref(out_ptr))
            if ok and out_ptr.value:
                return self._items.pop(out_ptr.value, None)
            return None

        # Fallback if no shared library
        item_id = next(iter(self._items))
        return self._items.pop(item_id)

    def count(self) -> int:
        if self._c_stream:
            return _lib.dshuf_stream_count(self._c_stream)
        return len(self._items)

    def __len__(self) -> int:
        return self.count()

    def __del__(self):
        if self._c_stream and hasattr(_lib, "dshuf_stream_free"):
            _lib.dshuf_stream_free(self._c_stream)


def stream(
    iterable: Iterable[Any],
    key: Optional[Callable[[Any], Any]] = None,
    keys: Optional[Callable[[Any], Sequence[Any]]] = None,
    weights: Optional[Sequence[float]] = None,
    window_size: int = 256,
    jitter: float = 0.20,
    seed: Optional[int] = None,
) -> Iterator[Any]:
    """
    Lazily stream items through a bounded sliding window buffer.
    """
    s = Stream(window_cap=window_size, weights=weights, jitter=jitter, seed=seed)
    it = iter(iterable)

    for item in it:
        k = None
        if keys:
            k = keys(item)
        elif key:
            k = [key(item)]

        while s.count() >= window_size:
            popped = s.pop()
            if popped is not None:
                yield popped

        s.push(item, k)

    # Drain remaining
    while s.count() > 0:
        popped = s.pop()
        if popped is not None:
            yield popped