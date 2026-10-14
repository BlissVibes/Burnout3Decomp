// The bookkeeping of the fork's texture upload cache alone (runtime/src/gs/gpu/upload_cache.hpp, a header:
// the GPU renderer around it cannot be built without WebGPU, so there is no source to name below and the
// header is reached from the runtime's include directory). The cache is a ring of 32-bit words: uploads
// are stored one after the other, the write position starts again at the front when the end is reached,
// and an entry is given up only when the write position reaches it. An entry the GPU submit in the making
// has used is never written over: the insert is refused instead. A second array stands in for the GPU
// buffer and is written the way the renderer writes it. The data is made up.

#include "check.hpp"

#include "../src/gs/gpu/upload_cache.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using ps2::gs::gpu::UploadCache;
using Bytes = std::vector<std::uint8_t>;

Bytes texture(std::uint32_t id, std::size_t bytes) {
    Bytes out(bytes);
    for (std::size_t i = 0; i < bytes; ++i) out[i] = static_cast<std::uint8_t>(id * 151u + i * 13u + (i >> 8) + 1u);
    return out;
}

// The cache with the GPU buffer beside it, used the way the renderer uses it.
struct Rig {
    UploadCache cache;
    std::vector<std::uint32_t> gpu;
    std::uint64_t serial = 1;

    explicit Rig(std::uint32_t words) : gpu(words, 0xDEADBEEFu) { cache.reset(words); }

    bool find(std::uint32_t id, const Bytes &b, std::uint32_t &offset) { return cache.find(id, b.data(), b.size(), serial, offset); }
    bool find(std::uint32_t id, const Bytes &b) {
        std::uint32_t offset = 0;
        return find(id, b, offset);
    }
    bool insert(std::uint32_t id, const Bytes &b, std::uint32_t &offset) {
        std::uint32_t words = 0;
        if (!cache.insert(id, b.data(), b.size(), serial, offset, words)) return false;
        std::memcpy(gpu.data() + offset, cache.data() + offset, static_cast<std::size_t>(words) * 4);
        return true;
    }
    bool insert(std::uint32_t id, const Bytes &b) {
        std::uint32_t offset = 0;
        return insert(id, b, offset);
    }
    // What the upload shader would read for a texture found at `offset`.
    bool on_gpu(std::uint32_t offset, const Bytes &b) const { return std::memcmp(gpu.data() + offset, b.data(), b.size()) == 0; }
};

void what_is_stored_stays_until_the_write_position_comes_round() {
    Rig rig(1024);                                 // 4096 bytes
    const Bytes a = texture(1, 300), b = texture(2, 200), c = texture(3, 500);
    std::uint32_t at_a = 0, at_b = 0, at_c = 0;
    CHECK(rig.insert(1, a, at_a));
    CHECK(rig.insert(2, b, at_b));
    CHECK(rig.insert(3, c, at_c));
    CHECK_EQ(rig.cache.entries(), 3u);
    CHECK_EQ(at_a, 0u);
    CHECK_EQ(at_b, 76u);                           // 300 bytes are 75 words, and one word of zeros after them
    CHECK_EQ(at_c, 127u);
    ++rig.serial;
    std::uint32_t found = 99;
    CHECK(rig.find(1, a, found));
    CHECK_EQ(found, at_a);
    CHECK(rig.find(2, b, found));
    CHECK_EQ(found, at_b);
    CHECK(rig.find(3, c, found));
    CHECK_EQ(found, at_c);
    CHECK(rig.on_gpu(at_a, a));
    CHECK(rig.on_gpu(at_b, b));
    CHECK(rig.on_gpu(at_c, c));
    CHECK(!rig.find(1, texture(4, 300)));          // the same key with other bytes is another texture
    CHECK(!rig.find(1, texture(1, 296)));
}

// The game's case: the same set of textures every frame, one submit a frame. From the second frame on
// every one of them comes from the cache.
void the_same_textures_every_frame_all_come_from_the_cache() {
    Rig rig(4096);
    std::vector<Bytes> set;
    for (std::uint32_t i = 0; i < 20; ++i) set.push_back(texture(i, 256 + i * 16));
    int inserted = 0, refused = 0, found_late = 0;
    for (int frame = 0; frame < 50; ++frame) {
        for (std::uint32_t i = 0; i < set.size(); ++i) {
            if (rig.find(i, set[i])) {
                if (frame == 0) ++found_late;
                continue;
            }
            if (rig.insert(i, set[i])) ++inserted;
            else ++refused;
        }
        ++rig.serial;
    }
    CHECK_EQ(inserted, 20);
    CHECK_EQ(refused, 0);
    CHECK_EQ(found_late, 0);
    CHECK_EQ(rig.cache.entries(), 20u);
    CHECK_EQ(rig.cache.inserts(), 20u);            // the cache keeps the same counts for the renderer's statistics
    CHECK_EQ(rig.cache.hits(), 49u * 20u);
    CHECK_EQ(rig.cache.refused(), 0u);
}

void the_oldest_entries_go_when_the_ring_comes_round_and_only_those_in_the_way() {
    Rig rig(1024);
    std::vector<Bytes> t;
    for (std::uint32_t i = 0; i < 12; ++i) t.push_back(texture(i, 396));     // 100 words each: ten fill 1000 of 1024
    for (std::uint32_t i = 0; i < 10; ++i) CHECK(rig.insert(i, t[i]));
    ++rig.serial;
    CHECK_EQ(rig.cache.entries(), 10u);
    std::uint32_t at = 99;
    CHECK(rig.insert(10, t[10], at));              // no room at the end: back to the front, over the first
    CHECK_EQ(at, 0u);
    CHECK(!rig.find(0, t[0]));
    for (std::uint32_t i = 1; i <= 10; ++i) CHECK(rig.find(i, t[i]));
    ++rig.serial;
    CHECK(rig.insert(11, t[11], at));
    CHECK_EQ(at, 100u);
    CHECK(!rig.find(1, t[1]));
    for (std::uint32_t i = 2; i <= 11; ++i) CHECK(rig.find(i, t[i]));
    CHECK_EQ(rig.cache.entries(), 10u);
    ++rig.serial;
    const Bytes small = texture(50, 40);           // 11 words: only the entry it lands on goes
    CHECK(rig.insert(50, small, at));
    CHECK_EQ(at, 200u);
    CHECK(!rig.find(2, t[2]));
    CHECK(rig.find(3, t[3]));
}

void an_entry_the_submit_in_the_making_has_used_is_not_written_over() {
    Rig rig(1024);
    std::vector<Bytes> t;
    for (std::uint32_t i = 0; i < 12; ++i) t.push_back(texture(i, 396));
    for (std::uint32_t i = 0; i < 10; ++i) rig.insert(i, t[i]);
    ++rig.serial;
    std::uint32_t at_first = 99;
    CHECK(rig.find(0, t[0], at_first));            // this submit draws with the first texture
    CHECK(!rig.insert(10, t[10]));                 // which is in the way of the next insert: refused
    CHECK_EQ(rig.cache.refused(), 1u);
    CHECK(rig.on_gpu(at_first, t[0]));
    CHECK(rig.find(0, t[0]));
    CHECK_EQ(rig.cache.entries(), 10u);
    ++rig.serial;                                  // the submit has gone: now it may be replaced
    CHECK(rig.insert(10, t[10]));
    CHECK(!rig.find(0, t[0]));
}

void data_longer_than_a_quarter_of_the_cache_is_not_cached() {
    Rig rig(1024);
    CHECK(rig.insert(1, texture(1, 1020)));        // 255 words and the word of zeros: just a quarter
    CHECK(!rig.insert(2, texture(2, 1024)));
    CHECK_EQ(rig.cache.entries(), 1u);
}

// Many frames of textures that come and go, several submits a frame. Whatever is found must be on the GPU
// as it was stored, and nothing a submit has used may change before that submit has gone.
void a_long_run_never_serves_or_overwrites_the_wrong_bytes() {
    Rig rig(32768);                                // 128 KB
    struct Use {
        std::uint32_t offset, id;
        std::size_t bytes;
    };
    std::vector<Use> in_submit;
    std::uint32_t state = 12345;
    const auto random = [&state] {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    const auto size_of = [](std::uint32_t id) { return static_cast<std::size_t>(256 + (id * 2654435761u >> 20) % 1500); };
    int bad_reads = 0, changed_under_a_submit = 0, hits = 0, uploads = 0, refused = 0;
    for (int step = 0; step < 60000; ++step) {
        const std::uint32_t id = (static_cast<std::uint32_t>(step) / 3000) * 7 + random() % 60;   // the set drifts
        const Bytes b = texture(id, size_of(id));
        std::uint32_t offset = 0;
        ++uploads;
        if (rig.find(id, b, offset)) {
            ++hits;
            if (!rig.on_gpu(offset, b)) ++bad_reads;
            in_submit.push_back({offset, id, b.size()});
        } else if (rig.insert(id, b, offset)) {
            in_submit.push_back({offset, id, b.size()});
        } else {
            ++refused;
        }
        if (random() % 40 == 0) {                  // the submit goes to the GPU
            for (const Use &u : in_submit)
                if (!rig.on_gpu(u.offset, texture(u.id, u.bytes))) ++changed_under_a_submit;
            in_submit.clear();
            ++rig.serial;
        }
    }
    CHECK_EQ(bad_reads, 0);
    CHECK_EQ(changed_under_a_submit, 0);
    CHECK(hits * 10 > uploads * 9);                // sixty textures of at most 1.8 KB at a time: nearly every upload repeats
    CHECK(refused * 20 < uploads);
}

}

int main() {
    what_is_stored_stays_until_the_write_position_comes_round();
    the_same_textures_every_frame_all_come_from_the_cache();
    the_oldest_entries_go_when_the_ring_comes_round_and_only_those_in_the_way();
    an_entry_the_submit_in_the_making_has_used_is_not_written_over();
    data_longer_than_a_quarter_of_the_cache_is_not_cached();
    a_long_run_never_serves_or_overwrites_the_wrong_bytes();
    return check::done("test_upload_cache");
}
