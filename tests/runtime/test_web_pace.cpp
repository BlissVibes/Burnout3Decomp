// The page's pacing rules alone (the fork's host/pace.cpp): how long a present may wait for the display,
// and when the audio device counts as stopped. The browser is stood in for by play(), a small model of
// the three parties as host/web.cpp connects them: the emulation thread (one frame, then a present is
// posted; it sleeps while more than 80 ms of sound is queued and stops while more than two presents are
// not yet served), the main thread (some work per frame, then the wait before the present) and the
// display (a refresh shows the last frame presented and ends the wait).
//
// fork-source: host/pace.cpp

#include "check.hpp"

#include "pace.hpp"

#include <algorithm>
#include <cmath>

namespace {

using ps2web::AudioStall, ps2web::PresentGate, ps2web::kUntilShown;

constexpr double kPeriod = 1000.0 / 59.94;   // one frame of the game, in ms

struct Setup {
    double display_hz = 60.0;
    double work_ms = 5.0;       // main thread work per frame
    double seconds = 10.0;
    bool wait_always = false;   // the rule before the gate: every present waits until the display has shown the last one
    bool starved = false;       // the browser draws only while the main thread is not working (a saturated main thread)
    double hitch_at = -1.0;     // the main thread does nothing for 150 ms from here
};

struct Outcome {
    double fps = 0.0;        // frames the game ran per second, over the last half of the run
    double dry_ms = 0.0;     // sound played from an empty queue, over the last half of the run
    int unseen = 0;          // frames presented and replaced before the display showed them
    int worst_run = 0;       // most presents in a row that did not wait for the display
};

Outcome play(const Setup &s) {
    const double dt = 0.01, end = s.seconds * 1000.0, refresh = 1000.0 / s.display_hz;
    PresentGate gate;
    Outcome out;
    double audio = 80.0, frame_ends = -1.0;                  // emulation thread
    int posted = 0, presented = 0, frames_late = 0;
    enum { Idle, Working, Waiting } state = Idle;            // main thread
    double work_ends = 0.0, wait_ends = 0.0;
    int since_skip = 0, run = 0;
    bool pending = false, refresh_due = false;               // display
    double next_refresh = refresh * 0.37;

    const auto present = [&](double now, bool last_shown) {
        if (pending) {
            ++out.unseen;
            out.worst_run = std::max(out.worst_run, ++run);
        } else {
            run = 0;
        }
        gate.presented(now, last_shown);
        ++presented;
        pending = true;
        state = Idle;
    };

    for (double t = 0.0; t < end; t += dt) {
        const bool late_half = t >= end / 2.0;
        audio -= dt;
        if (audio < 0.0) {
            if (late_half) out.dry_ms -= audio;
            audio = 0.0;
        }
        if (frame_ends < 0.0 && audio <= 80.0 && posted - presented <= 2) frame_ends = t + 6.0;
        if (frame_ends >= 0.0 && t >= frame_ends) {
            audio += kPeriod;
            ++posted;
            if (late_half) ++frames_late;
            frame_ends = -1.0;
        }

        const bool frozen = s.hitch_at >= 0.0 && t >= s.hitch_at && t < s.hitch_at + 150.0;
        if (t >= next_refresh) {
            next_refresh += refresh;
            refresh_due = true;
        }
        if (refresh_due && !frozen && !(s.starved && state == Working)) {
            refresh_due = false;
            pending = false;
        }
        if (frozen) continue;

        if (state == Idle) {
            if (posted > presented) {
                state = Working;
                work_ends = t + s.work_ms;
            } else {
                gate.queue_empty();
            }
        } else if (state == Working && t >= work_ends) {
            // The older rule of host/web.cpp: one present in nine does not wait while sound runs short.
            if (++since_skip > 8 && audio < 40.0 && (s.wait_always || gate.unshown < PresentGate::kMaxUnshown)) {
                since_skip = 0;
                present(t, false);
            } else if (!pending) {
                present(t, true);
            } else {
                const double limit = s.wait_always ? kUntilShown : gate.wait_limit(t, kPeriod);
                if (limit <= 0.0) {
                    present(t, false);
                } else {
                    state = Waiting;
                    wait_ends = t + std::ceil(limit);   // the page's timer counts whole milliseconds
                }
            }
        } else if (state == Waiting) {
            if (!pending) present(t, true);
            else if (t >= wait_ends) present(t, false);
        }
    }
    out.fps = frames_late / (s.seconds / 2.0);
    return out;
}

bool full_speed(const Outcome &o) { return o.fps > 59.94 * 0.995 && o.fps < 59.94 * 1.005; }

void the_wait_ends_one_game_frame_after_the_last_present() {
    PresentGate gate;
    gate.presented(1000.0, true);
    CHECK(gate.wait_limit(1004.0, kPeriod) > 12.0 && gate.wait_limit(1004.0, kPeriod) < 13.0);
    gate.queue_empty();
    CHECK(gate.wait_limit(1000.0 + kPeriod + 1.0, kPeriod) == 0.0);
}

void a_main_thread_that_is_behind_waits_for_the_display() {
    PresentGate gate;
    gate.presented(1000.0, true);   // nothing emptied the queue since, and a whole frame has passed
    CHECK(gate.wait_limit(1000.0 + kPeriod + 1.0, kPeriod) == kUntilShown);
}

void no_two_presents_in_a_row_go_ahead_without_the_display() {
    PresentGate gate;
    gate.presented(1000.0, true);
    gate.presented(1017.0, false);
    gate.queue_empty();
    CHECK(gate.wait_limit(1021.0, kPeriod) == kUntilShown);
    CHECK(gate.wait_limit(1040.0, kPeriod) == kUntilShown);
    gate.presented(1040.0, true);
    CHECK(gate.wait_limit(1044.0, kPeriod) < kUntilShown);
}

void a_fast_display_is_left_alone() {
    for (const double hz : {59.94, 60.0, 75.0, 100.0, 144.0}) {
        Setup s;
        s.display_hz = hz;
        s.seconds = 30.0;
        const Outcome now = play(s);
        s.wait_always = true;
        const Outcome before = play(s);
        CHECK(full_speed(now));
        CHECK_EQ(now.unseen, 0);
        CHECK(now.fps == before.fps && now.dry_ms == before.dry_ms);
    }
}

void a_slow_display_does_not_slow_the_game() {
    for (const double hz : {50.0, 48.0, 30.0}) {
        Setup s;
        s.display_hz = hz;
        s.wait_always = true;
        const Outcome before = play(s);
        CHECK(before.fps < 59.94 * 0.95);   // the defect: the game follows the display
        CHECK(before.dry_ms > 100.0);
        s.wait_always = false;
        const Outcome now = play(s);
        CHECK(full_speed(now));
        CHECK(now.dry_ms < 1.0);
        CHECK(now.worst_run <= 1);
    }
}

void a_backlog_on_a_slow_display_drains() {
    Setup s;
    s.display_hz = 50.0;
    s.hitch_at = 2000.0;
    const Outcome now = play(s);
    CHECK(full_speed(now));
    CHECK(now.dry_ms < 1.0);
}

void a_saturated_main_thread_still_shows_every_frame() {
    Setup s;
    s.display_hz = 100.0;
    s.work_ms = 18.0;
    s.starved = true;
    const Outcome now = play(s);
    CHECK_EQ(now.unseen, 0);
}

void at_least_every_second_frame_is_shown() {
    Setup s;
    s.display_hz = 20.0;
    const Outcome now = play(s);
    CHECK(now.unseen > 0);
    CHECK_EQ(now.worst_run, 1);
}

void a_device_that_takes_samples_never_counts_as_stopped() {
    std::uint32_t read = 5000;
    AudioStall stall(read);
    for (int ms = 0; ms < 5000; ++ms) {
        if (ms % 3 == 0) read += 128;   // the worklet takes 128 samples every 2.67 ms
        CHECK(!stall.stopped(read));
    }
    // A device that takes samples in bursts 100 ms apart is slow, not stopped.
    for (int ms = 0; ms < 5000; ++ms) {
        if (ms % 100 == 0) read += 4800;
        CHECK(!stall.stopped(read));
    }
}

void a_device_that_stops_is_noticed_after_200_ms() {
    AudioStall stall(5000);
    for (int ms = 1; ms < AudioStall::kLooks; ++ms) CHECK(!stall.stopped(5000));
    CHECK(stall.stopped(5000));
    CHECK_EQ(AudioStall::kLooks, 200);
}

}

int main() {
    the_wait_ends_one_game_frame_after_the_last_present();
    a_main_thread_that_is_behind_waits_for_the_display();
    no_two_presents_in_a_row_go_ahead_without_the_display();
    a_fast_display_is_left_alone();
    a_slow_display_does_not_slow_the_game();
    a_backlog_on_a_slow_display_drains();
    a_saturated_main_thread_still_shows_every_frame();
    at_least_every_second_frame_is_shown();
    a_device_that_takes_samples_never_counts_as_stopped();
    a_device_that_stops_is_noticed_after_200_ms();
    return check::done("test_web_pace");
}
