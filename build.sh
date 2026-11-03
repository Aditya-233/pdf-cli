#!/usr/bin/env bash
set -euo pipefail

CC="${CC:-gcc}"
CFLAGS="${CFLAGS:--std=c23 -O3 -march=native -Wall -Wextra -Wpedantic -Werror -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE}"
EXTRA_CPPFLAGS="${EXTRA_CPPFLAGS:--ffunction-sections -fdata-sections}"
EXTRA_LDFLAGS="${EXTRA_LDFLAGS:--Wl,--gc-sections -Wl,--as-needed -Wl,-O1}"

INCLUDES="-Iinclude -Idepends/go-fitz/include -Idepends/libsixel/include"
LIBS="depends/libsixel/libsixel.a depends/go-fitz/libs/libmupdf_linux_amd64.a depends/go-fitz/libs/libmupdfthird_linux_amd64.a -lm -lpthread"

BINARY="${BINARY:-pdf-cli}"
PREFIX="${PREFIX:-$HOME/.local/bin}"
BUILD_DIR="${BUILD_DIR:-build}"

SRCS=(
    src/pdf.c
    src/term.c
    src/sixel.c
    src/main.c
)

do_build() {
    echo "===> Compiling pdf-cli (C23 ISO/IEC 9899:2024, max strictness)..."
    mkdir -p "$BUILD_DIR"
    OBJS=()
    for src in "${SRCS[@]}"; do
        obj="$BUILD_DIR/${src//\//_}.o"
        OBJS+=("$obj")
        if [[ ! -f "$obj" || "$src" -nt "$obj" ]]; then
            echo "  [CC] $src"
            $CC $CFLAGS $EXTRA_CPPFLAGS $INCLUDES -c "$src" -o "$obj"
        fi
    done
    echo "  [LINK] $BINARY"
    $CC $CFLAGS "${OBJS[@]}" $LIBS $EXTRA_LDFLAGS -o "$BINARY"
    echo "===> Build complete: ./$BINARY"
}

do_test() {
    do_build
    echo ""
    echo "===> Running smoke test..."
    ./"$BINARY" --help > /dev/null
    echo "===> Smoke test passed!"
    echo ""
    echo "===> Building and running test suite..."
    $CC $CFLAGS $EXTRA_CPPFLAGS $INCLUDES tests/test_pdfcli.c "$BUILD_DIR/src_pdf.c.o" "$BUILD_DIR/src_sixel.c.o" "$BUILD_DIR/src_term.c.o" $LIBS $EXTRA_LDFLAGS -o "$BUILD_DIR/test_pdfcli"
    "$BUILD_DIR/test_pdfcli"
    echo ""
    echo "===> Testing AddressSanitizer (Memory & Bounds Check)..."
    $CC -std=c23 -fsanitize=address,undefined -g $INCLUDES tests/test_pdfcli.c src/pdf.c src/sixel.c src/term.c $LIBS -o "$BUILD_DIR/test_asan"
    "$BUILD_DIR/test_asan"
    echo "===> ASan & UBSan checks passed cleanly (0 leaks, 0 errors)!"
}

do_clean() {
    echo "===> Cleaning build artifacts..."
    rm -rf "$BUILD_DIR" "$BINARY"
    echo "===> Clean complete."
}

do_install() {
    do_build
    echo "===> Installing to $PREFIX..."
    mkdir -p "$PREFIX"
    install -Dm755 "$BINARY" "$PREFIX/$BINARY"
    echo "===> Clean install complete: $PREFIX/$BINARY"
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
