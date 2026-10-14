// The fork's message ring alone (the queue between the emulation, VU and render threads of the page): one
// poster, one reader, messages of a 16-byte header and a payload rounded up to 16 bytes, in a buffer that is
// written round and round. A message is never split over the end of the buffer: the poster gives up the rest
// of the buffer and starts again at the front. Whatever the position of the head, a message that fits the
// ring at all must get through once the reader has made room, and one that can never fit must be refused,
// not waited for. A poster that waits for ever shows here as a step that does not finish in half a second.
//
// fork-source: runtime/src/gs/queue.cpp

#include "check.hpp"

#include "queue.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace {

namespace queue = ps2::gs::queue;
using Bytes = std::vector<std::uint8_t>;

constexpr std::uint32_t kHeader = sizeof(queue::Header);

// A ring that is never destroyed: a poster stuck in it (a failed check) must not see it go away.
queue::Ring &ring_of(std::size_t capacity) {
    auto *ring = new queue::Ring;
    ring->start(capacity);
    return *ring;
}

Bytes pattern(std::uint32_t bytes, std::uint32_t seed) {
    Bytes out(bytes);
    for (std::uint32_t i = 0; i < bytes; ++i) out[i] = static_cast<std::uint8_t>(seed * 131u + i * 7u + 1u);
    return out;
}

void post(queue::Ring &ring, std::uint32_t seed, std::uint32_t bytes) {
    const Bytes data = pattern(bytes, seed);
    ring.post(queue::kGif, seed, ~seed, data.data(), bytes);
}

std::string g_log;   // stderr goes here for the whole run

// What has been printed to stderr since the last call.
std::string printed() {
    static std::streamoff seen = 0;
    std::fflush(stderr);
    std::ifstream in(g_log, std::ios::binary);
    in.seekg(seen);
    std::ostringstream text;
    text << in.rdbuf();
    seen += static_cast<std::streamoff>(text.str().size());
    return text.str();
}

bool has(const std::string &text, const char *part) { return text.find(part) != std::string::npos; }

constexpr int kPatience = 500;   // milliseconds

bool becomes_true(const std::atomic<bool> &flag, int ms = kPatience) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!flag.load() && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::microseconds(100));
    return flag.load();
}

// Runs `step` on a thread of its own and returns at once; the flag turns true when it has finished. A step
// that never finishes is left behind.
const std::atomic<bool> &start(std::function<void()> step) {
    auto *done = new std::atomic<bool>{false};
    std::thread([step = std::move(step), done] {
        step();
        done->store(true);
    }).detach();
    return *done;
}

// Reads the next message, waiting a while for it, and checks it is message `seed` of `bytes` bytes.
bool expect(queue::Ring &ring, std::uint32_t seed, std::uint32_t bytes) {
    queue::Header h{};
    const std::uint8_t *payload = nullptr;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(kPatience);
    while (!ring.peek(h, payload)) {
        if (std::chrono::steady_clock::now() >= until) return false;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    const Bytes want = pattern(bytes, seed);
    const bool same = h.type == queue::kGif && h.arg0 == seed && h.arg1 == ~seed && h.bytes == bytes &&
                      (bytes == 0 || std::memcmp(payload, want.data(), bytes) == 0);
    ring.pop(h);
    return same;
}

bool empty(queue::Ring &ring) {
    queue::Header h{};
    const std::uint8_t *payload = nullptr;
    return !ring.peek(h, payload);
}

void messages_come_out_as_posted_round_and_round_the_buffer() {
    queue::Ring &ring = ring_of(256);
    int wrong = 0;
    for (std::uint32_t i = 0; i < 500; ++i) {          // one at a time, each at most half the ring
        const std::uint32_t bytes = (i * 37u) % 113u;
        post(ring, i, bytes);
        if (!expect(ring, i, bytes)) ++wrong;
    }
    CHECK_EQ(wrong, 0);
    for (std::uint32_t i = 0; i < 300; ++i) {          // three waiting at a time
        post(ring, i, (i * 5u) % 40u);
        if (i >= 2 && !expect(ring, i - 2, ((i - 2) * 5u) % 40u)) ++wrong;
    }
    CHECK_EQ(wrong, 0);
    CHECK(expect(ring, 298, (298u * 5u) % 40u));
    CHECK(expect(ring, 299, (299u * 5u) % 40u));
    CHECK(empty(ring));
    CHECK_EQ(ring.waits(), 0u);
    CHECK_EQ(ring.largest(), 128u);                    // 112 bytes and the header
}

void a_full_ring_makes_the_poster_wait_for_the_reader() {
    queue::Ring &ring = ring_of(256);
    for (std::uint32_t i = 0; i < 4; ++i) post(ring, i, 64 - kHeader);     // 4 x 64 bytes: full
    std::atomic<bool> posted{false};
    std::thread poster([&] {
        post(ring, 4, 64 - kHeader);
        posted.store(true);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(!posted.load());                             // no room: it waits
    CHECK(expect(ring, 0, 64 - kHeader));              // the reader takes one
    poster.join();                                     // and the poster goes on
    CHECK(posted.load());
    for (std::uint32_t i = 1; i < 5; ++i) CHECK(expect(ring, i, 64 - kHeader));
    CHECK(empty(ring));
    CHECK_EQ(ring.high_water(), 256u);                 // the ring keeps count of how full it has been,
    CHECK_EQ(ring.largest(), 64u);                     // of its largest message
    CHECK_EQ(ring.waits(), 1u);                        // and of how often the poster found no room
}

// Posts message 7 of `bytes` bytes into a ring of 256 whose head stands at `head`, with a reader at work.
bool gets_through(std::uint32_t head, std::uint32_t bytes) {
    queue::Ring &ring = ring_of(256);
    if (head) {
        post(ring, 1, head - kHeader);
        if (!expect(ring, 1, head - kHeader)) return false;
    }
    const std::atomic<bool> &posted = start([&ring, bytes] { post(ring, 7, bytes); });
    const bool read = expect(ring, 7, bytes);
    return read && becomes_true(posted) && empty(ring);
}

void a_message_of_exactly_the_capacity_fits_wherever_the_head_is() {
    CHECK(gets_through(0, 256 - kHeader));
    CHECK(gets_through(32, 256 - kHeader));
    CHECK(gets_through(128, 256 - kHeader));
    CHECK(gets_through(240, 256 - kHeader));
}

void a_message_of_more_than_half_the_ring_fits_wherever_the_head_is() {
    for (std::uint32_t head = 0; head < 256; head += 16) {
        CHECK(gets_through(head, 144 - kHeader));
        CHECK(gets_through(head, 200 - kHeader));
    }
}

void a_message_above_the_capacity_is_refused_and_the_ring_goes_on() {
    for (std::uint32_t head : {0u, 48u}) {
        queue::Ring &ring = ring_of(256);
        if (head) {
            post(ring, 1, head - kHeader);
            CHECK(expect(ring, 1, head - kHeader));
        }
        printed();
        const bool refused = becomes_true(start([&ring] { post(ring, 2, 256 - kHeader + 1); })) &&    // one byte too many
                             becomes_true(start([&ring] { post(ring, 3, 4096); }));
        CHECK(refused);                                                    // it comes back; it does not wait
        if (!refused) continue;
        const std::string said = printed();                                // and it says so
        CHECK(has(said, "a message of 241 bytes (type 1) does not fit the ring of 256 bytes"));
        CHECK(has(said, "a message of 4096 bytes (type 1) does not fit the ring of 256 bytes"));
        CHECK(empty(ring));                                                // and nothing of it was queued
        post(ring, 4, 100);
        CHECK(expect(ring, 4, 100));
    }
}

void a_poster_and_a_reader_at_full_speed_lose_nothing() {
    queue::Ring &ring = ring_of(1024);
    constexpr std::uint32_t kCount = 20000;
    const auto size = [](std::uint32_t i) { return (i * 2654435761u >> 16) % (1024u - kHeader + 1u); };   // 0 to a whole ring
    auto *wrong = new std::atomic<int>{0};
    const std::atomic<bool> &read = start([&ring, size, wrong] {
        for (std::uint32_t i = 0; i < kCount && wrong->load() < 3; ++i)
            if (!expect(ring, i, size(i))) ++*wrong;
    });
    const std::atomic<bool> &posted = start([&ring, size] {
        for (std::uint32_t i = 0; i < kCount; ++i) post(ring, i, size(i));
    });
    CHECK(becomes_true(read, 20000));
    CHECK(becomes_true(posted));
    CHECK_EQ(wrong->load(), 0);
}

}

int main() {
    char path[] = "/tmp/b3-test-gs-ring-XXXXXX";
    const int file = mkstemp(path);
    if (file < 0) return 2;
    g_log = path;
    dup2(file, 2);
    messages_come_out_as_posted_round_and_round_the_buffer();
    a_full_ring_makes_the_poster_wait_for_the_reader();
    a_message_of_exactly_the_capacity_fits_wherever_the_head_is();
    a_message_of_more_than_half_the_ring_fits_wherever_the_head_is();
    a_message_above_the_capacity_is_refused_and_the_ring_goes_on();
    a_poster_and_a_reader_at_full_speed_lose_nothing();
    unlink(path);
    return check::done("test_gs_ring");
}
