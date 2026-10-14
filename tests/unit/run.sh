#!/usr/bin/env bash
# Builds and runs the data-free unit tests against the fork's runtime headers.
#
# usage: tests/unit/run.sh <fork path> [build dir]
#
# Every tests/unit/test_*.cpp is one program with its own main(); it passes when it exits 0. Each is
# compiled with -I src, -I <fork>/runtime/include and -I tests/unit, and linked against two small
# libraries built here: tests/unit/support/*.cpp (guest RAM for the runtime's memory.hpp) and every
# src/hooks/<subsystem>/*.cpp (the decompiled functions), so a test only names the headers it needs.
# A library is searched only for symbols the test leaves undefined, so a test may also define its own.
#
# Nothing is written inside the repository: objects and programs go to the build dir
# (default ${TMPDIR:-/tmp}/b3-unit). CXX and CXXFLAGS are honoured; without CXX the compiler is clang++
# when it is installed, else c++.
# Exit 0 when everything built and passed, 1 otherwise, 2 for bad arguments.
set -u

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    echo "usage: tests/unit/run.sh <fork path> [build dir]" >&2
    exit 2
fi
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
include="$1/runtime/include"
build=${2:-${TMPDIR:-/tmp}/b3-unit}
if [ ! -f "$include/ps2/fpu.hpp" ]; then
    echo "no runtime headers at $include" >&2
    exit 2
fi
# The fork is built with clang, and its vector headers (ps2/vu_ops.hpp, ps2/vu_exact.hpp) are written with
# clang's builtins: the tests of those print "skipped" under another compiler. So clang++ is used when it
# is installed and CXX does not say otherwise. A multiply is never fused into an add, as in the fork.
if [ -n "${CXX:-}" ]; then
    cxx=$CXX
elif command -v clang++ > /dev/null 2>&1; then
    cxx=clang++
else
    cxx=c++
fi
flags=(-std=c++20 -O1 -g -Wall -Wextra -ffp-contract=off ${CXXFLAGS:-} -I "$repo/src" -I "$include" -I "$here")
mkdir -p "$build/support" "$build/hooks" || exit 2
failed=0

# archive <name> <object dir> <source>...: compiles each source and collects the objects in lib<name>.a
archive() {
    local name=$1 dir=$2 objects=() source object
    shift 2
    rm -f "$build/lib$name.a" "$dir"/*.o
    for source in "$@"; do
        [ -f "$source" ] || continue
        object="$dir/$(echo "${source#"$repo/"}" | tr '/' '_').o"
        if "$cxx" "${flags[@]}" -c "$source" -o "$object"; then
            objects+=("$object")
        else
            echo "FAIL  ${source#"$repo/"} (does not compile)"
            failed=$((failed + 1))
        fi
    done
    [ ${#objects[@]} -eq 0 ] || ar rcs "$build/lib$name.a" "${objects[@]}"
}

archive support "$build/support" "$here"/support/*.cpp
archive hooks "$build/hooks" "$repo"/src/hooks/*/*.cpp
libraries=()
[ -f "$build/libhooks.a" ] && libraries+=("$build/libhooks.a")
[ -f "$build/libsupport.a" ] && libraries+=("$build/libsupport.a")

passed=0
for source in "$here"/test_*.cpp; do
    [ -f "$source" ] || continue
    name=$(basename "$source" .cpp)
    if ! "$cxx" "${flags[@]}" "$source" "${libraries[@]}" -o "$build/$name"; then
        echo "FAIL  $name (does not build)"
        failed=$((failed + 1))
    elif ! "$build/$name"; then
        echo "FAIL  $name"
        failed=$((failed + 1))
    else
        passed=$((passed + 1))
    fi
done

echo "unit tests: $passed passed, $failed failed"
[ "$failed" -eq 0 ] && [ "$passed" -gt 0 ]
