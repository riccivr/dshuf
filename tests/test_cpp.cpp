#define DSHUF_IMPLEMENTATION
#include "../bindings/cpp/dshuf.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <cassert>

struct Track {
    std::string title;
    std::string artist;
    std::string album;
};

int main() {
    std::cout << "=== C++ Binding Tests ===" << std::endl;

    std::vector<Track> playlist = {
        {"Track 1", "Radiohead", "OK Computer"},
        {"Track 2", "Radiohead", "OK Computer"},
        {"Track 3", "Radiohead", "The Bends"},
        {"Track 4", "Daft Punk", "Discovery"},
        {"Track 5", "Daft Punk", "Discovery"},
        {"Track 6", "Daft Punk", "Homework"},
        {"Track 7", "Beatles", "Abbey Road"},
        {"Track 8", "Beatles", "Abbey Road"},
        {"Track 9", "Beatles", "Revolver"}
    };

    // Test 1: Single-key shuffle
    dshuf::shuffle(playlist.begin(), playlist.end(), [](const Track &t) {
        return t.artist;
    }, 0.05f, 16, 42);

    assert(playlist.size() == 9);
    std::cout << "  [PASS] Single-key in-place vector shuffle" << std::endl;

    // Test 2: Multi-key shuffle
    dshuf::shuffle_multi(playlist.begin(), playlist.end(), 2, [](const Track &t) {
        return std::vector<uint32_t>{dshuf::to_key(t.artist), dshuf::to_key(t.album)};
    }, {1.0f, 0.5f}, 0.05f, 16, 99);

    assert(playlist.size() == 9);
    std::cout << "  [PASS] Multi-key in-place vector shuffle" << std::endl;

    // Test 3: C++ Stream API
    dshuf::Stream<std::string> stream(8, 1, {}, 0.1f, 777);
    stream.push("Song A", "Artist1");
    stream.push("Song B", "Artist2");
    stream.push("Song C", "Artist1");

    assert(stream.count() == 3);
    std::string popped;
    bool ok = stream.pop(popped);
    assert(ok && !popped.empty());
    std::cout << "  [PASS] C++ Stream push/pop" << std::endl;

    std::cout << "All C++ binding tests passed!" << std::endl;
    return 0;
}