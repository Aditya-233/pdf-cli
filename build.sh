#!/usr/bin/env bash
set -euo pipefail

CC="${CC:-gcc}"
# Baseline flags with C23. -march=native kept for rendering performance; size work is
# done via -Os-neutral gc-sections/LTO set below (-Os was measured ~0.1 MB smaller
# than -O3 here but costs vectorization quality in hot render paths, so -O3 stays).
CFLAGS="${CFLAGS:--std=c23 -O3 -march=native -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE}"
# RAM-size-reduction flags:
#  * -ffunction-sections/-fdata-sections: per-function/data sections for our TUs
#    (the static MuPDF/libsixel archives were already built with them), letting the
#    final link discard every unused handler/code path/rodata table (-1.8 MB text).
#  * -fno-ident/-fno-unwind-tables/-fno-asynchronous-unwind-tables: drop ident
#    comments and CFI tables for our TUs (C CZ uses setjmp; no C++ unwinding). (-9 KB)
EXTRA_CFLAGS="${EXTRA_CFLAGS:--flto=auto}"
EXTRA_CPPFLAGS="${EXTRA_CPPFLAGS:--ffunction-sections -fdata-sections -fno-ident -fno-unwind-tables -fno-asynchronous-unwind-tables}"
INCLUDES="-Iinclude -Idepends/go-fitz/include -Idepends/libsixel/include -Idepends/yyjson/src"
LIBS="depends/libsixel/libsixel.a depends/go-fitz/libs/libmupdf_linux_amd64.a depends/go-fitz/libs/libmupdfthird_linux_amd64.a -lm -lpthread"
# Linker: LTO on the driver link, drop unreferenced sections archive-wide, drop
# unused DT_NEEDED entries, small-page-safe separate-code off, no build-id note.
EXTRA_LDFLAGS="${EXTRA_LDFLAGS:--flto=auto -Wl,--gc-sections -Wl,--as-needed -Wl,-O1 -Wl,--build-id=none -Wl,-z,noseparate-code}"

BINARY="${BINARY:-pdf-cli}"
PREFIX="${PREFIX:-$HOME/.local/bin}"
BUILD_DIR="${BUILD_DIR:-build}"

SRCS=(
    src/main.c
    src/viewer.c
    src/doc_engine.c
    src/img_util.c
    src/term_raw.c
    src/term_gfx.c
    src/utf8_util.c
    src/fuzzy_match.c
    src/file_search.c
    src/menu.c
    src/picker.c
    src/toc_modal.c
    src/config.c
    src/ctl_proto.c
    src/ctl_server.c
    src/ctl_client.c
    src/cache.c
    src/prefetch.c
    src/profile.c
    src/file_watcher.c
    src/arena.c
    depends/yyjson/src/yyjson.c
)

do_build() {
    echo "===> Compiling pdf-cli (C23 ISO/IEC 9899:2024)..."
    mkdir -p "$BUILD_DIR"

    OBJS=()
    for src in "${SRCS[@]}"; do
        obj="$BUILD_DIR/${src//\//_}.o"
        OBJS+=("$obj")
        if [[ ! -f "$obj" || "$src" -nt "$obj" ]]; then
            echo "  [CC] $src"
            $CC $CFLAGS $EXTRA_CPPFLAGS $EXTRA_CFLAGS $INCLUDES -c "$src" -o "$obj"
        fi
    done

    echo "  [LINK] $BINARY"
    $CC $CFLAGS $EXTRA_CFLAGS "${OBJS[@]}" $LIBS -o "$BINARY" $EXTRA_LDFLAGS
    ln -sf "$BINARY" dvctl
    echo "===> Build complete: ./$BINARY and ./dvctl"
}

do_test() {
    do_build
    echo ""
    echo "===> Running smoke test..."
    ./"$BINARY" --help > /dev/null
    ./dvctl presets > /dev/null
    echo "===> Smoke tests passed: $BINARY is operational!"
    echo ""
    if [[ -f "tests/test_hud.c" ]]; then
        $CC $CFLAGS $EXTRA_CFLAGS $INCLUDES tests/test_hud.c "${OBJS[@]:1}" $LIBS -o "$BUILD_DIR/test_hud" $EXTRA_LDFLAGS
        "$BUILD_DIR/test_hud"
    fi
}

do_clean() {
    echo "===> Cleaning build artifacts..."
    rm -rf "$BUILD_DIR" "$BINARY" dvctl
    echo "===> Clean complete."
}

do_install() {
    do_build
    echo "===> Installing to $PREFIX..."
    mkdir -p "$PREFIX"
    cp "$BINARY" "$PREFIX/$BINARY.new"
    mv -f "$PREFIX/$BINARY.new" "$PREFIX/$BINARY"
    ln -sf "$PREFIX/$BINARY" "$PREFIX/dvctl"
    echo "===> Installed $PREFIX/$BINARY and $PREFIX/dvctl"
}

case "${1:-build}" in
    build)
        do_build
        ;;
    test)
        do_test
        ;;
    clean)
        do_clean
        ;;
    install)
        do_install
        ;;
    *)
        echo "Usage: $0 {build|test|clean|install}"
        exit 1
        ;;
esac
