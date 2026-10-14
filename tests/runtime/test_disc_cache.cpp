// The page's disc chunk cache alone (the fork's host/disc_cache.cpp): which slot holds which chunk, and
// which one is given up. The browser side is stood in for by fetch_all(), which "loads" every wanted
// chunk at once and marks its data with the chunk's number. The one rule that matters most: the slot
// handed back for a read holds the chunk that was asked for.
//
// fork-source: host/disc_cache.cpp

#include "check.hpp"

#include "disc_cache.hpp"

#include <cstring>
#include <memory>

namespace {

using ps2web::DiscCache, ps2web::Slot, ps2web::kChunk, ps2web::kSlots, ps2web::kReadAhead;

constexpr std::uint64_t kDisc = 20000ull * kChunk + 1000;   // 20,000 whole chunks and a short last one

std::unique_ptr<DiscCache> fresh() {
    auto c = std::make_unique<DiscCache>();
    c->disc_size = kDisc;
    return c;
}

// What the host does between a read and its answer: every wanted chunk arrives.
void fetch_all(DiscCache &c) {
    for (Slot &s : c.slots)
        if (s.state.load() == ps2web::kWanted) {
            std::memcpy(s.data, &s.index, sizeof s.index);
            s.state.store(ps2web::kReady);
        }
    c.asked = false;
}

std::uint64_t mark_of(const Slot &s) {
    std::uint64_t mark;
    std::memcpy(&mark, s.data, sizeof mark);
    return mark;
}

// One chunk read as host/web.cpp makes it: ask, let the fetches arrive, take the slot.
Slot *read(DiscCache &c, std::uint64_t index) {
    Slot *s = c.read(index);
    fetch_all(c);
    return s;
}

bool cached(DiscCache &c, std::uint64_t index) { return c.find(index) != nullptr; }

// Fills every slot with chunks that are not neighbours (so nothing is read ahead), `first` the oldest.
void fill(DiscCache &c, std::uint64_t first) {
    read(c, first);
    for (std::uint32_t i = 1; i < kSlots; ++i) read(c, 5000 + 2ull * i);
}

void a_read_returns_the_chunk_asked_for() {
    auto c = fresh();
    Slot *s = read(*c, 7);
    CHECK(s != nullptr);
    CHECK_EQ(s->index, 7u);
    CHECK_EQ(mark_of(*s), 7u);
    CHECK_EQ(s->size, static_cast<std::uint32_t>(kChunk));
    CHECK(read(*c, 7) == s);                      // and again from the cache
}

void reading_forwards_asks_ahead() {
    auto c = fresh();
    read(*c, 100);
    CHECK(!cached(*c, 101));                      // one read is not a run yet
    read(*c, 101);
    CHECK(cached(*c, 102));
    CHECK(cached(*c, 103));                       // two more per chunk of the run
    CHECK(!cached(*c, 104));
    for (std::uint64_t i = 102; i < 120; ++i) read(*c, i);
    CHECK(cached(*c, 119 + kReadAhead));          // never more than kReadAhead
    CHECK(!cached(*c, 119 + kReadAhead + 1));
    read(*c, 4000);                               // a seek ends the run
    CHECK(!cached(*c, 4001));
}

void the_end_of_the_disc() {
    auto c = fresh();
    Slot *last = read(*c, 20000);
    CHECK(last != nullptr);
    CHECK_EQ(last->size, 1000u);
    CHECK(read(*c, 20001) == nullptr);
    read(*c, 19999);
    read(*c, 20000);                              // a run up to the end asks for nothing past it
    CHECK(!cached(*c, 20001));
}

void the_chunk_being_read_is_not_given_to_its_own_read_ahead() {
    // The case that returned wrong disc data: the cache is full, chunk 1000 is cached and old, 1001 is
    // not cached. The game reads 999 and goes on into 1000; asking ahead for 1001 needs a slot.
    auto c = fresh();
    read(*c, 9000);                               // the oldest, to be given up for 999
    read(*c, 1000);
    for (std::uint32_t i = 2; i < kSlots; ++i) read(*c, 5000 + 2ull * i);
    read(*c, 999);
    Slot *s = read(*c, 1000);
    CHECK(s != nullptr);
    CHECK_EQ(s ? s->index : 0, 1000u);
    CHECK_EQ(s ? mark_of(*s) : 0, 1000u);
    CHECK(cached(*c, 1001));                      // the read-ahead took another slot
}

void a_chunk_just_read_is_the_last_to_be_given_up() {
    auto c = fresh();
    fill(*c, 300);                                // 300 is the oldest
    read(*c, 300);                                // read again: now the newest
    for (std::uint32_t i = 0; i < kSlots - 1; ++i) read(*c, 12000 + 2ull * i);   // all others are replaced
    CHECK(cached(*c, 300));
    read(*c, 14000);
    CHECK(!cached(*c, 300));
}

void every_read_of_a_long_mixed_session_gets_its_own_chunk() {
    // Files read front to back, re-reads of earlier files, short reads at random places: enough to fill
    // the cache many times over. Counts reads that came back with another chunk.
    auto c = fresh();
    std::uint64_t seed = 0x9E3779B97F4A7C15ull, wrong = 0, reads = 0;
    auto next = [&seed](std::uint64_t below) {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        return seed % below;
    };
    std::uint64_t files[12];
    for (std::uint64_t &f : files) f = next(19000);
    for (int load = 0; load < 6000; ++load) {
        const std::uint64_t kind = next(10);
        const std::uint64_t start = kind < 4 ? files[next(12)] : next(19800);
        const std::uint64_t length = kind < 8 ? 1 + next(120) : 1 + next(3);
        for (std::uint64_t i = 0; i < length && start + i <= 20000; ++i) {
            Slot *s = read(*c, start + i);
            ++reads;
            if (!s || s->index != start + i || mark_of(*s) != start + i) ++wrong;
        }
    }
    CHECK(reads > 100000);
    CHECK_EQ(wrong, 0u);
}

void with_every_slot_in_flight_a_new_chunk_has_to_wait() {
    auto c = fresh();
    for (std::uint32_t i = 0; i < kSlots; ++i) c->read(100 + 2ull * i);   // nothing arrives
    CHECK(c->read(9000) == nullptr);
    CHECK(c->read(100) != nullptr);               // one already on its way is found
    fetch_all(*c);
    Slot *s = read(*c, 9000);
    CHECK(s != nullptr);
    CHECK_EQ(mark_of(*s), 9000u);
}

void read_ahead_never_takes_the_only_ready_slot_when_it_is_the_one_being_read() {
    // Everything else is still loading, so the chunk being read is the only slot that could be given up.
    auto c = fresh();
    Slot *first = read(*c, 50);
    for (std::uint32_t i = 1; i < kSlots; ++i) c->read(200 + 2ull * i);   // in flight
    c->last_chunk = 49;                           // as if the game had been reading forwards up to here
    c->run = 3;
    Slot *s = c->read(50);                        // asks ahead for 51 to 58: no slot is free for them
    CHECK(s == first);
    CHECK_EQ(first->index, 50u);
    CHECK_EQ(mark_of(*first), 50u);
    CHECK(!cached(*c, 51));
}

}

int main() {
    a_read_returns_the_chunk_asked_for();
    reading_forwards_asks_ahead();
    the_end_of_the_disc();
    the_chunk_being_read_is_not_given_to_its_own_read_ahead();
    a_chunk_just_read_is_the_last_to_be_given_up();
    every_read_of_a_long_mixed_session_gets_its_own_chunk();
    with_every_slot_in_flight_a_new_chunk_has_to_wait();
    read_ahead_never_takes_the_only_ready_slot_when_it_is_the_one_being_read();
    return check::done("test_disc_cache");
}
