// The page's SHA-256 alone (the fork's host/sha256.cpp), against the examples of FIPS 180-4 and against
// made-up data at every length around the 64-byte block and the 56-byte padding edge. The page compares
// this digest with the one CMake took of the executable at build time, so a wrong digest would refuse
// every disc and a constant one would accept any.
//
// fork-source: host/sha256.cpp

#include "check.hpp"

#include "sha256.hpp"

#include <string>
#include <vector>

namespace {

std::string of(const std::string &text) { return ps2web::sha256_hex(reinterpret_cast<const std::uint8_t *>(text.data()), text.size()); }

void the_examples_of_the_standard() {
    CHECK(of("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(of("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(of(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// Lengths 55, 56, 63, 64 and 65 pad differently (digests from Python's hashlib over the same bytes: byte i is i * 7 + 3).
void lengths_around_a_block() {
    const auto made_up = [](std::size_t n) {
        std::string s(n, '\0');
        for (std::size_t i = 0; i < n; ++i) s[i] = static_cast<char>(i * 7 + 3);
        return s;
    };
    CHECK(of(made_up(55)) == "e7313d333c272e639f790978283f9eb392e843d0f29b7016828bb1daa4aac70b");
    CHECK(of(made_up(56)) == "4324d65f3c103567f5589c710bc08f8523f929a9272e3af36fc968e52abc6c27");
    CHECK(of(made_up(63)) == "81c80242132f230c3bd41b3e63bbcff16107339549214a99614ff26664625055");
    CHECK(of(made_up(64)) == "39e3d7b6b5d075d37d053ad89b24b41bef4f3c29760c84447cab3f3be1882241");
    CHECK(of(made_up(65)) == "aacca6ff74fdbb296d165a45cecfa04e5127bc008770fbbdd48006f2d2fae95e");
    CHECK(of(made_up(4073816)) == "159ed4cd021606d7e2e13e51d9ab714dccbec2d8889e044421e92e07bfef908e");
}

void one_changed_byte_changes_the_digest() {
    std::string a(4096, 'x'), b = a;
    b[2000] = 'y';
    CHECK(of(a) != of(b));
    CHECK(of(a).size() == 64);
}

}

int main() {
    the_examples_of_the_standard();
    lengths_around_a_block();
    one_changed_byte_changes_the_digest();
    return check::done("test_web_sha256");
}
