// path_extension_after_dot (0x002934D0): every edge case of the specification with synthetic strings.
//
// Expected values are reasoned from the specification, never taken from the game: the scan runs
// from the terminator back towards the start, the first '.' met wins and yields the pointer just
// past it, and the walk stops on reaching index 0 without examining it, so a dot at index 0 is
// invisible and the start pointer comes back. Results are compared as offsets from the start of
// each buffer so a failure prints something readable.

#include "check.hpp"
#include "hooks/audio/path_extension_after_dot.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

using b3::audio::path_extension_after_dot;

namespace {

// Offset of the result from the start of `s`.
std::ptrdiff_t offset_of_result(const char *s) { return path_extension_after_dot(s) - s; }

void simple_cases() {
    CHECK_EQ(offset_of_result("a.b"), 2);          // the dot at index 1 is found, result points at "b"
    CHECK_EQ(offset_of_result("name.ext"), 5);     // "ext"
    CHECK_EQ(offset_of_result("noext"), 0);        // no dot at all: start pointer
    CHECK_EQ(offset_of_result("x"), 0);            // one byte, never examined on the way back
    CHECK_EQ(offset_of_result("xy"), 0);           // two bytes, neither a dot
    CHECK_EQ(offset_of_result("x."), 2);           // dot at index 1: the result is the terminator
    const char *result = path_extension_after_dot("x.");
    CHECK(*result == '\0');                        // an empty extension string
}

void last_dot_wins() {
    CHECK_EQ(offset_of_result("foo.tar.gz"), 8);   // "gz", not "tar.gz"
    CHECK_EQ(offset_of_result("a.b.c.d"), 6);      // "d"
    CHECK_EQ(offset_of_result("..a"), 2);          // the dot at index 1 is met first on the way back
    CHECK_EQ(offset_of_result("a..b"), 3);         // "b"
    CHECK_EQ(offset_of_result("a.."), 3);          // trailing double dot: terminator again
}

void first_byte_is_never_examined() {
    CHECK_EQ(offset_of_result(".hidden"), 0);      // the only dot is at index 0: not found
    CHECK_EQ(offset_of_result("."), 0);            // same for a lone dot
    CHECK_EQ(offset_of_result(".a"), 0);           // index 1 is 'a', index 0 is skipped
    CHECK_EQ(offset_of_result(".."), 2);           // index 1 is a dot and is found; index 0 is not needed
    CHECK_EQ(offset_of_result(".a.b"), 3);         // the last dot (index 2) wins anyway
}

void trailing_dot_gives_the_terminator() {
    const char text[] = "name.";
    const char *result = path_extension_after_dot(text);
    CHECK_EQ(result - text, 5);
    CHECK(result == text + std::strlen(text));
}

void paths_with_directory_dots() {
    // A dot in a directory part is still a dot: the retail function knows nothing about separators.
    CHECK_EQ(offset_of_result("dir.d/file"), 4);   // "d/file"
    CHECK_EQ(offset_of_result("../Data/Config/Sample.cfg"), 22);    // "cfg" after the last dot at index 21
    CHECK_EQ(offset_of_result("..\\sound\\engine.wav"), 16);
    CHECK_EQ(offset_of_result("HOST0:tracks/01/ambient.rws"), 24);
}

void only_byte_0x2e_matches_and_only_zero_terminates() {
    // Bytes 0x80 and above: negative as signed bytes, never equal to '.', and they do not terminate.
    {
        char text[] = {'a', static_cast<char>(0xAE), 'b', '\0'};   // 0xAE is '.' with the top bit set
        CHECK_EQ(path_extension_after_dot(text) - text, 0);
    }
    {
        char text[] = {'a', '.', static_cast<char>(0xFF), static_cast<char>(0x80), 'b', '\0'};
        CHECK_EQ(path_extension_after_dot(text) - text, 2);        // the walk passes the high bytes
    }
    {
        char text[] = {'a', static_cast<char>(0x2E), '\0'};         // literally 46: matches
        CHECK_EQ(path_extension_after_dot(text) - text, 2);
    }
    // Control bytes and spaces are ordinary non-dot, non-terminating bytes.
    {
        char text[] = {'a', '.', '\t', ' ', '\x01', '\0'};
        CHECK_EQ(path_extension_after_dot(text) - text, 2);
    }
    // The scan stops at the first zero byte: whatever follows is never seen.
    {
        char text[] = {'a', 'b', '\0', '.', 'c', '\0'};
        CHECK_EQ(path_extension_after_dot(text) - text, 0);        // "ab" has no dot
    }
    {
        char text[] = {'a', '.', 'b', '\0', '.', 'z', '\0'};
        CHECK_EQ(path_extension_after_dot(text) - text, 2);        // "a.b" -> "b"
    }
}

void empty_string_is_the_documented_deviation() {
    // Retail would read before the buffer; our version returns the start pointer (see the header).
    const char empty[] = "";
    CHECK(path_extension_after_dot(empty) == empty);
}

void long_strings() {
    // A dot far from the end and a dot near the end in a buffer longer than any retail path.
    constexpr std::size_t n = 4096;
    static char text[n + 1];
    std::memset(text, 'q', n);
    text[n] = '\0';
    CHECK_EQ(path_extension_after_dot(text) - text, 0);          // no dot
    text[1] = '.';
    CHECK_EQ(path_extension_after_dot(text) - text, 2);          // the only dot is at index 1
    text[0] = '.';
    CHECK_EQ(path_extension_after_dot(text) - text, 2);          // index 0 still never counts
    text[1] = 'q';
    CHECK_EQ(path_extension_after_dot(text) - text, 0);          // only the invisible dot remains
    text[n - 1] = '.';
    CHECK_EQ(path_extension_after_dot(text) - text, static_cast<std::ptrdiff_t>(n));   // terminator
    text[n - 1] = 'q';
    text[n / 2] = '.';
    CHECK_EQ(path_extension_after_dot(text) - text, static_cast<std::ptrdiff_t>(n / 2 + 1));
}

void result_aliases_the_input() {
    // The result is a pointer into the caller's buffer, not a copy: writing through the buffer shows.
    char text[] = "song.wav";
    const char *ext = path_extension_after_dot(text);
    CHECK(ext == text + 5);
    text[5] = 'W';
    CHECK(ext[0] == 'W');
    // The caller at 0x002A5414 tests result == argument for "no extension"; that identity must hold.
    char bare[] = "song";
    CHECK(path_extension_after_dot(bare) == bare);
}

}

int main() {
    simple_cases();
    last_dot_wins();
    first_byte_is_never_examined();
    trailing_dot_gives_the_terminator();
    paths_with_directory_dots();
    only_byte_0x2e_matches_and_only_zero_terminates();
    empty_string_is_the_documented_deviation();
    long_strings();
    result_aliases_the_input();
    return check::done("test_path_extension_after_dot");
}
