// 0x002934D0: the extension of a path, found by walking back from the end to the last '.'.
// See path_extension_after_dot.hpp for the description, evidence and status (draft, oracle pending).

#include "path_extension_after_dot.hpp"

namespace b3::audio {

const char *path_extension_after_dot(const char *s) {
    const char *p = s;
    while (*p != '\0') ++p;          // p now points at the terminator
    if (p == s) return s;            // documented deviation: retail reads before an empty string
    for (;;) {
        if (*p == '.') return p + 1; // the terminator is tested first (harmless), then s[len-1] ... s[1]
        --p;
        if (p == s) return s;        // the first byte is never examined
    }
}

}
