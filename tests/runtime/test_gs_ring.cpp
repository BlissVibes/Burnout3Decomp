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

// GIF data added by the poster (the VU thread's XGKICKs, relays and DIRECT data) is written into the ring but
// not sent: the reader sees none of it until a send, and then sees the pieces of one path that followed each
// other as one message, their bytes in order.
void added_gif_data_waits_for_a_send_and_comes_as_one_message() {
    queue::Ring &ring = ring_of(4096);
    const Bytes a = pattern(48, 1), b = pattern(32, 2), c = pattern(16, 3);
    ring.add_gif(1, a.data(), 48);
    ring.add_gif(1, b.data(), 32);
    ring.add_gif(1, c.data(), 16);
    CHECK(empty(ring));
    ring.send();
    queue::Header h{};
    const std::uint8_t *payload = nullptr;
    CHECK(ring.peek(h, payload));
    Bytes all = a;
    all.insert(all.end(), b.begin(), b.end());
    all.insert(all.end(), c.begin(), c.end());
    CHECK(h.type == queue::kGif && h.arg0 == 1u && h.arg1 == 6u && h.bytes == 96u && std::memcmp(payload, all.data(), 96) == 0);
    ring.pop(h);
    CHECK(empty(ring));
    CHECK_EQ(ring.messages(), 1u);
    CHECK_EQ(ring.sends(), 1u);
}

// Reads the next message, which must be there already: GIF data of `path` with these bytes.
bool next_gif(queue::Ring &ring, std::uint32_t path, const Bytes &bytes) {
    queue::Header h{};
    const std::uint8_t *payload = nullptr;
    if (!ring.peek(h, payload)) return false;
    const bool same = h.type == queue::kGif && h.arg0 == path && h.arg1 * 16 == bytes.size() && h.bytes == bytes.size() &&
                      std::memcmp(payload, bytes.data(), bytes.size()) == 0;
    ring.pop(h);
    return same;
}

// Another path starts another message, and a message posted after added data sends that data first, in
// the order it came.
void a_post_sends_what_was_added_before_it_in_order() {
    queue::Ring &ring = ring_of(4096);
    const Bytes a = pattern(64, 1), b = pattern(32, 2), c = pattern(48, 3), d = pattern(16, 4);
    ring.add_gif(1, a.data(), 64);
    ring.add_gif(2, b.data(), 32);
    ring.add_gif(2, c.data(), 48);
    ring.add_gif(1, d.data(), 16);
    CHECK(empty(ring));
    post(ring, 9, 20);
    CHECK(next_gif(ring, 1, a));
    Bytes bc = b;
    bc.insert(bc.end(), c.begin(), c.end());
    CHECK(next_gif(ring, 2, bc));
    CHECK(next_gif(ring, 1, d));
    CHECK(expect(ring, 9, 20));
    CHECK(empty(ring));
    ring.add_gif(1, a.data(), 64);                     // what follows a post is a message of its own
    ring.send();
    CHECK(next_gif(ring, 1, a));
    CHECK(empty(ring));
}

// A poster that adds and adds without sending still feeds the reader: once queue::kSendBytes wait, they go.
void added_data_is_sent_once_enough_waits() {
    queue::Ring &ring = ring_of(1u << 20);
    const Bytes piece = pattern(16u << 10, 5);
    std::uint64_t added = 0;
    while (added + piece.size() < queue::kSendBytes) {
        ring.add_gif(1, piece.data(), static_cast<std::uint32_t>(piece.size()));
        added += piece.size();
    }
    CHECK(empty(ring));
    ring.add_gif(1, piece.data(), static_cast<std::uint32_t>(piece.size()));
    CHECK(!empty(ring));
}

// A poster that runs out of room while what it added is unsent must send it before it waits: the reader can
// only free what it was given.
void a_poster_out_of_room_sends_what_it_added_first() {
    queue::Ring &ring = ring_of(256);
    const Bytes a = pattern(64, 1), b = pattern(64, 2);
    for (int i = 0; i < 3; ++i) ring.add_gif(1, a.data(), 64);    // one message of 16 + 192 bytes, unsent
    const std::atomic<bool> &added = start([&ring, b] { ring.add_gif(2, b.data(), 64); });   // 80 more: no room
    queue::Header h{};
    const std::uint8_t *payload = nullptr;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(kPatience);
    bool seen = false;
    while (!(seen = ring.peek(h, payload)) && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::microseconds(100));
    CHECK(seen);
    if (!seen) return;
    CHECK(h.arg0 == 1u && h.bytes == 192u);
    ring.pop(h);
    CHECK(becomes_true(added));
    ring.send();
    CHECK(next_gif(ring, 2, b));
    CHECK(empty(ring));
}

// Pieces of random size and path, sends at random points and a reader at work, round and round a small ring:
// the reader gets every path's bytes in order, never a message over the end of the buffer, and the runs of
// one path in the order they were added.
void added_data_round_and_round_the_buffer_loses_nothing() {
    queue::Ring &ring = ring_of(1024);
    constexpr std::uint32_t kPieces = 30000;
    struct Run {
        std::uint32_t path;
        Bytes bytes;
    };
    auto *want = new std::vector<Run>;
    std::uint32_t seed = 12345;
    const auto next = [&seed] { return seed = seed * 1103515245u + 12345u, seed >> 8; };
    std::vector<std::pair<std::uint32_t, Bytes>> pieces;
    std::vector<bool> send_after;
    for (std::uint32_t i = 0; i < kPieces; ++i) {
        const std::uint32_t path = 1 + next() % 3, bytes = 16 * (1 + next() % 40);
        pieces.push_back({path, pattern(bytes, i)});
        send_after.push_back(next() % 7 == 0);
    }
    // What the reader must see: pieces of a path that follow each other while unsent join, so compare the
    // runs of one path between paths, with the bytes laid end to end.
    for (const auto &[path, bytes] : pieces) {
        if (want->empty() || want->back().path != path) want->push_back({path, {}});
        want->back().bytes.insert(want->back().bytes.end(), bytes.begin(), bytes.end());
    }
    auto *wrong = new std::atomic<int>{0};
    const std::atomic<bool> &read = start([&ring, want, wrong] {
        std::size_t run = 0, at = 0;
        std::uint32_t last = 0;
        while (run < want->size() && wrong->load() < 3) {
            queue::Header h{};
            const std::uint8_t *payload = nullptr;
            if (!ring.peek(h, payload)) {
                ring.wait_for_work(50);
                continue;
            }
            if (h.type != queue::kGif || h.bytes != h.arg1 * 16) ++*wrong;
            if (h.arg0 != (*want)[run].path || (last == h.arg0 && at == 0)) ++*wrong;
            const Bytes &w = (*want)[run].bytes;
            if (at + h.bytes > w.size() || std::memcmp(payload, w.data() + at, h.bytes) != 0) ++*wrong;
            at += h.bytes;
            last = h.arg0;
            if (at >= w.size()) {
                ++run;
                at = 0;
            }
            ring.pop(h);
        }
    });
    const std::atomic<bool> &posted = start([&ring, pieces, send_after] {
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            ring.add_gif(pieces[i].first, pieces[i].second.data(), static_cast<std::uint32_t>(pieces[i].second.size()));
            if (send_after[i]) ring.send();
        }
        ring.send();
    });
    CHECK(becomes_true(posted, 20000));
    CHECK(becomes_true(read, 20000));
    CHECK_EQ(wrong->load(), 0);
}

// A reader asleep in wait_for_work() is woken by every send that finds it waiting, wherever the two threads
// are when it goes to sleep. A wake that is lost leaves the reader asleep until its timeout, which here is
// far longer than the whole exchange should take.
void a_waiting_reader_never_sleeps_through_a_send() {
    queue::Ring &ring = ring_of(1u << 16);
    constexpr std::uint32_t kCount = 50000;
    auto *slow = new std::atomic<int>{0};
    const std::atomic<bool> &read = start([&ring, slow] {
        for (std::uint32_t i = 0; i < kCount;) {
            queue::Header h{};
            const std::uint8_t *payload = nullptr;
            if (ring.peek(h, payload)) {
                ring.pop(h);
                ++i;
                continue;
            }
            const auto before = std::chrono::steady_clock::now();
            ring.wait_for_work(2000);
            if (std::chrono::steady_clock::now() - before > std::chrono::milliseconds(1000)) ++*slow;
        }
    });
    const std::atomic<bool> &posted = start([&ring] {
        std::uint32_t seed = 7;
        for (std::uint32_t i = 0; i < kCount; ++i) {
            post(ring, i, 16);
            seed = seed * 1103515245u + 12345u;
            for (std::uint32_t spin = (seed >> 16) % 2000; spin; --spin) std::atomic_signal_fence(std::memory_order_seq_cst);
        }
    });
    CHECK(becomes_true(posted, 10000));
    CHECK(becomes_true(read, 10000));
    CHECK_EQ(slow->load(), 0);
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
    added_gif_data_waits_for_a_send_and_comes_as_one_message();
    a_post_sends_what_was_added_before_it_in_order();
    added_data_is_sent_once_enough_waits();
    a_poster_out_of_room_sends_what_it_added_first();
    added_data_round_and_round_the_buffer_loses_nothing();
    a_waiting_reader_never_sleeps_through_a_send();
    unlink(path);
    return check::done("test_gs_ring");
}
