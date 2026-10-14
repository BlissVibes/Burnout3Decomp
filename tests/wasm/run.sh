#!/usr/bin/env bash
# Builds the tests of the fork's exact VU arithmetic as WebAssembly and runs them under node, as the page
# runs that code.
#
# usage: tests/wasm/run.sh <fork path> [build dir]
#
# The tests are tests/unit/test_vu_exact.cpp (the arithmetic on host floats against the integer model),
# tests/runtime/test_vu0_loop.cpp (the fast VU0 loop against the exact one) and tests/runtime/test_vu1_jit.cpp
# (the VU1 JIT, which exists only in WebAssembly, against the fast interpreter's arithmetic), built from the
# same sources as tests/unit/run.sh and tests/runtime/run.sh build them, a runtime test with the fork sources
# it names on its "// fork-source:" lines. The compiler and node are the fork's own, in <fork>/tools/emsdk.
#
# Each test is built twice, unless a line "// wasm-builds: page" (or plain) names the one it runs in; a line
# "// wasm-link: ..." gives link flags of its own:
#   plain  -O3 -msimd128, a multiply never fused into an add; main() on node's main thread.
#   page   with the compile flags the fork gives the page, read from its CMakeLists.txt (what it adds for
#          every host and for emscripten: threads, tail calls, WebAssembly exceptions), and main() on a
#          thread of its own, as the page runs the emulation. The test's environment reaches it as the
#          page's does: through Module.ENV, set before the program starts. A page built with relaxed SIMD
#          fails here without a build: its bits would be the engine's choice.
#
# A build passes when it exits 0 under node and its "hash" lines are the ones the native build of the same
# test prints: the builds must compute the same bits. The native build is made here with clang++ -O3;
# without clang++ the hashes are not compared, and that counts as a failure, not as a pass.
#
# The page selects the VU0 loop, and the mode that checks one loop against the other, through the
# environment only (the PS2_ parameters of its address). A test whose usage names --environments lists such
# runs, a mode and its variables on each line; its page build is run once for each, with nothing else in
# the environment, and every one must exit 0.
#
# Nothing is written inside the repository or the fork: programs and the emscripten cache go to the build
# dir (default ${TMPDIR:-/tmp}/b3-wasm). The first run fills the cache, which takes a few minutes.
# Exit 0 when everything built and passed, 1 otherwise, 2 for bad arguments or a missing toolchain.
set -u

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    echo "usage: tests/wasm/run.sh <fork path> [build dir]" >&2
    exit 2
fi
here=$(cd "$(dirname "$0")" && pwd)
tests=$(cd "$here/.." && pwd)
fork=$1
build=${2:-${TMPDIR:-/tmp}/b3-wasm}
if [ ! -f "$fork/runtime/include/ps2/vu_exact.hpp" ]; then
    echo "no runtime headers at $fork/runtime/include" >&2
    exit 2
fi
empp="$fork/tools/emsdk/upstream/emscripten/em++"
node=$(ls "$fork"/tools/emsdk/node/*/bin/node 2> /dev/null | sort | tail -n 1)
if [ ! -x "$empp" ] || [ -z "$node" ] || [ ! -x "$node" ]; then
    echo "no emscripten toolchain in $fork/tools/emsdk (em++ and its node are needed)" >&2
    exit 2
fi
mkdir -p "$build/emcache" || exit 2
export EM_CONFIG="$fork/tools/emsdk/.emscripten"
export EM_CACHE="$build/emcache"
passed=0
failed=0

# The page's compile flags: the add_compile_options lines of the fork's CMakeLists.txt, the one for every
# host and the one inside if(EMSCRIPTEN), after what CMake itself passes for a release build in C++20.
every_host=$(sed -n 's/^add_compile_options(\(.*\))$/\1/p' "$fork/CMakeLists.txt" 2> /dev/null | tr '\r\n' '  ')
emscripten=$(sed -n '/^if(EMSCRIPTEN)$/,/^endif()$/s/^ *add_compile_options(\(.*\))$/\1/p' "$fork/CMakeLists.txt" 2> /dev/null | tr '\r\n' '  ')
page_flags=$(echo "-std=gnu++20 -O3 -DNDEBUG $every_host $emscripten" | tr -s ' ' | sed 's/ $//')
case " $emscripten " in
*" -pthread "*) ;;
*)
    echo "FAIL  the page's compile flags (no add_compile_options with -pthread inside if(EMSCRIPTEN) in $fork/CMakeLists.txt)"
    echo "wasm tests: 0 passed, 1 failed"
    exit 1
    ;;
esac
# WebAssembly fixes the bits of every float operation, so what node computes here a browser computes too.
# Relaxed SIMD takes that away: the engine may then choose, and a pass under node on this machine would say
# nothing about another engine. (A fast-math switch needs no such line: the page builds below fail with it.)
case " $page_flags " in
*" -mrelaxed-simd "*)
    echo "FAIL  the page's compile flags (-mrelaxed-simd: the engine may choose the bits, so node cannot speak for a browser)"
    echo "wasm tests: 0 passed, 1 failed"
    exit 1
    ;;
esac
echo "      page flags: $page_flags"

# What web/shell.html does with the PS2_ parameters of the page's address, done with the variables of the
# shell that starts node.
cat > "$build/environment.js" << 'EOF'
Module.preRun = [() => {
  for (const [key, value] of Object.entries(process.env)) if (key.startsWith('PS2_')) Module.ENV[key] = value;
}];
EOF

for source in "$tests/unit/test_vu_exact.cpp" "$tests/runtime/test_vu0_loop.cpp" "$tests/runtime/test_vu1_jit.cpp"; do
    name=$(basename "$source" .cpp)
    flags=(-Wall -Wextra ${CXXFLAGS:-} -I "$fork/runtime/include" -I "$tests/unit")
    # A test may name the builds it runs in ("// wasm-builds: page" for code that needs the page's threads)
    # and link flags of its own ("// wasm-link: ...").
    kinds=$(sed -n 's|^// wasm-builds: *||p' "$source" | tr -d '\r')
    kinds=${kinds:-plain page}
    # shellcheck disable=SC2207
    link=($(sed -n 's|^// wasm-link: *||p' "$source" | tr -d '\r'))
    sources=()
    while read -r path; do
        [ -n "$path" ] || continue
        sources+=("$fork/$path")
        flags+=(-I "$(dirname "$fork/$path")")
    done < <(sed -n 's|^// fork-source: *||p' "$source" | tr -d '\r')

    # The native build, whose hash lines every WebAssembly build must print too.
    if ! command -v clang++ > /dev/null 2>&1; then
        echo "FAIL  $name (no clang++ for the native build its hashes are compared with)"
        failed=$((failed + 1))
        continue
    elif ! clang++ -std=c++20 -O3 -ffp-contract=off "${flags[@]}" "$source" "${sources[@]}" -o "$build/$name.native" || ! "$build/$name.native" > "$build/$name.native.txt"; then
        echo "FAIL  $name (the native build)"
        failed=$((failed + 1))
        continue
    elif ! grep -q ': hash ' "$build/$name.native.txt"; then
        echo "FAIL  $name (prints no hash line)"
        failed=$((failed + 1))
        continue
    fi

    for kind in $kinds; do
        if [ "$kind" = plain ]; then
            how=(-std=c++20 -O3 -ffp-contract=off -msimd128 -sENVIRONMENT=node)
        else
            # shellcheck disable=SC2206
            how=($page_flags -sENVIRONMENT=node,worker -sPROXY_TO_PTHREAD -sEXIT_RUNTIME=1 -sEXPORTED_RUNTIME_METHODS=ENV --pre-js "$build/environment.js")
        fi
        how+=("${link[@]}")
        js="$build/$name.$kind.js"
        out="$build/$name.$kind.txt"
        if ! "$empp" "${how[@]}" "${flags[@]}" "$source" "${sources[@]}" -o "$js"; then
            echo "FAIL  $name, $kind (does not build as WebAssembly)"
            failed=$((failed + 1))
            continue
        fi
        if ! "$node" "$js" > "$out"; then
            cat "$out"
            echo "FAIL  $name, $kind (under node)"
            failed=$((failed + 1))
            continue
        fi
        tail -n 1 "$out"
        if ! diff <(grep ': hash ' "$build/$name.native.txt") <(grep ': hash ' "$out"); then
            echo "FAIL  $name, $kind (the WebAssembly build's hashes are not the native build's)"
            failed=$((failed + 1))
            continue
        fi
        echo "      $name, $kind: $(grep -c ': hash ' "$out") hash lines equal to the native build's"
        if [ "$kind" = page ] && grep -q '^// .*--environments' "$source"; then
            runs=0
            wrong=0
            while read -r mode variables; do
                [ -n "$mode" ] || continue
                runs=$((runs + 1))
                # shellcheck disable=SC2086
                if ! env -i $variables "$node" "$js" --environment "$mode" < /dev/null > "$build/$name.environment.txt"; then
                    cat "$build/$name.environment.txt"
                    echo "FAIL  $name, $kind (the environment \"$variables\" does not give the $mode mode)"
                    wrong=$((wrong + 1))
                fi
            done < <("$node" "$js" --environments)
            if [ "$runs" -eq 0 ]; then
                echo "FAIL  $name, $kind (lists no environment runs)"
                wrong=1
            fi
            if [ "$wrong" -ne 0 ]; then
                failed=$((failed + 1))
                continue
            fi
            echo "      $name, $kind: $runs environments give their modes"
        fi
        passed=$((passed + 1))
    done
done

echo "wasm tests: $passed passed, $failed failed"
[ "$failed" -eq 0 ] && [ "$passed" -gt 0 ]
