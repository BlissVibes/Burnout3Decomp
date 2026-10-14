// The fork's GIF module alone: what happens to PATH3 data while PATH3 is masked. Data that arrives
// masked is held; each time the mask is lifted exactly one GS packet passes; an open PATH3 drains the
// rest; a saved state carries what is held. The packets here are made up.
//
// fork-source: runtime/src/gs/gif.cpp

#include "check.hpp"

#include "gif.hpp"
#include "gs.hpp"
#include "queue.hpp"

#include "ps2/state.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

struct Post {
    std::uint32_t path, qwords;
};
std::vector<Post> g_posts;
std::vector<std::uint8_t> g_passed;   // every byte forwarded on PATH3, in order
ps2::gs::queue::Role g_role = ps2::gs::queue::Role::Emulation;

}

namespace ps2::gs {

State g_state;
void write(std::uint32_t, std::uint64_t) {}
void signal(std::uint32_t, std::uint64_t) {}
void write_packed(std::uint32_t, const Reg128 &) {}
void write_image(const std::uint8_t *, std::uint32_t) {}

namespace queue {
bool active() { return true; }
Role &role() { return g_role; }
void post(Type type, std::uint32_t arg0, std::uint32_t arg1, const void *data, std::uint32_t bytes) {
    if (type != kGif) return;
    g_posts.push_back({arg0, arg1});
    const auto *p = static_cast<const std::uint8_t *>(data);
    if (arg0 == 3) g_passed.insert(g_passed.end(), p, p + bytes);
}
}

}

namespace {

namespace gif = ps2::gif;
using Bytes = std::vector<std::uint8_t>;

void qword(Bytes &to, std::uint64_t low, std::uint64_t high) {
    std::uint8_t raw[16];
    std::memcpy(raw, &low, 8);
    std::memcpy(raw + 8, &high, 8);
    to.insert(to.end(), raw, raw + 16);
}

// One GS packet: a tag that sets a register `registers` times (address+data form), then an image tag with
// `image` qwords of data, the last tag marked as the end of the packet. Every data qword carries `mark`.
Bytes packet(std::uint32_t registers, std::uint32_t image, std::uint64_t mark) {
    Bytes out;
    if (registers) {
        qword(out, registers | (0ull << 58) | (1ull << 60) | (image ? 0 : 1ull << 15), 0xE);
        for (std::uint32_t i = 0; i < registers; ++i) qword(out, mark + i, 0x3F);   // register 0x3F: nothing listens
    }
    if (image) {
        qword(out, image | (1ull << 15) | (2ull << 58), 0);
        for (std::uint32_t i = 0; i < image; ++i) qword(out, mark + 0x1000 + i, mark);
    }
    return out;
}

void send(const Bytes &data, std::size_t from = 0, std::size_t to = SIZE_MAX) {
    to = std::min(to, data.size());
    gif::transfer(3, data.data() + from, static_cast<std::uint32_t>((to - from) / 16));
}

void append(Bytes &to, const Bytes &more) { to.insert(to.end(), more.begin(), more.end()); }

void fresh() {
    gif::reset();
    g_posts.clear();
    g_passed.clear();
}

std::uint32_t held_flag() { return (gif::read(0x10003020u) >> 6) & 1u; }

void an_open_path3_passes_data_as_it_comes() {
    fresh();
    const Bytes a = packet(2, 5, 0xA000);
    send(a);
    CHECK(g_passed == a);
    CHECK_EQ(g_posts.size(), 1u);
    CHECK_EQ(g_posts[0].qwords, static_cast<std::uint32_t>(a.size() / 16));
    CHECK_EQ(held_flag(), 0u);
}

void a_masked_path3_holds_whole_packets_and_each_unmask_lets_one_through() {
    fresh();
    const Bytes a = packet(2, 5, 0xA000), b = packet(0, 300, 0xB000), c = packet(4, 0, 0xC000), d = packet(1, 17, 0xD000);
    gif::set_path3_mask(true);
    for (const Bytes *p : {&a, &b, &c, &d}) send(*p);
    CHECK(g_passed.empty());
    Bytes expect;
    for (const Bytes *p : {&a, &b, &c, &d}) {
        gif::set_path3_mask(false);
        gif::set_path3_mask(true);
        append(expect, *p);
        CHECK(g_passed == expect);
    }
    CHECK_EQ(g_posts.size(), 4u);
    gif::set_path3_mask(false);                   // nothing left: nothing more passes
    CHECK_EQ(g_posts.size(), 4u);
}

void a_packet_that_arrives_in_pieces_is_released_whole() {
    fresh();
    const Bytes a = packet(3, 40, 0xA000), b = packet(0, 9, 0xB000);
    gif::set_path3_mask(true);
    send(a, 0, 32);                               // the tag and one register
    send(a, 32, 16 * 20);
    send(a, 16 * 20);
    send(b);
    gif::set_path3_mask(false);
    gif::set_path3_mask(true);
    CHECK(g_passed == a);
    gif::set_path3_mask(false);
    Bytes both = a;
    append(both, b);
    CHECK(g_passed == both);
}

void a_packet_already_on_its_way_finishes_under_the_mask() {
    fresh();
    const Bytes a = packet(0, 30, 0xA000), b = packet(0, 4, 0xB000);
    send(a, 0, 16 * 10);                          // open: the first ten qwords pass
    gif::set_path3_mask(true);
    Bytes rest(a.begin() + 16 * 10, a.end());
    append(rest, b);
    send(rest);                                   // the rest of a, then b, in one transfer
    CHECK(g_passed == a);                         // a finished; b waits
    gif::set_path3_mask(false);
    Bytes both = a;
    append(both, b);
    CHECK(g_passed == both);
}

void an_open_path3_drains_everything_it_still_holds() {
    fresh();
    Bytes all;
    gif::set_path3_mask(true);
    for (std::uint64_t i = 0; i < 6; ++i) {
        const Bytes p = packet(1, 8 + static_cast<std::uint32_t>(i), 0x100 * (i + 1));
        send(p);
        append(all, p);
    }
    gif::set_path3_mask(false);                   // the first packet
    CHECK(g_passed.size() < all.size());
    CHECK_EQ(held_flag(), 0u);                    // reading the status lets an open PATH3 drain
    CHECK(g_passed == all);
}

void many_packets_come_out_in_order_one_at_a_time() {
    fresh();
    std::vector<Bytes> packets;
    for (std::uint32_t i = 0; i < 60; ++i) packets.push_back(packet(i % 5, (i * 37) % 211 + 1, 0x10000ull * (i + 1)));
    gif::set_path3_mask(true);
    for (std::size_t i = 0; i < packets.size(); ++i) {
        send(packets[i]);
        if (i % 3 == 2) {                         // one is released while more keep arriving
            gif::set_path3_mask(false);
            gif::set_path3_mask(true);
        }
    }
    for (int i = 0; i < 40; ++i) {                // twenty went while sending; the other forty
        gif::set_path3_mask(false);
        gif::set_path3_mask(true);
    }
    Bytes stream;
    for (const Bytes &p : packets) append(stream, p);
    CHECK(g_passed == stream);
    CHECK_EQ(g_posts.size(), 60u);
    int wrong = 0;
    for (std::size_t i = 0; i < packets.size() && i < g_posts.size(); ++i)
        if (g_posts[i].qwords != packets[i].size() / 16) ++wrong;
    CHECK_EQ(wrong, 0);
}

// What is held has no upper limit, and an open PATH3 drains all of it at once. A message of the queue between
// the threads has one (queue::kMaxPayload), so a long drain goes out as several messages, cut between
// qwords, with every byte in its place.
void a_drain_longer_than_one_message_goes_out_in_pieces_in_order() {
    fresh();
    const std::uint32_t most = ps2::gs::queue::kMaxPayload / 16;
    Bytes all;
    gif::set_path3_mask(true);
    for (std::uint64_t i = 0; all.size() / 16 < 2 * most + 5000; ++i) {
        const Bytes p = packet(3, 9000 + static_cast<std::uint32_t>(i) * 7, 0x100000 * (i + 1));
        send(p);
        append(all, p);
    }
    gif::set_path3_mask(false);                   // the first packet
    const std::size_t first = g_posts.size();
    CHECK_EQ(first, 1u);
    CHECK_EQ(held_flag(), 0u);                    // the rest drains
    CHECK(g_passed == all);
    CHECK_EQ(g_posts.size(), first + 3);          // two full messages and the remainder
    std::uint64_t qwords = 0;
    int too_long = 0;
    for (const Post &post : g_posts) {
        qwords += post.qwords;
        if (post.qwords > most) ++too_long;
    }
    CHECK_EQ(too_long, 0);
    CHECK_EQ(qwords, all.size() / 16);
    const Bytes after = packet(1, 3, 0x7000);     // and the stream is still followed packet by packet
    gif::set_path3_mask(true);
    send(after);
    CHECK(g_passed == all);
    gif::set_path3_mask(false);
    append(all, after);
    CHECK(g_passed == all);
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

// The hardware would stall a game that sends on and on behind the mask; the runtime holds it all. When that
// passes 16 MB, far beyond the few megabytes of a frame's uploads, it says so, once.
void a_hold_that_keeps_growing_is_reported_once() {
    fresh();
    const Bytes p = packet(0, 0x7FFF, 0x5000);    // half a megabyte
    gif::set_path3_mask(true);
    printed();
    for (int i = 0; i < 32; ++i) send(p);         // 16 MB to the byte: not more
    CHECK(!has(printed(), "[gif]"));
    send(p);
    CHECK(has(printed(), "[gif] more than 16 MB of PATH3 data is held behind the mask"));
    for (int i = 0; i < 4; ++i) send(p);
    CHECK(!has(printed(), "[gif]"));
    CHECK(g_passed.empty());
    gif::reset();
}

void a_saved_state_carries_what_is_held_and_no_more() {
    fresh();
    const Bytes a = packet(0, 50, 0xA000), b = packet(2, 6, 0xB000), c = packet(0, 70, 0xC000);
    gif::set_path3_mask(true);
    send(a);
    send(b);
    send(c);
    gif::set_path3_mask(false);                   // a passes
    gif::set_path3_mask(true);
    ps2::state::Writer saved;
    gif::state(saved);
    CHECK(saved.out.size() < a.size() + b.size() + c.size());   // a is gone: it must not be in the state
    CHECK(saved.out.size() > b.size() + c.size());
    gif::set_path3_mask(false);
    gif::set_path3_mask(true);
    const Bytes after_b = g_passed;
    gif::reset();
    ps2::state::Reader reader(saved.out.data(), saved.out.size());
    gif::state(reader);
    g_passed.clear();
    gif::set_path3_mask(false);
    gif::set_path3_mask(true);
    CHECK(g_passed == b);
    gif::set_path3_mask(false);
    Bytes rest = b;
    append(rest, c);
    CHECK(g_passed == rest);
    CHECK(after_b.size() == a.size() + b.size());
}

}

int main() {
    char path[] = "/tmp/b3-test-gif-path3-XXXXXX";
    const int file = mkstemp(path);
    if (file < 0) return 2;
    g_log = path;
    dup2(file, 2);
    an_open_path3_passes_data_as_it_comes();
    a_masked_path3_holds_whole_packets_and_each_unmask_lets_one_through();
    a_packet_that_arrives_in_pieces_is_released_whole();
    a_packet_already_on_its_way_finishes_under_the_mask();
    an_open_path3_drains_everything_it_still_holds();
    many_packets_come_out_in_order_one_at_a_time();
    a_drain_longer_than_one_message_goes_out_in_pieces_in_order();
    a_hold_that_keeps_growing_is_reported_once();
    a_saved_state_carries_what_is_held_and_no_more();
    unlink(path);
    return check::done("test_gif_path3");
}
