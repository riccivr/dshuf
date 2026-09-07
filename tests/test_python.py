import sys
import os
sys.path.insert(0, os.path.abspath("bindings/python"))
import dshuf

def test_python_bindings():
    print("=== Python Binding Tests ===")

    tracks = [
        {"artist": "Radiohead", "title": "Karma Police"},
        {"artist": "Radiohead", "title": "Creep"},
        {"artist": "Radiohead", "title": "Paranoid Android"},
        {"artist": "Beatles", "title": "Come Together"},
        {"artist": "Beatles", "title": "Something"},
        {"artist": "Beatles", "title": "Taxman"},
    ]

    shuffled = dshuf.shuffle(tracks, key=lambda t: t["artist"], jitter=0.05, seed=42)
    assert len(shuffled) == len(tracks)

    artists = [t["artist"] for t in shuffled]
    print(f"  Shuffled order: {' -> '.join(artists)}")

    for i in range(len(artists) - 1):
        assert artists[i] != artists[i+1], f"Artists repeated back-to-back at {i}"

    print("  [PASS] Single-key Python shuffle with zero adjacent repeats")

    items = [
        ("Radiohead", "OK Computer", 1),
        ("Radiohead", "OK Computer", 2),
        ("Radiohead", "The Bends", 3),
        ("Beatles", "Abbey Road", 4),
        ("Beatles", "Abbey Road", 5),
        ("Beatles", "Revolver", 6),
    ]

    mk_shuffled = dshuf.shuffle(items, keys=lambda x: (x[0], x[1]), jitter=0.05, seed=123)
    assert len(mk_shuffled) == len(items)
    print("  [PASS] Multi-key Python shuffle")

    # Native Python Stream test
    s = dshuf.Stream(window_cap=8, jitter=0.1, seed=99)
    s.push("Radiohead_1", ["Radiohead"])
    s.push("Beatles_1", ["Beatles"])
    s.push("Radiohead_2", ["Radiohead"])
    assert s.count() == 3

    popped = s.pop()
    assert popped is not None
    print(f"  Stream popped: {popped}")
    print("  [PASS] Python Stream push/pop")

    # Generator stream() test
    gen_items = [f"Band{i%3}_Track{i}" for i in range(20)]
    streamed = list(dshuf.stream(gen_items, key=lambda x: x.split("_")[0], window_size=8, seed=42))
    assert len(streamed) == len(gen_items)
    print("  [PASS] Python stream() generator")

    print("All Python binding tests passed!")

if __name__ == "__main__":
    test_python_bindings()