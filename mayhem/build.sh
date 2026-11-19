#!/usr/bin/env bash
#
# mayhem/build.sh — build this repo's fuzz harness(es). EDIT per repo.
#
# Runs inside the commit image (mayhem/Dockerfile) as `mayhem` in /mayhem. The base image
# (ghcr.io/savantenvs/base) already exports the build contract — use these, don't redefine:
#   CC, CXX             stock clang / clang++
#   LIB_FUZZING_ENGINE  -fsanitize=fuzzer   (link into each harness that has a LLVMFuzzer entry)
#   SANITIZER_FLAGS     -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
#                       (ASan + UBSan, both set to HALT — so Mayhem catches memory AND UB defects)
#   DEBUG_FLAGS         -g -gdwarf-3   (DWARF debug info — always on for fuzz/standalone builds,
#                       independent of the sanitizer off-switch; DWARF version must be < 4)
#   RUST_DEBUG_FLAGS    -C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3
#                       (thread through RUSTFLAGS on every cargo-fuzz build)
#   GO_DEBUG_FLAGS      -gcflags=all=-N -l
#                       (thread through go build / go-fuzz-build so the linked ELF keeps symbols)
#   SRC                 /mayhem (the repo source)
#
# Contract: build EVERYTHING here — one runnable binary per fuzz harness, AND the project's test
# suite (so mayhem/test.sh only has to RUN it, never compile). Keep it ADDITIVE (build upstream as
# upstream documents; don't edit upstream files). IMPORTANT: build the PROJECT ITSELF with
# $SANITIZER_FLAGS and $DEBUG_FLAGS (not just the harness) so the fuzzed code is instrumented
# AND carries DWARF < 4 symbols — otherwise ASan/UBSan only see the harness, not the library
# you're trying to find bugs in, and backtraces won't resolve project source lines. Build the TEST
# suite with the project's NORMAL flags (a clean, independent build) so test.sh stays an honest
# functional oracle and won't false-fail on benign UB. Leave the test binary/runner where test.sh
# expects it.
set -euo pipefail

# clang rejects SOURCE_DATE_EPOCH='' (empty) — it must be unset or a valid integer.
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

# Build knobs come from the ENVIRONMENT (overridable), with sane defaults — no if-statements,
# just parameter-expansion fallbacks. Default sanitizers are the base's ASan+UBSan (halting); override
# per build via the Dockerfile's `--build-arg SANITIZER_FLAGS="..."`.
# NB: SANITIZER_FLAGS uses `=` (no colon) on purpose — `=` only fills when the var is UNSET, so an
# explicit EMPTY value (`--build-arg SANITIZER_FLAGS=`) is honored and builds with NO sanitizers
# (useful when you want the program's natural crash / full backtrace, not an ASan report). The other
# knobs use `:=` (default on empty too). MAYHEM_JOBS sets build parallelism (falls back to nproc).
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
# DEBUG_FLAGS carries DWARF debug info INDEPENDENTLY of the sanitizer off-switch (so an empty
# SANITIZER_FLAGS still yields DWARF symbols). DWARF MUST be < 4 (Mayhem triage can't read >=4); clang-19's
# plain `-g` emits DWARF-5, so `-gdwarf-3` is explicit. Apply $DEBUG_FLAGS to the fuzz/harness/standalone
# builds (NOT the test/oracle build). Rust/Go carry the same intent via their language flags below.
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${RUST_DEBUG_FLAGS:=-C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3}"
: "${GO_DEBUG_FLAGS:=-gcflags=all=-N -l}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
# COVERAGE_FLAGS: empty by default → no effect on the normal oracle build. Set it via the Dockerfile's
# `--build-arg COVERAGE_FLAGS="-fprofile-instr-generate -fcoverage-mapping"` to instrument the TEST
# build for source-coverage measurement (how much of the project the test suite actually exercises —
# a quality signal for the oracle; complements the anti-reward-hack sabotage check). APPEND it to the
# test build's compile+link flags in step 3 (NOT the fuzz build); after `test.sh` runs, merge with
# `llvm-profdata` and report with `llvm-cov`. Empty value is honored (`=`, not `:=`).
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS RUST_DEBUG_FLAGS GO_DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

TARGETS="fuzz_config fuzz_convert fuzz_mask fuzz_text"
CONF_OPTS="--without-v4l2 --without-webp --without-libcam --without-opencv --without-mariadb
  --without-mysql --without-pgsql --without-sqlite3 --without-pulse --without-alsa --without-fftw3"
LIBS="-pthread -ljpeg -lmicrohttpd -lz -lavutil -lavformat -lavcodec -lswscale -lavdevice -lavfilter"
# Mirrors src/Makefile.am's AM_CPPFLAGS (configure's prefix defaults) for the motion.o rebuild below.
MOTION_DEFS='-Dconfigdir=\"/usr/local/var/lib/motion\" -Dsysconfdir=\"/usr/local/etc/motion\" -DLOCALEDIR=\"/usr/local/share/locale\"'

# 0) Generate the build system (configure, Makefile.in, m4/ are gitignored upstream, so this must
#    also work on a git-clean'ed tree). Skipped when already generated and up to date.
if [ ! -x configure ] || [ configure.ac -nt configure ]; then
  autoreconf -fi
fi

# configure_and_make <dir> <CXXFLAGS>: out-of-tree autotools build of motion. Re-runnable: an
# existing configured dir with the same flags is reused and make rebuilds only what changed.
configure_and_make() {
  local dir="$1" flags="$2"
  mkdir -p "$dir"
  if [ ! -f "$dir/config.status" ] || [ "$dir/config.status" -ot configure ] || \
     [ "$(cat "$dir/.mayhem-flags" 2>/dev/null)" != "$flags" ]; then
    # shellcheck disable=SC2086
    (cd "$dir" && "$SRC/configure" $CONF_OPTS CC="$CC" CXX="$CXX" CXXFLAGS="$flags" LDFLAGS="$flags")
    echo "$flags" > "$dir/.mayhem-flags"
  fi
  make -C "$dir" -j"$MAYHEM_JOBS"
}

# motion.cpp defines main(); recompile it with main renamed so a harness/runner can own main.
compile_nomain() {
  local dir="$1" flags="$2"
  eval "$CXX -std=c++20 -DHAVE_CONFIG_H -D_THREAD_SAFE -I\"$dir\" -I\"$SRC/src\" $MOTION_DEFS $flags \
    -Dmain=motion_main -c \"$SRC/src/motion.cpp\" -o \"$dir/motion_nomain.o\""
}

# link_with_app <dir> <flags> <out> <objs/flags...>: link against every motion object except motion.o.
link_with_app() {
  local dir="$1" flags="$2" out="$3"; shift 3
  local objs
  objs=$(ls "$dir"/src/*.o | grep -v '/motion\.o$')
  # shellcheck disable=SC2086
  $CXX $flags "$@" $objs "$dir/motion_nomain.o" $LIBS -o "$out"
}

# 1) Sanitized build of motion itself (ASan+UBSan, DWARF 3), so the fuzzed code is instrumented.
FUZZ_DIR="$SRC/mayhem/build-fuzz"
FUZZ_FLAGS="-O1 $SANITIZER_FLAGS $DEBUG_FLAGS"
# SanCov edge instrumentation for libFuzzer/Mayhem on every motion object (not only the harness).
COV_FLAGS="-fsanitize=fuzzer-no-link"
configure_and_make "$FUZZ_DIR" "$FUZZ_FLAGS $COV_FLAGS"
compile_nomain "$FUZZ_DIR" "$FUZZ_FLAGS $COV_FLAGS"

# 2) Harnesses: a libFuzzer binary and a standalone reproducer each, with the LSan-off hook.
HFLAGS="-std=c++20 -DHAVE_CONFIG_H -D_THREAD_SAFE -I$FUZZ_DIR -I$SRC/src -I$SRC/mayhem/harnesses $FUZZ_FLAGS $COV_FLAGS"
# shellcheck disable=SC2086
$CXX $FUZZ_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$FUZZ_DIR/lsan_off.o"
# shellcheck disable=SC2086
$CC $FUZZ_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$FUZZ_DIR/standalone_main.o"
for t in $TARGETS; do
  # shellcheck disable=SC2086
  $CXX $HFLAGS -c "$SRC/mayhem/harnesses/$t.cpp" -o "$FUZZ_DIR/$t.o"
  link_with_app "$FUZZ_DIR" "$FUZZ_FLAGS" "/mayhem/$t" $LIB_FUZZING_ENGINE "$FUZZ_DIR/$t.o" "$FUZZ_DIR/lsan_off.o"
  link_with_app "$FUZZ_DIR" "$FUZZ_FLAGS" "/mayhem/$t-standalone" "$FUZZ_DIR/standalone_main.o" "$FUZZ_DIR/$t.o" "$FUZZ_DIR/lsan_off.o"
done

# 3) Functional oracle: motion built with its NORMAL flags (no sanitizers; configure adds its own
#    -O3/-Wall) plus the known-answer runner mayhem/harnesses/selftest.cpp linked against it.
TEST_DIR="$SRC/mayhem/build-test"
TEST_FLAGS="-O2 $COVERAGE_FLAGS"
configure_and_make "$TEST_DIR" "$TEST_FLAGS"
compile_nomain "$TEST_DIR" "$TEST_FLAGS"
# shellcheck disable=SC2086
$CXX -std=c++20 -DHAVE_CONFIG_H -D_THREAD_SAFE -I"$TEST_DIR" -I"$SRC/src" -I"$SRC/mayhem/harnesses" \
  $TEST_FLAGS -c "$SRC/mayhem/harnesses/selftest.cpp" -o "$TEST_DIR/selftest.o"
link_with_app "$TEST_DIR" "$TEST_FLAGS" "$TEST_DIR/motion_selftest" "$TEST_DIR/selftest.o"
