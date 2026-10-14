#!/usr/bin/env bash
# Builds and runs the data-free tests of single runtime sources of the fork.
#
# usage: tests/runtime/run.sh <fork path> [build dir]
#
# Every tests/runtime/test_*.cpp is one program with its own main(); it passes when it exits 0. Each
# names the fork sources it is built with on lines of the form
#     // fork-source: runtime/src/iop/spu2.cpp
# and supplies the few functions those sources call from the rest of the runtime itself, so a test
# exercises one source as it is in the fork, through its public interface, without the game.
# Include paths: <fork>/runtime/include, the directory of each named source, and tests/unit (check.hpp).
#
# Nothing is written inside the repository: programs go to the build dir
# (default ${TMPDIR:-/tmp}/b3-runtime). CXX and CXXFLAGS are honoured; without CXX the compiler is clang++
# when it is installed, else c++. A test with a line "// needs-clang" is skipped under another compiler.
# Exit 0 when everything built and passed, 1 otherwise, 2 for bad arguments.
set -u

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    echo "usage: tests/runtime/run.sh <fork path> [build dir]" >&2
    exit 2
fi
here=$(cd "$(dirname "$0")" && pwd)
fork=$1
build=${2:-${TMPDIR:-/tmp}/b3-runtime}
if [ ! -f "$fork/runtime/include/ps2/state.hpp" ]; then
    echo "no runtime headers at $fork/runtime/include" >&2
    exit 2
fi
# The fork is built with clang and some of its sources only build with it, so clang++ is used when it is
# installed and CXX does not say otherwise. A multiply is never fused into an add, as in the fork.
if [ -n "${CXX:-}" ]; then
    cxx=$CXX
elif command -v clang++ > /dev/null 2>&1; then
    cxx=clang++
else
    cxx=c++
fi
mkdir -p "$build" || exit 2
passed=0
failed=0

for source in "$here"/test_*.cpp; do
    [ -f "$source" ] || continue
    name=$(basename "$source" .cpp)
    # A fork source written with clang's vector builtins builds with clang only. A test of one says so on
    # a line "// needs-clang" and is skipped, in words, under another compiler.
    if grep -q '^// needs-clang' "$source" && ! "$cxx" --version 2> /dev/null | grep -qi clang; then
        echo "$name: skipped (its fork source needs clang)"
        continue
    fi
    flags=(-std=c++20 -O1 -g -Wall -Wextra -ffp-contract=off ${CXXFLAGS:-} -I "$fork/runtime/include" -I "$here/../unit")
    sources=()
    missing=0
    while read -r path; do
        [ -n "$path" ] || continue
        if [ ! -f "$fork/$path" ]; then
            echo "FAIL  $name (no $path in the fork)"
            missing=1
            continue
        fi
        sources+=("$fork/$path")
        flags+=(-I "$(dirname "$fork/$path")")
    done < <(sed -n 's|^// fork-source: *||p' "$source" | tr -d '\r')
    if [ "$missing" -ne 0 ]; then
        failed=$((failed + 1))
    elif ! "$cxx" "${flags[@]}" "$source" "${sources[@]}" -o "$build/$name"; then
        echo "FAIL  $name (does not build)"
        failed=$((failed + 1))
    elif ! "$build/$name"; then
        echo "FAIL  $name"
        failed=$((failed + 1))
    else
        passed=$((passed + 1))
    fi
done

echo "runtime tests: $passed passed, $failed failed"
[ "$failed" -eq 0 ] && [ "$passed" -gt 0 ]
