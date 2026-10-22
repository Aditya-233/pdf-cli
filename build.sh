#!/usr/bin/env bash
set -euo pipefail

CC="${CC:-gcc}"
CFLAGS="${CFLAGS:--std=c23 -O3 -march=native -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE}"
INCLUDES="-Iinclude -Idepends/go-fitz/include -Idepends/libsixel/include -Idepends/yyjson/src"
LIBS="depends/libsixel/libsixel.a depends/go-fitz/libs/libmupdf_linux_amd64.a depends/go-fitz/libs/libmupdfthird_linux_amd64.a -lm -lpthread"

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
            $CC $CFLAGS $INCLUDES -c "$src" -o "$obj"
        fi
    done

    echo "  [LINK] $BINARY"
    $CC $CFLAGS "${OBJS[@]}" $LIBS -o "$BINARY"
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
        $CC $CFLAGS $INCLUDES tests/test_hud.c "${OBJS[@]:1}" $LIBS -o "$BUILD_DIR/test_hud"
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
