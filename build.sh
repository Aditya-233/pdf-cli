#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_ROOT"

CC="${CC:-gcc}"
CLANGD="${CLANGD:-/home/aditya/.local/share/zed/languages/clangd/clangd_23.1.0/bin/clangd}"
if [[ ! -x "$CLANGD" ]]; then
    CLANGD="$(command -v clangd || true)"
fi

# Detect C23 / C2x standard support
if $CC -std=c23 -E -x c /dev/null -o /dev/null 2>/dev/null; then
    STD_FLAG="-std=c23"
else
    STD_FLAG="-std=c2x"
fi

# Host architecture tuning override
ARCH_FLAGS="${ARCH_FLAGS- -march=native}"
if ! $CC $ARCH_FLAGS -E -x c /dev/null -o /dev/null 2>/dev/null; then
    ARCH_FLAGS=""
fi

WARN_FLAGS=(-Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wundef -Wformat=2 -Wvla -Wnull-dereference -Wdouble-promotion)
BASE_CFLAGS=("$STD_FLAG" -O3 $ARCH_FLAGS "${WARN_FLAGS[@]}" -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE)
EXTRA_CPPFLAGS=(-ffunction-sections -fdata-sections)
EXTRA_LDFLAGS=(-no-pie -Wl,--gc-sections -Wl,--as-needed -Wl,-O1)

INCLUDES=(-Iinclude -isystem depends/go-fitz/include -isystem depends/libsixel/include)
BINARY="${BINARY:-pdf-cli}"
PREFIX="${PREFIX:-$HOME/.local/bin}"
BUILD_DIR="${BUILD_DIR:-build}"
SRCS=(src/util.c src/pdf.c src/term.c src/sixel.c src/main.c src/mupdf_opt.c)
LIBS=(depends/libsixel/libsixel.a "$BUILD_DIR/libs/libmupdf_opt.a" "$BUILD_DIR/libs/libthird_opt.a" -lz -ljpeg -lm -lpthread)

prepare_opt_libs() {
    local lib_dir="$BUILD_DIR/libs"
    local opt_mupdf="$lib_dir/libmupdf_opt.a"
    local opt_third="$lib_dir/libthird_opt.a"
    local hash_file="$lib_dir/.lib_prune_hash"
    mkdir -p "$lib_dir"

    local mupdf_del=(
        hyph-all.zip.o html-parse.o html-layout.o css-apply.o \
        svg-run.o svg-device.o pdf-js.o pdf-write.o \
        xps-common.o xps-doc.o xps-glyphs.o xps-gradient.o xps-image.o xps-link.o xps-outline.o xps-path.o xps-resource.o xps-tile.o xps-util.o xps-zip.o \
        svg-color.o svg-doc.o svg-parse.o css-parse.o epub-doc.o html-doc.o html-font.o html-outline.o md.o mobi.o office.o txt.o xml-dom.o reflow-doc.o mucbz.o muimg.o \
        NimbusBoxes-Regular.cff.o CharisSIL-Bold.cff.o CharisSIL-BoldItalic.cff.o CharisSIL-Italic.cff.o CharisSIL.cff.o \
        barcode.o zxingbarcode.o tessocr.o ocr-device.o output-cbz.o output-csv.o output-docx.o output-jpeg.o output-pcl.o output-pclm.o \
        output-pdfocr.o output-png.o output-pnm.o output-ps.o output-psd.o output-pwg.o output-svg.o gz-doc.o unlibarchive.o untar.o uncfb.o \
        load-bmp.o load-gif.o load-pnm.o load-psd.o load-tiff.o load-jxr.o stext-table.o stext-raft.o stext-boxer.o stext-classify.o \
        pdf-clean.o pdf-clean-file.o pdf-recolor.o pdf-shade-recolor.o pdf-zugferd.o \
        document-all.o story-writer.o writer.o xml-write.o pdf-af.o pdf-graft.o pdf-subset.o subset-cff.o subset-ttf.o pdf-op-vectorize.o \
        test-device.o trace-device.o xmltext-device.o track-usage.o memento.o leptonica-wrap.o skew.o deskew.o warp.o getopt.o
    )

    local third_del=(
        hb-subset-table-layout.o hb-subset-table-color.o hb-subset-table-var.o hb-subset-table-other.o hb-subset.o hb-subset-table-cff.o \
        hb-subset-plan.o hb-subset-cff2.o hb-subset-cff1.o hb-subset-plan-var.o hb-subset-plan-layout.o one.o char_ref.o tokenizer.o parser.o houdini_html_u.o \
        arena.o blocks.o buffer.o cmark.o cmark_ctype.o footnotes.o houdini_href_e.o houdini_html_e.o html.o inlines.o iterator.o linked_list.o map.o \
        node.o plugin.o references.o registry.o scanners.o syntax_extension.o utf8.o autolink.o core-extensions.o ext_scanners.o strikethrough.o \
        table.o tagfilter.o tasklist.o autoheaderid.o docx.o docx_template.o odt.o odt_template.o boxer.o extract.o join.o outf.o rect.o sys.o \
        hb-subset-cff-common.o hb-subset-input.o hb-subset-instancer-iup.o hb-subset-instancer-solver.o \
        adler32.o compress.o crc32.o deflate.o inffast.o inflate.o inftrees.o trees.o uncompr.o zutil.o \
        jaricom.o jcapimin.o jcapistd.o jcarith.o jccoefct.o jccolor.o jcdctmgr.o jchuff.o jcinit.o jcmainct.o jcmarker.o jcmaster.o jcomapi.o \
        jcparam.o jcprepct.o jcsample.o jdapimin.o jdapistd.o jdarith.o jdatadst.o jdatasrc.o jdcoefct.o jdcolor.o jddctmgr.o jdhuff.o jdinput.o \
        jdmainct.o jdmarker.o jdmaster.o jdmerge.o jdpostct.o jdsample.o jdtrans.o jerror.o jfdctflt.o jfdctfst.o jfdctint.o jidctflt.o jidctfst.o \
        jidctint.o jmemmgr.o jquant1.o jquant2.o jutils.o \
        alloc.o astring.o buffer.o document.o json.o mem.o html.o text.o xml.o zip.o
    )

    local current_prune_hash="${mupdf_del[*]} ||| ${third_del[*]}"
    local need_rebuild=0
    if [[ ! -f "$opt_mupdf" || ! -f "$opt_third" ]]; then
        need_rebuild=1
    elif [[ depends/go-fitz/libs/libmupdf_linux_amd64.a -nt "$opt_mupdf" || depends/go-fitz/libs/libmupdfthird_linux_amd64.a -nt "$opt_third" ]]; then
        need_rebuild=1
    elif [[ ! -f "$hash_file" || "$(cat "$hash_file")" != "$current_prune_hash" ]]; then
        need_rebuild=1
    fi

    if [[ "$need_rebuild" -eq 1 ]]; then
        echo "  [OPT] Preparing streamlined MuPDF and third-party static libraries in $lib_dir..."
        cp depends/go-fitz/libs/libmupdf_linux_amd64.a "$opt_mupdf"
        ar d "$opt_mupdf" "${mupdf_del[@]}" 2>/dev/null || true

        cp depends/go-fitz/libs/libmupdfthird_linux_amd64.a "$opt_third"
        ar d "$opt_third" "${third_del[@]}" 2>/dev/null || true

        echo "$current_prune_hash" > "$hash_file"
    fi
}

check_config_fingerprint() {
    local target_dir="$1"
    local config_str="$CC ${BASE_CFLAGS[*]} ${EXTRA_CPPFLAGS[*]}"
    local hash_file="$target_dir/.config_hash"
    mkdir -p "$target_dir"
    if [[ -f "$hash_file" ]]; then
        local prev_hash
        prev_hash="$(cat "$hash_file")"
        if [[ "$prev_hash" != "$config_str" ]]; then
            echo "  [CONFIG] Build configuration changed, cleaning $target_dir..."
            rm -f "$target_dir"/*.o "$target_dir"/*.d
        fi
    fi
    echo "$config_str" > "$hash_file"
}

do_build() {
    echo "===> Compiling pdf-cli ($STD_FLAG ISO/IEC 9899:2024, max strictness)..."
    local obj_dir="$BUILD_DIR/release"
    check_config_fingerprint "$obj_dir"

    local OBJS=()
    for src in "${SRCS[@]}"; do
        local base
        base="$(basename "$src" .c)"
        local obj="$obj_dir/${base}.o"
        local dep="$obj_dir/${base}.d"
        OBJS+=("$obj")

        local rebuild=0
        if [[ ! -f "$obj" || "$src" -nt "$obj" ]]; then
            rebuild=1
        elif [[ -f "$dep" ]]; then
            while IFS= read -r header; do
                if [[ -n "$header" && -f "$header" && "$header" -nt "$obj" ]]; then
                    rebuild=1
                    break
                fi
            done < <(tr ' ' '\n' < "$dep" | grep -v ':$' | grep -v '^$' | sed 's/\\$//' || true)
        fi

        if [[ "$rebuild" -eq 1 ]]; then
            echo "  [CC] $src"
            $CC "${BASE_CFLAGS[@]}" "${EXTRA_CPPFLAGS[@]}" "${INCLUDES[@]}" -MMD -MP -c "$src" -o "$obj"
        fi
    done

    prepare_opt_libs
    echo "  [LINK] $BINARY"
    $CC "${BASE_CFLAGS[@]}" "${OBJS[@]}" "${LIBS[@]}" "${EXTRA_LDFLAGS[@]}" -o "$BINARY"
    strip -s "$BINARY" 2>/dev/null || true
    echo "===> Build complete: ./$BINARY"
}

do_lint() {
    echo "===================================================="
    echo "===> Running GCC at Maximum Strictness & -fanalyzer"
    echo "===================================================="
    local lint_dir="$BUILD_DIR/lint"
    mkdir -p "$lint_dir"
    $CC "${BASE_CFLAGS[@]}" -fanalyzer "${INCLUDES[@]}" -c "${SRCS[@]}" tests/test_pdfcli.c tests/test_simulation.c
    mv *.o "$lint_dir/" 2>/dev/null || true
    echo "===> GCC check passed with 0 errors and 0 warnings!"

    if [[ -n "$CLANGD" && -x "$CLANGD" ]]; then
        echo -e "\n===================================================="
        echo "===> Running Clangd & Clang-Tidy at Max Strictness"
        echo "===================================================="
        echo "Using Clangd: $CLANGD"
        mkdir -p "$BUILD_DIR"
        cat << EOF > "$BUILD_DIR/compile_commands.json"
[
  { "directory": "$REPO_ROOT", "command": "$CC $STD_FLAG -Wall -Wextra -Wpedantic ${INCLUDES[*]} -c src/util.c", "file": "src/util.c" },
  { "directory": "$REPO_ROOT", "command": "$CC $STD_FLAG -Wall -Wextra -Wpedantic ${INCLUDES[*]} -c src/pdf.c", "file": "src/pdf.c" },
  { "directory": "$REPO_ROOT", "command": "$CC $STD_FLAG -Wall -Wextra -Wpedantic ${INCLUDES[*]} -c src/term.c", "file": "src/term.c" },
  { "directory": "$REPO_ROOT", "command": "$CC $STD_FLAG -Wall -Wextra -Wpedantic ${INCLUDES[*]} -c src/sixel.c", "file": "src/sixel.c" },
  { "directory": "$REPO_ROOT", "command": "$CC $STD_FLAG -Wall -Wextra -Wpedantic ${INCLUDES[*]} -c src/main.c", "file": "src/main.c" },
  { "directory": "$REPO_ROOT", "command": "$CC $STD_FLAG -Wall -Wextra -Wpedantic ${INCLUDES[*]} -c src/mupdf_opt.c", "file": "src/mupdf_opt.c" },
  { "directory": "$REPO_ROOT", "command": "$CC $STD_FLAG -Wall -Wextra -Wpedantic ${INCLUDES[*]} -c tests/test_pdfcli.c", "file": "tests/test_pdfcli.c" },
  { "directory": "$REPO_ROOT", "command": "$CC $STD_FLAG -Wall -Wextra -Wpedantic ${INCLUDES[*]} -c tests/test_simulation.c", "file": "tests/test_simulation.c" }
]
EOF
        for f in "${SRCS[@]}" tests/test_pdfcli.c tests/test_simulation.c; do
            echo "  [CLANGD] Checking $f..."
            local out
            if ! out="$("$CLANGD" --compile-commands-dir="$BUILD_DIR" --check="$f" --clang-tidy --tweaks="" 2>&1)"; then
                echo "$out"
                echo "Error: Clangd failed on $f"
                exit 1
            fi
            echo "$out" | grep -E "All checks completed|FAIL|ERROR|error:" || true
            if echo "$out" | grep -qE "FAIL|ERROR|error:"; then
                echo "Error: Clangd detected error in $f"
                exit 1
            fi
        done
        echo "===> Clangd checks passed cleanly on the entire codebase!"
    else
        echo "Notice: clangd not found, skipping clangd language server checks."
    fi
}

do_test() {
    do_build

    local bin_cmd
    [[ "$BINARY" = /* ]] && bin_cmd="$BINARY" || bin_cmd="./$BINARY"

    echo -e "\n===> Running smoke test..."
    "$bin_cmd" --help > /dev/null
    "$bin_cmd" --version
    echo -e "===> Smoke test passed!\n"

    echo "===> Building and running strictness unit test suite..."
    local test_bin="$BUILD_DIR/test_pdfcli"
    $CC "${BASE_CFLAGS[@]}" "${EXTRA_CPPFLAGS[@]}" "${INCLUDES[@]}" \
        tests/test_pdfcli.c "$BUILD_DIR/release/util.o" "$BUILD_DIR/release/pdf.o" "$BUILD_DIR/release/sixel.o" "$BUILD_DIR/release/term.o" "$BUILD_DIR/release/mupdf_opt.o" \
        "${LIBS[@]}" "${EXTRA_LDFLAGS[@]}" -o "$test_bin"
    "$test_bin"

    echo -e "\n===> Building Full Application under AddressSanitizer & UndefinedBehaviorSanitizer..."
    local asan_dir="$BUILD_DIR/asan"
    mkdir -p "$asan_dir"
    local ASAN_FLAGS=("$STD_FLAG" -fsanitize=address,undefined -fno-sanitize-recover=all -g -Wall -Wextra -Wpedantic -Werror -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE -ffunction-sections -fdata-sections)
    local ASAN_OBJS=()
    for src in "${SRCS[@]}"; do
        local base
        base="$(basename "$src" .c)"
        local obj="$asan_dir/${base}.o"
        ASAN_OBJS+=("$obj")
        $CC "${ASAN_FLAGS[@]}" "${INCLUDES[@]}" -c "$src" -o "$obj"
    done

    local asan_unit="$BUILD_DIR/test_asan"
    $CC "${ASAN_FLAGS[@]}" "${INCLUDES[@]}" tests/test_pdfcli.c \
        "$asan_dir/util.o" "$asan_dir/pdf.o" "$asan_dir/sixel.o" "$asan_dir/term.o" "$asan_dir/mupdf_opt.o" \
        "${LIBS[@]}" -Wl,--gc-sections -o "$asan_unit"
    "$asan_unit"

    local asan_app="$BUILD_DIR/pdf-cli-asan"
    $CC "${ASAN_FLAGS[@]}" "${ASAN_OBJS[@]}" "${LIBS[@]}" -Wl,--gc-sections -o "$asan_app"
    echo -e "===> ASan & UBSan unit checks passed cleanly (0 leaks, 0 errors)!\n"

    echo "===> Building & Running Interactive PTY Terminal Simulation on ASan binary..."
    local sim_bin="$BUILD_DIR/test_simulation"
    $CC "${BASE_CFLAGS[@]}" tests/test_simulation.c -o "$sim_bin"
    "$sim_bin" "$asan_app"
    echo "===> Simulation suite passed cleanly under ASan!"

    echo -e "\n===> Running Linter Checks (GCC -fanalyzer & Clangd)..."
    do_lint
}

do_clean() {
    rm -rf "$BUILD_DIR" "$BINARY"
    echo "===> Clean complete."
}

do_install() {
    do_build
    mkdir -p "$PREFIX"
    install -Dm755 "$BINARY" "$PREFIX/$(basename "$BINARY")"
    echo "===> Clean install complete: $PREFIX/$(basename "$BINARY")"
}

case "${1:-build}" in
    build) do_build ;;
    test) do_test ;;
    lint) do_lint ;;
    clean) do_clean ;;
    install) do_install ;;
    *) echo "Usage: $0 {build|test|lint|clean|install}"; exit 1 ;;
esac
