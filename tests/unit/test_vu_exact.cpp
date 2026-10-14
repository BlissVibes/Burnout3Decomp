// The fork's exact VU arithmetic on host floats (ps2/vu_exact.hpp: add, sub, mul, the accumulating forms
// and the MAC flags, four lanes at a time) against the integer model it stands in for (ps2::fp::add, sub
// and mul of ps2/fpu.hpp, one lane at a time). The contract of every form is, lane by lane: the result has
// the model's bits, or the lane comes back set in the `bad` mask and the caller computes it with the model.
// Close is a failure here: raw bits are compared, +0 and -0 are different values, and a lane that is off by
// one unit in the last place counts the same as one that is off by a thousand.
//
// The operands are made up: a grid of edge values, enumerated corner cases of the adder and the
// multiplier, and random streams.
//
// usage: test_vu_exact [millions of lanes per form and random stream, default 1] [--full] [--seed N]
//   --full also puts every one of the 2^32 bit patterns through the times-one fold;
//   --seed N draws the random streams from another seed (the fixed stream keeps its own).
// With any argument the counts of every stream are printed. The "hash" lines come from one fixed stream
// and must be the same from every build of this test, native and WebAssembly.
//
// ps2/vu_exact.hpp is written with clang's vector builtins, as the fork is built; with another compiler
// this test reports that it was skipped.

#include "check.hpp"

#ifndef __clang__
int main() {
    std::puts("test_vu_exact: skipped (ps2/vu_exact.hpp needs clang)");
    return 0;
}
#else
#include "ps2/fpu.hpp"
#include "ps2/vu_exact.hpp"
#include "ps2/vu_ops.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace fp = ps2::fp;
namespace vuexact = ps2::vuexact;
namespace vuops = ps2::vuops;
using vuops::v4i;
using vuops::v4u;

namespace {

bool g_verbose = false;

// One form under test: the lanes compared since the stream began, the lanes whose bits were not the
// model's, the lanes handed back as bad, and a hash of what a caller ends up with.
struct Form {
    const char *name;
    std::uint64_t lanes = 0, wrong = 0, bad = 0, hash = 0;
    int shown = 0;

    void lane(std::uint32_t got, std::uint32_t bad_mask, std::uint32_t want, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        ++lanes;
        // A bad lane is computed with the model, so the model's value is what the caller has.
        const std::uint64_t outcome = (bad_mask ? want : got) | (static_cast<std::uint64_t>(bad_mask != 0) << 32);
        hash = (hash ^ outcome) * 0x9E3779B97F4A7C15ull;
        hash ^= hash >> 29;
        if (bad_mask) {
            ++bad;
            if (bad_mask == ~0u) return;   // a mask lane is all ones or all zeros
        } else if (got == want) {
            return;
        }
        ++wrong;
        if (shown++ < 4) std::printf("  %s: a %08X b %08X c %08X gives %08X (bad mask %08X), the model %08X\n", name, a, b, c, got, bad_mask, want);
    }
};

Form g_add{"add"}, g_sub{"sub"}, g_mul{"mul"}, g_mul1{"mul1"}, g_madd{"madd"}, g_msub{"msub"}, g_madd1{"madd1"}, g_msub1{"msub1"};
// The fold: a product with 1.0 is its other operand flushed, so MUL, MADD and MSUB by 1.0 need no multiply.
Form g_times_one{"times-one"}, g_madd_one{"madd-one"}, g_msub_one{"msub-one"};
// MAX, MINI and ABS do no arithmetic; vuops has had their four-lane forms since before this header.
Form g_max{"max"}, g_min{"min"}, g_abs{"abs"};
Form *const g_forms[] = {&g_add, &g_sub, &g_mul, &g_mul1, &g_madd, &g_msub, &g_madd1, &g_msub1, &g_times_one, &g_madd_one, &g_msub_one, &g_max, &g_min, &g_abs};

// Ends a stream: no lane of any form may be wrong. With no_fallback, no lane may have come back bad either:
// a form that hands ordinary numbers to the model is correct and silently slow.
void close(const char *stream, bool no_fallback = false) {
    for (Form *form : g_forms) {
        if (g_verbose || form->wrong || (no_fallback && form->bad))
            std::printf("  %-14s %-10s %12llu lanes, %llu wrong, %llu bad\n", stream, form->name, static_cast<unsigned long long>(form->lanes),
                        static_cast<unsigned long long>(form->wrong), static_cast<unsigned long long>(form->bad));
        CHECK_EQ(form->wrong, 0u);
        if (no_fallback) CHECK_EQ(form->bad, 0u);
        form->lanes = form->wrong = form->bad = 0;
        form->shown = 0;
    }
    if (g_verbose) std::fflush(stdout);
}

// Four operand triples, a lane each, through every form. The forms whose second operand is one value for
// all lanes (a broadcast lane, Q or I) run once with each of the four b.
void compare(const std::uint32_t *a, const std::uint32_t *b, const std::uint32_t *c) {
    v4u va, vb, vc, bad;
    std::memcpy(&va, a, 16);
    std::memcpy(&vb, b, 16);
    std::memcpy(&vc, c, 16);

    v4u r = vuexact::add(va, vb, bad);
    for (int i = 0; i < 4; ++i) g_add.lane(r[i], bad[i], fp::add(a[i], b[i]), a[i], b[i], 0);
    r = vuexact::sub(va, vb, bad);
    for (int i = 0; i < 4; ++i) g_sub.lane(r[i], bad[i], fp::sub(a[i], b[i]), a[i], b[i], 0);
    r = vuexact::mul(va, vb, bad);
    for (int i = 0; i < 4; ++i) g_mul.lane(r[i], bad[i], fp::mul(a[i], b[i]), a[i], b[i], 0);
    r = vuexact::madd(vc, va, vb, bad);
    for (int i = 0; i < 4; ++i) g_madd.lane(r[i], bad[i], fp::add(c[i], fp::mul(a[i], b[i])), a[i], b[i], c[i]);
    r = vuexact::msub(vc, va, vb, bad);
    for (int i = 0; i < 4; ++i) g_msub.lane(r[i], bad[i], fp::sub(c[i], fp::mul(a[i], b[i])), a[i], b[i], c[i]);

    for (int j = 0; j < 4; ++j) {
        std::uint32_t product[4];
        for (int i = 0; i < 4; ++i) product[i] = fp::mul(a[i], b[j]);
        r = vuexact::mul1(va, b[j], bad);
        for (int i = 0; i < 4; ++i) g_mul1.lane(r[i], bad[i], product[i], a[i], b[j], 0);
        r = vuexact::madd1(vc, va, b[j], bad);
        for (int i = 0; i < 4; ++i) g_madd1.lane(r[i], bad[i], fp::add(c[i], product[i]), a[i], b[j], c[i]);
        r = vuexact::msub1(vc, va, b[j], bad);
        for (int i = 0; i < 4; ++i) g_msub1.lane(r[i], bad[i], fp::sub(c[i], product[i]), a[i], b[j], c[i]);
    }

    r = vuexact::vflush(va);
    for (int i = 0; i < 4; ++i) g_times_one.lane(r[i], 0, fp::mul(a[i], fp::kOne), a[i], fp::kOne, 0);
    r = vuexact::add(vc, va, bad);
    for (int i = 0; i < 4; ++i) g_madd_one.lane(r[i], bad[i], fp::add(c[i], fp::mul(a[i], fp::kOne)), a[i], fp::kOne, c[i]);
    r = vuexact::sub(vc, va, bad);
    for (int i = 0; i < 4; ++i) g_msub_one.lane(r[i], bad[i], fp::sub(c[i], fp::mul(a[i], fp::kOne)), a[i], fp::kOne, c[i]);

    r = vuops::vmax(va, vb);
    for (int i = 0; i < 4; ++i) g_max.lane(r[i], 0, fp::max(a[i], b[i]), a[i], b[i], 0);
    r = vuops::vmin(va, vb);
    for (int i = 0; i < 4; ++i) g_min.lane(r[i], 0, fp::min(a[i], b[i]), a[i], b[i], 0);
    r = vuops::vabs(va);
    for (int i = 0; i < 4; ++i) g_abs.lane(r[i], 0, a[i] & fp::kAbs, a[i], 0, 0);
}

// Collects operand triples four at a time.
struct Batch {
    std::uint32_t a[4], b[4], c[4];
    int count = 0;

    void push(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
        a[count] = x;
        b[count] = y;
        c[count] = z;
        if (++count < 4) return;
        compare(a, b, c);
        count = 0;
    }
    void finish() {
        while (count) push(0, 0, 0);
    }
};

std::uint32_t make(std::uint32_t sign, std::uint32_t exponent, std::uint32_t mantissa) {
    return ((sign & 1u) << 31) | ((exponent & 0xFFu) << 23) | (mantissa & 0x7FFFFFu);
}

std::uint32_t exponent_in_range(int e) { return e < 0 ? 0u : e > 255 ? 255u : static_cast<std::uint32_t>(e); }

// 30 exponents (both ends of the range, around 1.0, and the distances at which an addend falls off the
// end of a 24-bit sum) x 8 mantissas x 2 signs.
std::vector<std::uint32_t> edge_values() {
    static const std::uint32_t exponents[] = {0,   1,   2,   3,   24,  25,  26,  27,  100, 101, 102, 123, 124, 125, 126,
                                              127, 128, 129, 130, 150, 151, 152, 153, 228, 229, 230, 252, 253, 254, 255};
    static const std::uint32_t mantissas[] = {0u, 1u, 2u, 0x3FFFFFu, 0x400000u, 0x400001u, 0x7FFFFEu, 0x7FFFFFu};
    std::vector<std::uint32_t> values;
    for (std::uint32_t sign = 0; sign < 2; ++sign)
        for (std::uint32_t exponent : exponents)
            for (std::uint32_t mantissa : mantissas) values.push_back(make(sign, exponent, mantissa));
    return values;
}

void every_pair_of_edge_values() {
    const std::vector<std::uint32_t> edge = edge_values();
    CHECK_EQ(edge.size(), 480u);
    Batch batch;
    for (std::size_t i = 0; i < edge.size(); ++i)
        for (std::size_t j = 0; j < edge.size(); ++j) batch.push(edge[i], edge[j], edge[(i * 31u + j * 17u + 5u) % edge.size()]);
    batch.finish();
    close("edge pairs");
}

void every_triple_of_a_subset() {
    static const std::uint32_t exponents[] = {0, 1, 2, 25, 26, 101, 125, 126, 127, 128, 129, 152, 229, 254, 255};
    std::vector<std::uint32_t> values;
    for (std::uint32_t sign = 0; sign < 2; ++sign)
        for (std::uint32_t exponent : exponents)
            for (std::uint32_t mantissa : {0u, 0x7FFFFFu}) values.push_back(make(sign, exponent, mantissa));
    CHECK_EQ(values.size(), 60u);
    Batch batch;
    for (std::uint32_t x : values)
        for (std::uint32_t y : values)
            for (std::uint32_t z : values) batch.push(x, y, z);
    batch.finish();
    close("edge triples");
}

// The adder keeps one bit below the last place of the larger operand and drops the rest of the smaller
// one. Here the smaller operand sits 0 to 40 binades below, and the bits of it that fall below the larger
// one's last place are exactly half a unit there (a tie for a rounding adder), just under half, just over,
// or nothing. Every combination of signs, both orders, and the same sum in the accumulating forms.
void sums_with_chosen_dropped_bits() {
    static const std::uint32_t larger_exponents[] = {3, 30, 127, 150, 254, 255};
    static const int differences[] = {0, 1, 2, 3, 22, 23, 24, 25, 26, 27, 28, 40};
    static const std::uint32_t mantissas[] = {0u, 1u, 0x2AAAAAu, 0x400000u, 0x555555u, 0x7FFFFEu, 0x7FFFFFu};
    Batch batch;
    for (std::uint32_t ea : larger_exponents)
        for (int difference : differences) {
            if (static_cast<int>(ea) < difference) continue;
            const std::uint32_t eb = ea - static_cast<std::uint32_t>(difference);
            for (std::uint32_t ma : mantissas)
                for (std::uint32_t base : mantissas)
                    for (int kind = 0; kind < 4; ++kind) {
                        std::uint32_t mb = base;
                        if (difference >= 1 && difference <= 23) {
                            const std::uint32_t half = 1u << (difference - 1), below = half - 1u;
                            mb &= ~(half | below);
                            if (kind == 0) mb |= half;        // exactly half a unit
                            if (kind == 1) mb |= below;       // just under
                            if (kind == 2) mb |= half | 1u;   // just over
                        } else if (kind) {
                            continue;   // nothing is dropped, or all of it from the hidden bit down: the mantissa as it is
                        }
                        for (std::uint32_t signs = 0; signs < 4; ++signs) {
                            const std::uint32_t x = make(signs, ea, ma), y = make(signs >> 1, eb, mb);
                            batch.push(x, y, x);
                            batch.push(y, x, y);
                            batch.push(y, fp::kOne, x);   // the product is y itself: x + y and x - y through MADD and MSUB
                        }
                    }
        }
    batch.finish();
    close("dropped bits");
}

// The product's exponent before the carry is ea + eb - 127: from 3 below the smallest a VU holds to 3 above
// the largest, with mantissas whose product does and does not carry into bit 47. The accumulator is the
// product negated (MADD cancels to nothing) or a neighbour of it.
void products_at_the_ends_of_the_range() {
    static const std::uint32_t mantissas[] = {0u, 1u, 0x3504F2u, 0x3504F3u, 0x3504F4u, 0x400000u, 0x7FFFFEu, 0x7FFFFFu};
    std::uint64_t carried = 0, straight = 0;
    Batch batch;
    for (int end = 0; end < 2; ++end)
        for (int sum = (end ? 379 : 124); sum <= (end ? 386 : 131); ++sum)
            for (int ea = 1; ea <= 255; ++ea) {
                const int eb = sum - ea;
                if (eb < 1 || eb > 255) continue;
                for (std::uint32_t ma : mantissas)
                    for (std::uint32_t mb : mantissas)
                        for (std::uint32_t signs = 0; signs < 4; ++signs) {
                            const std::uint32_t x = make(signs, static_cast<std::uint32_t>(ea), ma), y = make(signs >> 1, static_cast<std::uint32_t>(eb), mb);
                            if ((static_cast<std::uint64_t>(fp::mantissa(x)) * fp::mantissa(y)) >> 47) ++carried;
                            else ++straight;
                            const std::uint32_t neighbour = make(signs >> 1, exponent_in_range(sum - 126), 0x7FFFFFu);
                            batch.push(x, y, (ea & 1) ? fp::mul(x, y) ^ fp::kSign : neighbour);
                        }
            }
    batch.finish();
    CHECK(carried > 1000);
    CHECK(straight > 1000);
    close("product ends");
}

struct Lane {
    std::uint32_t value;
    bool bad;
};

Lane sum_of(std::uint32_t a, std::uint32_t b) {
    v4u bad;
    const v4u r = vuexact::add(vuops::vsplat(a), vuops::vsplat(b), bad);
    return {r[0], bad[0] != 0};
}
Lane difference_of(std::uint32_t a, std::uint32_t b) {
    v4u bad;
    const v4u r = vuexact::sub(vuops::vsplat(a), vuops::vsplat(b), bad);
    return {r[0], bad[0] != 0};
}
Lane product_of(std::uint32_t a, std::uint32_t b) {
    v4u bad;
    const v4u r = vuexact::mul(vuops::vsplat(a), vuops::vsplat(b), bad);
    return {r[0], bad[0] != 0};
}
Lane accumulated(std::uint32_t acc, std::uint32_t a, std::uint32_t b) {
    v4u bad;
    const v4u r = vuexact::madd(vuops::vsplat(acc), vuops::vsplat(a), vuops::vsplat(b), bad);
    return {r[0], bad[0] != 0};
}

// The contract of one lane.
bool agrees(Lane lane, std::uint32_t model) { return lane.bad || lane.value == model; }

void the_named_cases() {
    // A tie at the top of the range: the adder gives the neighbour toward zero, the host the even one.
    // (vuops::add gives the host's answer for this pair: a step of its two-sum overflows on the way.)
    CHECK_EQ(fp::add(0x73C00000u, 0xFF7FFFFFu), 0xFF7FFFFDu);
    const Lane tie = sum_of(0x73C00000u, 0xFF7FFFFFu);
    CHECK(!tie.bad);
    CHECK_EQ(tie.value, 0xFF7FFFFDu);

    // Adding and taking away 2^23 drops a fraction toward zero on a VU; the host would give the nearest integer.
    const std::uint32_t big = 0x4B000000u;
    for (std::uint32_t value : {0x3F000000u, 0x3F400000u, 0x3FC00000u, 0x40200000u, 0x40300000u, 0x40600000u}) {
        const Lane up = sum_of(value, big);
        CHECK(!up.bad);
        CHECK_EQ(up.value, fp::add(value, big));
        const Lane back = difference_of(up.value, big);
        CHECK(!back.bad);
        CHECK_EQ(back.value, fp::sub(up.value, big));
    }

    // The two zeros: none of these needs the model, and the sign of the zero that comes out is the model's.
    for (std::uint32_t signs = 0; signs < 4; ++signs) {
        const std::uint32_t a = make(signs, 0, 0), b = make(signs >> 1, 0, 0);
        for (std::uint32_t other : {b, make(signs >> 1, 0, 0x7FFFFFu), make(signs >> 1, 127, 0)}) {
            const Lane sum = sum_of(a, other), difference = difference_of(a, other), product = product_of(a, other), swapped = product_of(other, a);
            CHECK(!sum.bad && !difference.bad && !product.bad && !swapped.bad);
            CHECK_EQ(sum.value, fp::add(a, other));
            CHECK_EQ(difference.value, fp::sub(a, other));
            CHECK_EQ(product.value, fp::mul(a, other));
            CHECK_EQ(swapped.value, fp::mul(other, a));
        }
    }

    // x - x and x + -x are +0 for every x a host float can hold as a number.
    for (std::uint32_t x : edge_values()) {
        if (fp::exponent(x) == 255) continue;
        const Lane difference = difference_of(x, x), sum = sum_of(x, x ^ fp::kSign);
        CHECK(!difference.bad && !sum.bad);
        CHECK_EQ(difference.value, 0u);
        CHECK_EQ(sum.value, 0u);
    }

    // The top binade (exponent field 255) is a number on a VU and an infinity or a NaN on the host. The
    // model keeps the operand's bits; the form must not let the host's answer through.
    for (std::uint32_t top : {0x7FFFFFFFu, 0xFFFFFFFFu, 0x7F800000u, 0xFF800001u}) {
        for (std::uint32_t other : {0u, 0x80000000u, 0x3F800000u, 0xBF800000u, 0x7F7FFFFFu, top, top ^ fp::kSign}) {
            CHECK(agrees(sum_of(top, other), fp::add(top, other)));
            CHECK(agrees(sum_of(other, top), fp::add(other, top)));
            CHECK(agrees(difference_of(top, other), fp::sub(top, other)));
            CHECK(agrees(product_of(top, other), fp::mul(top, other)));
            CHECK(agrees(product_of(other, top), fp::mul(other, top)));
            CHECK(agrees(accumulated(top, other, other), fp::add(top, fp::mul(other, other))));
            CHECK(agrees(accumulated(other, top, other), fp::add(other, fp::mul(top, other))));
        }
    }

    // A sum that carries into exponent 255 (a number on a VU), and one that carries past it (the model
    // saturates): the host has an infinity for both.
    CHECK_EQ(fp::add(0x7F7FFFFFu, 0x7F7FFFFFu), 0x7FFFFFFFu);
    CHECK(agrees(sum_of(0x7F7FFFFFu, 0x7F7FFFFFu), 0x7FFFFFFFu));
    CHECK_EQ(fp::add(0x7F000000u, 0x7F000000u), 0x7F800000u);
    CHECK(agrees(sum_of(0x7F000000u, 0x7F000000u), 0x7F800000u));
    CHECK_EQ(fp::add(0xFFC00000u, 0xFFC00000u), 0xFFFFFFFFu);
    CHECK(agrees(sum_of(0xFFC00000u, 0xFFC00000u), 0xFFFFFFFFu));

    // The one product a host float rounds the wrong way: one step of the 24-bit product under 2^-126. The
    // model gives zero; narrowing the double rounds it up to the smallest normal number.
    const std::uint32_t just_under = make(0, 100, 0x7FFFFFu), power = make(1, 27, 0);
    CHECK_EQ(fp::mul(just_under, power), 0x80000000u);
    CHECK(agrees(product_of(just_under, power), 0x80000000u));
    CHECK(agrees(product_of(power, just_under), 0x80000000u));
    // The neighbours on both sides are ordinary: 2^-126 itself, and two steps under it.
    CHECK_EQ(fp::mul(make(0, 100, 0), make(0, 28, 0)), 0x00800000u);
    CHECK(agrees(product_of(make(0, 100, 0), make(0, 28, 0)), 0x00800000u));
    CHECK_EQ(fp::mul(make(0, 100, 0x7FFFFEu), make(0, 27, 0)), 0u);
    CHECK(agrees(product_of(make(0, 100, 0x7FFFFEu), make(0, 27, 0)), 0u));

    // A multiply-add whose product is itself out of the ordinary: too small (a zero is added), the one
    // above, in the top binade, and past it (the saturated product is added, and saturates again).
    const std::uint32_t one = fp::kOne;
    CHECK(agrees(accumulated(one, make(0, 60, 0x123456u), make(0, 60, 0x654321u)), fp::add(one, fp::mul(make(0, 60, 0x123456u), make(0, 60, 0x654321u)))));
    CHECK_EQ(fp::add(one, fp::mul(make(0, 60, 0x123456u), make(0, 60, 0x654321u))), one);
    CHECK(agrees(accumulated(0x00800000u, just_under, power), fp::add(0x00800000u, fp::mul(just_under, power))));
    CHECK_EQ(fp::add(0x00800000u, fp::mul(just_under, power)), 0x00800000u);
    CHECK(agrees(accumulated(0xFF7FFFFFu, make(0, 191, 0), make(0, 191, 0x7FFFFFu)), fp::add(0xFF7FFFFFu, fp::mul(make(0, 191, 0), make(0, 191, 0x7FFFFFu)))));
    CHECK(agrees(accumulated(0xFF7FFFFFu, make(0, 200, 0), make(0, 200, 0)), fp::add(0xFF7FFFFFu, fp::mul(make(0, 200, 0), make(0, 200, 0)))));
    CHECK_EQ(fp::mul(make(0, 200, 0), make(0, 200, 0)), 0x7FFFFFFFu);
}

// MAC as the exact loop computes it. For each lane the instruction writes, x in bit 3 down to w in bit 0:
// zero (exponent field 0) in the low four bits, sign in the next four, overflow (every magnitude bit set)
// in the top four. The underflow bits are never set.
std::uint32_t mac_rule(const std::uint32_t *lanes, unsigned dest) {
    std::uint32_t mac = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (!(dest & (8u >> i))) continue;
        const unsigned bit = 3u - i;
        if (!fp::exponent(lanes[i])) mac |= 0x0001u << bit;
        if (lanes[i] & fp::kSign) mac |= 0x0010u << bit;
        if ((lanes[i] & fp::kAbs) == fp::kAbs) mac |= 0x1000u << bit;
    }
    return mac;
}

std::uint32_t next();

void the_mac_flags_follow_the_scalar_rule() {
    static const std::uint32_t values[] = {0u,          0x80000000u, 1u,          0x007FFFFFu, 0x807FFFFFu, 0x00800000u,
                                           0x3F800000u, 0xBF800000u, 0x7F800000u, 0x7FFFFFFEu, 0x7FFFFFFFu, 0xFFFFFFFFu};
    std::uint64_t wrong = 0, compared = 0;
    const auto one = [&](const std::uint32_t *lanes) {
        v4u v;
        std::memcpy(&v, lanes, 16);
        for (unsigned dest = 0; dest < 16; ++dest) {
            ++compared;
            if (vuexact::mac(v, dest * 0x1111u) != mac_rule(lanes, dest)) ++wrong;
        }
    };
    for (std::uint32_t x : values)
        for (std::uint32_t y : values)
            for (std::uint32_t z : values)
                for (std::uint32_t w : values) {
                    const std::uint32_t lanes[4] = {x, y, z, w};
                    one(lanes);
                }
    for (int i = 0; i < 100000; ++i) {
        const std::uint32_t lanes[4] = {next(), next(), next(), next()};
        one(lanes);
    }
    CHECK_EQ(compared, (20736u + 100000u) * 16u);
    CHECK_EQ(wrong, 0u);

    // The sign bits of four lanes as a number, lane 0 in bit 0, whatever the other bits are.
    for (unsigned pattern = 0; pattern < 16; ++pattern) {
        v4u v;
        for (unsigned i = 0; i < 4; ++i) v[i] = (next() & fp::kAbs) | ((pattern >> i) & 1u ? fp::kSign : 0u);
        CHECK_EQ(vuexact::vbits((v4i)v), pattern);
    }
}

// --full: fp::mul(s, 1.0) is s flushed for every s there is.
void every_bit_pattern_times_one() {
    std::uint64_t wrong = 0;
    for (std::uint64_t base = 0; base < 0x100000000ull; base += 4) {
        const std::uint32_t s = static_cast<std::uint32_t>(base);
        const v4u flushed = vuexact::vflush(v4u{s, s + 1u, s + 2u, s + 3u});
        for (std::uint32_t i = 0; i < 4; ++i)
            if (flushed[i] != fp::mul(s + i, fp::kOne)) ++wrong;
    }
    if (g_verbose || wrong) std::printf("  every pattern  times-one    4294967296 lanes, %llu wrong\n", static_cast<unsigned long long>(wrong));
    CHECK_EQ(wrong, 0u);
}

std::uint64_t g_state = 1;

void seed(std::uint64_t value) {
    g_state = (value + 1u) * 0x9E3779B97F4A7C15ull;
    if (!g_state) g_state = 1;
}

// xorshift64; the top half is the better half.
std::uint32_t next() {
    g_state ^= g_state << 13;
    g_state ^= g_state >> 7;
    g_state ^= g_state << 17;
    return static_cast<std::uint32_t>(g_state >> 32);
}

// A mantissa that is often short or ends in a run: random bits above a random position and, below it,
// nothing, all ones, the top bit alone (half a unit of that position) or the bottom bit alone. Sums and
// products of such values land on and beside the rounding boundaries far more often than those of random
// mantissas, which get the other half of the draws.
std::uint32_t shaped(std::uint32_t r) {
    const std::uint32_t m = r & 0x7FFFFFu, below = (1u << (((r >> 23) & 63u) % 24u)) - 1u;
    switch (r >> 29) {
    case 0: return m & ~below;
    case 1: return m | below;
    case 2: return (m & ~below) | ((below + 1u) >> 1);
    case 3: return (m & ~below) | (below ? 1u : 0u);
    default: return m;
    }
}

enum Stream { kAnyBits, kNear, kEnds, kTypical };

std::uint32_t end_exponent(std::uint32_t r) { return (r & 8u) ? r % 5u : 251u + r % 5u; }

// What a game computes with: exponents 90 to 165, and one value in sixteen a zero of either sign.
std::uint32_t typical(std::uint32_t r) { return (r & 15u) ? make(r >> 31, 90u + (r >> 4) % 76u, shaped(next())) : (r & fp::kSign); }

void draw(Stream stream, std::uint32_t &a, std::uint32_t &b, std::uint32_t &c) {
    const std::uint32_t r = next();
    switch (stream) {
    case kAnyBits:
        a = r;
        b = next();
        c = next();
        break;
    case kNear: {
        // b within 27 binades of a, and the accumulator within 27 of their product: every sum rounds.
        const std::uint32_t ea = 100u + r % 56u, eb = ea - 27u + (r >> 6) % 55u, ec = ea + eb - 154u + (r >> 12) % 55u;
        a = make(r >> 29, ea, shaped(next()));
        b = make(r >> 30, eb, shaped(next()));
        c = make(r >> 31, ec, shaped(next()));
        break;
    }
    case kEnds: {
        // Exponents at the two ends of the range; or a product that lands on the bottom or the top of it;
        // or two numbers of the top binades, whose sum carries into exponent 255 and past it.
        int ea, eb, ec;
        switch ((r >> 4) & 3u) {
        case 0:
            ea = static_cast<int>(end_exponent(r >> 6));
            eb = static_cast<int>(end_exponent(r >> 12));
            ec = static_cast<int>(end_exponent(r >> 18));
            break;
        case 1:
            ea = 1 + static_cast<int>((r >> 6) % 123u);
            eb = 124 + static_cast<int>((r >> 14) % 8u) - ea;
            ec = ea + eb - 129 + static_cast<int>((r >> 18) % 6u);
            break;
        case 2:
            ea = 128 + static_cast<int>((r >> 6) % 128u);
            eb = 379 + static_cast<int>((r >> 14) % 8u) - ea;
            ec = ea + eb - 129 + static_cast<int>((r >> 18) % 6u);
            break;
        default:
            ea = 250 + static_cast<int>((r >> 6) % 6u);
            eb = ea - static_cast<int>((r >> 10) % 4u);
            ec = 250 + static_cast<int>((r >> 14) % 6u);
            break;
        }
        a = make(r >> 29, exponent_in_range(ea), shaped(next()));
        b = make(r >> 30, exponent_in_range(eb), shaped(next()));
        c = make(r >> 31, exponent_in_range(ec), shaped(next()));
        break;
    }
    case kTypical:
        a = typical(r);
        b = typical(next());
        c = typical(next());
        break;
    }
}

void random_stream(const char *name, Stream stream, std::uint64_t lanes, bool no_fallback) {
    for (std::uint64_t done = 0; done < lanes; done += 4) {
        std::uint32_t a[4], b[4], c[4];
        for (int i = 0; i < 4; ++i) draw(stream, a[i], b[i], c[i]);
        compare(a, b, c);
    }
    close(name, no_fallback);
}

// Ten million triples from one seed, a vector from each kind of stream in turn, and a hash per form of
// what a caller ends up with (the value, and whether the lane went to the model). Two builds that print
// the same hashes drew the same operands, handed back the same lanes and got the same bits.
void the_fixed_stream_and_its_hashes() {
    seed(0x5EED);
    for (Form *form : g_forms) form->hash = 0;
    for (std::uint32_t vector = 0; vector < 2500000u; ++vector) {
        std::uint32_t a[4], b[4], c[4];
        for (int i = 0; i < 4; ++i) draw(static_cast<Stream>(vector & 3u), a[i], b[i], c[i]);
        compare(a, b, c);
    }
    for (const Form *form : g_forms) std::printf("test_vu_exact: hash %-10s %016llX\n", form->name, static_cast<unsigned long long>(form->hash));
    close("fixed stream");
}

}

int main(int argc, char **argv) {
    std::uint64_t millions = 1, random_seed = 0;
    bool full = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--full")) full = true;
        else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) random_seed = std::strtoull(argv[++i], nullptr, 0);
        else millions = std::strtoull(argv[i], nullptr, 10);
    }
    g_verbose = argc > 1;

    the_named_cases();
    seed(0xF1A6);
    the_mac_flags_follow_the_scalar_rule();
    every_pair_of_edge_values();
    every_triple_of_a_subset();
    sums_with_chosen_dropped_bits();
    products_at_the_ends_of_the_range();
    the_fixed_stream_and_its_hashes();

    seed(random_seed);
    const std::uint64_t lanes = millions * 1000000u;
    random_stream("any bits", kAnyBits, lanes, false);
    random_stream("near", kNear, lanes, false);
    random_stream("range ends", kEnds, lanes, false);
    random_stream("typical", kTypical, lanes, true);
    if (full) every_bit_pattern_times_one();
    return check::done("test_vu_exact");
}
#endif
