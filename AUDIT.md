# Codebase audit

## Scope and method

Audited the current working tree, including the pre-existing modifications to `src/main.c`, `src/pdf.c`, `tests/test_pdfcli.c`, and `tests/test_simulation.py`. No application source was changed.

Reviewed all first-party source, the public header, build script, tests, README, repository metadata, dependency headers/licenses, and the architecture described by the supplementary documents. Dependency archives were linked and exercised, but their implementations were **not source-audited**, and no online vulnerability database check was performed. This is not a guarantee that all bugs have been found.

Validation:

- Fresh strict optimized build: passed, using GCC 16.2.1.
- Existing C tests with ASan, UBSan, and float-cast-overflow instrumentation: passed.
- Existing PTY tests: passed, including the optional local 780-page PDF tests.
- GCC `-fanalyzer`: reported allocation-related null dereferences in `main.c`.
- Additional generated-PDF, PTY, arithmetic, and build-cache probes: findings below.
- Temporary probes and logs: `/tmp/pdf-cli-audit.7RfdmJ/` (not permanent regression tests).

**Evidence labels:** **Reproduced** means observed with a targeted probe; **Code-confirmed** means established by control flow/API contracts; **Risk/gap** means an exposure or missing guarantee, not a demonstrated exploit.

Severity is contextual: this is a local document viewer, but documents and filenames may be untrusted.

## High-priority findings

### 1. Terminal escape injection from document outlines and filenames

**High · Reproduced** — `src/main.c:59–91`, `src/main.c:337–362`, `src/main.c:533`.

Tab names are interpolated directly into terminal output. Outline titles only replace CR, LF, and tab; ESC, BEL, and other controls remain. Failed-open diagnostics also print the unescaped path. Truncation is not sanitization.

A filename containing `ESC ] 2 ; OWNED BEL` emitted that exact OSC title-setting command when the HUD was shown. A valid PDF with a UTF-16 outline title containing the same sequence emitted it when the TOC opened. Consequences depend on the terminal: display spoofing, title/clipboard manipulation where supported, and other terminal control effects. Arbitrary shell execution was **not** demonstrated.

**Fix:** Centralize terminal-safe text rendering. Reject/escape control characters and invalid text before display, then truncate by display width. Apply it to filenames, outline text, and diagnostics. Add hostile-text fixtures.

### 2. Unchecked allocations can crash startup or rendering

**High · Code-confirmed; static-analyzer corroborated** — `src/main.c:99–104`, `src/main.c:174–177`, `src/main.c:535–546`.

- `page_pw` and `page_ph` are dereferenced without checking `calloc`.
- `path` and `name` duplications are unchecked.
- Layout allocation failure returns from `update_tab_layout`, but `render_frame` still dereferences the layout arrays.
- Partial layout allocation leaves inconsistent state: if `page_y` succeeds and `page_h` fails, the next layout call skips allocation and writes through null `page_h`; the reverse case can leak `page_h` on retry.

GCC's analyzer flags the layout null dereference and both page-dimension allocation dereferences.

**Fix:** Make tab initialization and layout updates transactional; return explicit status, free partial allocations, and never render an invalid tab.

### 3. Unsafe `realloc` loses pointers and writes through null on failure

**High · Code-confirmed** — `src/pdf.c:98–105`, `src/pdf.c:150–151`.

Both outline growth and search-result growth overwrite the owning pointer with `realloc` and immediately dereference it. Failure leaks the previous allocation and causes a null dereference. Capacity multiplication also lacks overflow checks; outline `strdup` failure is unchecked.

**Fix:** Use a temporary pointer, check capacity/size overflow, preserve ownership on failure, and propagate allocation errors separately from empty results.

### 4. PDF dimensions can cause undefined numeric conversions

**High · Reproduced** — `src/main.c:120–140`, `src/main.c:183–201`; related public rendering inputs in `src/pdf.c:46–59`.

The layout and renderer convert scaled document dimensions to `int` without checking finiteness or range. A generated valid page with `/MediaBox [0 0 1 100000000]` caused float-cast-overflow reports at lines 126 and 184: `7.2e+10` is not representable as `int`. Clamping the already-converted result to 1 does not fix undefined conversion.

Scale parameters also accept negative, nonfinite, and extreme values. Cumulative page layout and outline indentation/count arithmetic lack explicit checked limits.

**Fix:** Validate page bounds and scale before arithmetic, use checked conversion helpers and bounded dimensions, and reject documents/layouts that cannot be represented safely.

### 5. Bounds checks themselves contain signed overflow

**Medium · Reproduced** — `src/pdf.c:48`.

`dst_x + vw` and `dst_y + vh` can overflow before the bounds comparison. A direct API probe with `INT_MAX` values triggered UBSan. Current interactive callers ordinarily supply smaller values; this is a confirmed unsafe API validation path, not a demonstrated interactive memory overwrite.

**Fix:** After validating offsets, compare `vw > dst->w - dst_x` and equivalent height expressions.

### 6. Prompt passes special-key values outside the `isprint` domain

**High · Code-confirmed** — `src/main.c:451–463`, `include/pdfcli.h:12–40`.

`term_read_key` returns special keys above 255, but `prompt_input` passes them to `isprint`. C only permits EOF or values representable as `unsigned char`; passing arrow/mouse/function-key enum values is undefined behavior and can index outside libc character tables. The local probe did not crash, which does not make this valid.

**Fix:** Handle special keys first and call `isprint` only for validated byte values. Handle Unicode through a separate input path.

### 7. Uncaught MuPDF exceptions terminate the application; rendering failure handling is inconsistent

**High · Reproduced/code-confirmed** — `src/pdf.c:12`, `src/pdf.c:78–81`, `src/main.c:174–175`, `src/main.c:205–212`.

MuPDF constructors throw, so a null check after a throwing allocation is insufficient. Viewport allocation in both rendering paths is outside `fz_try`. Handler registration is also outside protected cleanup. Application rendering ignores failures from `pdf_render_page_slice` and `sixel_draw`; `pdf_render_viewport` can return a white pixmap after slice failure.

Under a 256 MiB address-space limit with a reported 65535×65535-pixel terminal, the viewer exited with status 1. Cleanup escape sequences were emitted, but buffered render setup was flushed **after** them, including synchronized-update enable and a background change, undoing part of cleanup.

**Fix:** Catch exceptions at ownership boundaries; return actionable errors; always unwind synchronized output; flush/discard buffered frame output before final restoration; avoid emitting a partial frame on failure.

### 8. License/distribution requirements are unresolved

**High · Risk/gap** — `README.md:104–106`, `depends/go-fitz/COPYING`, `build.sh:10`, repository root.

The README says MIT, but there is no top-level MIT license grant. The executable statically links MuPDF, whose bundled license and header notices are AGPL-3.0-or-later. An MIT license on original code alone does not remove obligations on the distributed combined work. The repository contains prebuilt MuPDF/third-party archives without a documented corresponding-source provision or reproducible recipe for those exact builds. Additional bundled component notices cannot be verified from the archives alone.

A commercial MuPDF license or another compliance arrangement may exist outside the repository; none is documented here. This is a release blocker requiring licensing review, not a legal conclusion.

**Fix:** Establish and document the licensing basis, supply the first-party license, preserve required notices, and provide the required corresponding source/build information or document the applicable commercial arrangement.

## Input and terminal behavior

### 9. Escape parsing consumes and discards subsequent input

**High · Reproduced** — `src/term.c:88–160`.

After ESC, a single read consumes up to 126 bytes, but only one event is returned and remaining bytes are discarded. Sending arrow-up and `q` in one write lost the quit; a subsequent `i` still showed the HUD. Batched mouse events, pasted commands, and other keys can disappear similarly.

**Fix:** Maintain a persistent byte queue, consume exactly one complete event, and retain the remainder.

### 10. Split escape sequences become unrelated commands

**Medium · Reproduced** — `src/term.c:93–100`, `src/term.c:134–160`.

Only mouse sequences receive continuation reads. CSI keyboard sequences arriving in separate reads are discarded while incomplete; remaining bytes are interpreted as normal commands. Sending `ESC [` followed later by `D` toggled dark mode instead of moving left. The fixed 25 ms ESC heuristic is also fragile over slow connections.

**Fix:** Incremental parser with explicit sequence states and bounded timeout handling.

### 11. Advertised modified-key encodings are shadowed by early returns

**Medium · Reproduced/code-confirmed** — `src/term.c:141–158`.

For example, `ESC [ 9 ; 5 u` is interpreted as Ctrl+Tab, but `ESC [ 49 ; 5 u` (Ctrl+1 in CSI-u form) returns from the `case '4'` branch before modifier parsing. The intended Ctrl+PgUp/PgDn branches are similarly unreachable for sequences beginning with 5 or 6. Modify-other-keys and CSI-u support is partial, and the terminal is never asked to enable an extended keyboard protocol.

**Fix:** Parse complete CSI parameters/final bytes before dispatch, and explicitly negotiate/document supported keyboard protocols.

### 12. EOF, hangup, and persistent input errors never terminate the loop

**Medium · Code-confirmed** — `src/term.c:85–90`, `src/main.c:559–563`; modal loops.

`poll` errors/HUP and zero-byte reads become `KEY_NONE`, indistinguishable from a timeout. A hung-up input can produce an immediate poll/read/retry loop. Prompts and TOC loops have the same issue.

**Fix:** Inspect `revents`, distinguish EOF/error from timeout, retry EINTR appropriately, and terminate/restorе cleanly on terminal loss.

### 13. Signal cleanup can race with buffered frame output and other cleanup

**Medium · Code-confirmed** — `src/term.c:15–25`, `src/term.c:29–61`, `src/main.c:552–557`.

Terminal state is changed before signal handlers are installed and before `s_raw_active` is set, leaving a startup interruption window. `s_raw_active` is a plain `bool` shared with handlers. Different termination signals can interrupt each other's cleanup because they are not mutually masked. Direct signal-handler writes can interleave with buffered stdio frame output; restoring terminal modes does not discard that buffer.

**Fix:** Block relevant signals during state transitions, use signal-safe state/notification, and perform orderly cleanup in normal control flow where possible. Check terminal setup/cleanup return values.

### 14. Terminal capabilities and pre-existing modes are not preserved

**Medium · Risk/gap** — `src/term.c:29–65`, `src/main.c:172`, `src/main.c:302`.

No Sixel capability check/fallback exists, stdout is not required to be a terminal, and synchronized-update sequences are emitted unconditionally. Cleanup forces cursor visibility, autowrap, mouse modes, alternate-screen state, and default background rather than preserving prior states. OSC 111 resets to the default, not necessarily the user's runtime-selected background. SIGTSTP/job-control suspension is not supported cleanly because ISIG is disabled and Ctrl+Z is not handled.

**Fix:** Validate the output device, detect/document capabilities, preserve state where feasible, and define suspend/resume behavior.

### 15. Mouse parsing accepts malformed events and incompletely handles legacy/modifier variants

**Low · Code-confirmed** — `src/term.c:100–132`.

SGR parsing accepts three fields without requiring a valid final `M`/`m`; wheel events are returned regardless of final type. Modifier bits are not decoded. Legacy mouse events return actions without populating coordinates, so tab clicks/wheel-over-HUD behavior cannot work correctly through that path. Numeric `sscanf` conversions are not range-validated.

**Fix:** Validate complete sequences and coordinate ranges; decode button masks/modifiers and legacy coordinates.

## Viewer and PDF correctness

### 16. Zoom limits are actually approximately 10%–510%

**Medium · Reproduced** — `src/main.c:644–647`.

Repeated subtraction/addition accumulates floating-point error. The guard permits an extra step: measured minimum `0.10000000000000014`, maximum `5.0999999999999988`.

**Fix:** Store zoom as integer steps/percent, or clamp the result after arithmetic.

### 17. Page jump/search/TOC can render an unclamped scroll position

**Medium · Reproduced** — `src/main.c:426–448`, `src/main.c:470–498`, `src/main.c:663–665`, `src/main.c:94–97`.

Modal helpers change `virtual_y` and immediately render. If layout dimensions are unchanged, `update_tab_layout` returns before clamping. The event loop clamps later with redraw disabled, leaving the displayed frame inconsistent with the stored position. On a generated two-page landscape PDF, jumping to page 2 produced a different viewport from immediately pressing refresh, despite no intervening navigation.

**Fix:** Clamp scroll position before every render, independent of whether layout recomputation is needed; centralize navigation.

### 18. Active-page definition breaks when multiple small pages fit onscreen

**Medium · Reproduced** — `src/main.c:34–42`, `src/main.c:109–148`.

The page at viewport center is reported as active. With two 612×792 pages at 20% zoom in a 720×432 viewport, the viewer reports page 2 while scroll is zero and both pages fit. Navigation and fit/zoom preservation use this same definition, so they can select an unexpected page.

**Fix:** Define the intended page anchor explicitly (topmost visible, largest visible area, or a persistent user-selected page) and test low zoom/mixed page sizes.

### 19. Fit-height is not guaranteed to fit a page; enlarged content cannot be panned horizontally

**Medium · Code-confirmed** — `src/main.c:125`, `src/main.c:183–189`.

FIT_HEIGHT scales solely by height. A wide/landscape page can exceed terminal width and be center-cropped even at 100%; help calls this “Fit Page.” Zoomed pages are likewise center-cropped, with no horizontal scroll controls to reach the sides.

**Fix:** Use the minimum of width/height scale for true fit-page mode, or accurately label fit-height and provide horizontal panning.

### 20. Zero-page/failed-count/encrypted documents are accepted as valid tabs

**Medium · Reproduced/code-confirmed** — `src/pdf.c:22–24`, `src/main.c:532–549`.

A generated zero-page PDF opens successfully and becomes a blank tab. A page-count exception is silently converted to zero rather than open failure. There is no `fz_needs_password`/authentication flow, so encrypted documents have no useful support or diagnostic. Suppressed errors make these cases look like blank/broken rendering.

**Fix:** Reject unusable documents explicitly, report count failures, and implement password prompting or a clear unsupported-document error.

### 21. Exception paths leak loaded MuPDF resources

**Medium · Code-confirmed** — `src/pdf.c:39–42`, `src/pdf.c:118–121`, `src/pdf.c:142–154`.

Loaded pages are only dropped on the normal path in bounds lookup and search; a later throw skips the drop. Loaded outlines are only dropped after successful traversal. An error on one search page aborts the whole document search and returns partial results without indicating incompleteness.

**Fix:** Use `fz_always` for each owned MuPDF object, with the required `fz_var` declarations, and return explicit failure/partial-result status.

### 22. Unbounded recursive outline traversal risks stack/resource exhaustion

**Medium · Risk/gap** — `src/pdf.c:93–110`, `src/main.c:231–245`.

Traversal recursively descends the entire outline without a depth/item budget. The library may impose some parsing limits, but the wrapper establishes no bound of its own. Counts are eventually narrowed from `size_t` to `int`, and title lengths/indentation also use unchecked integer arithmetic.

**Fix:** Iterative traversal or explicit depth/item/size limits; checked count conversions.

### 23. Search cannot search Unicode text and does not identify occurrences

**Medium · Code-confirmed** — `src/main.c:451–488`, `src/pdf.c:142–151`.

No locale is initialized and prompt input is processed one byte at a time with C-locale `isprint`; UTF-8 input is rejected rather than decoded. Queries are limited to 63 bytes. Search retains only one result per page, discards coordinates, and offers no highlights or per-occurrence navigation. There is no visible no-match/error indication when the HUD is hidden, no explicit way to clear a search by submitting an empty query, and whole-document search blocks input/cancellation.

**Fix:** UTF-8-aware text editing, explicit search state/results/errors, cancellable processing, and occurrence coordinates if match cycling is promised.

### 24. Page-number parsing invokes undefined behavior on overflow

**Medium · Code-confirmed** — `src/main.c:495–497`.

`atoi` is applied to up to 15 arbitrary printable characters. Out-of-range integer conversion is undefined behavior; trailing junk is also accepted (`2abc` becomes 2).

**Fix:** Use `strtol` with `errno`, end-pointer validation, and explicit range checks.

### 25. Text layout uses byte counts, not terminal display width

**Low · Code-confirmed** — `src/main.c:59–91`, `src/main.c:218–225`, `src/main.c:331–347`.

Tab labels can split UTF-8 at 16 bytes. Tab click positions use `strlen`, so multibyte/wide/combining text misaligns hitboxes. TOC truncation avoids cutting continuation bytes but still treats bytes as columns. HUD information is not clipped to the terminal width; narrow terminals lose content.

**Fix:** A shared validated UTF-8/display-width renderer, with clipping and hitbox geometry derived from displayed cells.

### 26. Empty TOC/no-op redraws and tiny terminal layouts are poorly handled

**Low · Code-confirmed** — `src/main.c:234`, `src/main.c:247–448`, `src/main.c:566–574`.

TOC redraws every 500 ms even without input; tiny widths still print fixed strings wider than the terminal. Prompt dimensions are captured once, and a resize event is treated as printable `r` rather than recomputing the prompt position. A one-row terminal with HUD enabled yields zero content height in event-step calculation, unlike the clamped render calculation.

**Fix:** Render modals only when dirty, handle resize as a distinct event, and define minimum usable dimensions consistently.

### 27. Startup/search/scrolling do unnecessary work and have no responsiveness budget

**Medium · Risk/gap** — `src/main.c:34–56`, `src/main.c:176–203`, `src/main.c:530–547`, `src/pdf.c:142–154`.

Startup loads bounds for every page of every document before interaction. Active-page lookup/navigation and visible-page discovery scan linearly. Every scroll re-rasterizes and re-encodes the viewport. All PDF work and output run synchronously, without cancellation or timeout. A complex document or blocked output can make quitting unresponsive. The 64 MiB MuPDF context store is a cache target, not a total memory limit.

**Fix:** Measure representative documents; use indexed page offsets, lazy metadata, reusable display lists/render caches where useful, cancellation, and explicit resource budgets. No numerical performance regression is claimed without benchmarks.

### 28. Tab controls do not provide a reliable documented route to all ten tabs

**Low · Code-confirmed** — `include/pdfcli.h:9`, `src/main.c:59–91`, `src/main.c:589–596`, `src/term.c:141–158`.

Only tabs 1–9 have direct bindings; tab 10 requires cycling, whose extended-key support is terminal-dependent. The documented `gt` fallback does not exist (`g` opens the page prompt). The HUD truncates tabs to preserve space, so later tabs can also be inaccessible by mouse.

**Fix:** Implement a portable next/previous-tab binding, document tab 10 behavior, and allow scrolling/selecting overflowed tabs.

### 29. CLI arguments are silently ignored instead of validated

**Low · Code-confirmed** — `src/main.c:501–531`.

Arguments beginning with `-` are skipped, unknown options produce no error, `--` is not an end-of-options delimiter, and documents above the ten-tab limit are silently ignored. Help advertises `[options]` without implementing normal option parsing. A leading-dash filename requires a `./` workaround.

**Fix:** Parse options explicitly and report unknown options/excess documents.

### 30. Early startup failure does not release opened tabs

**Low · Code-confirmed** — `src/main.c:552`.

Failure to enter raw mode returns without releasing documents and allocations already initialized. The OS reclaims them at process exit, but the lifecycle is inconsistent and this path is not sanitizer-tested.

**Fix:** One cleanup path for initialization failure and normal shutdown.

### 31. Sixel API accepts pixel layouts its implementation does not correctly encode

**Medium · Code-confirmed** — `src/sixel.c:16–29`, `src/sixel.c:32–40`, `src/sixel.c:47–68`.

All pixmaps with `n >= 3` are passed to palette initialization as packed RGB888, even RGBA and CMYK; gray+alpha is treated as packed G8. Later mapping steps by the actual component count, so the two stages disagree. Component count/colorspace, stride size, and allocation-size products are not validated comprehensively. The interactive renderer currently supplies RGB without alpha, so this is mainly an unsafe/incorrect public API contract rather than a demonstrated normal-viewer defect.

**Fix:** Enforce RGB/G8-only input explicitly or convert supported formats correctly; validate component/stride/size contracts with checked arithmetic.

### 32. Custom palette lookup trades color correctness for speed without tests

**Low · Code-confirmed/risk** — `src/sixel.c:46–72`.

The LUT groups colors into 5-bit RGB bins. The first color mapped in a bin determines all later pixels in that bin, even if another palette entry is nearer. The background bin is pre-seeded, which can merge nearby foreground shades into the background. Rendering fidelity of this approximation is not tested.

**Fix:** Document the tradeoff, add image/gradient/small-text regression tests, and measure whether a different lookup scheme is needed.

## Build, tests, dependencies, and documentation

### 33. Incremental builds ignore header and configuration changes

**High · Reproduced/code-confirmed** — `build.sh:21–26`.

Only each `.c` timestamp is compared with its object. Changing `include/pdfcli.h`, dependency headers, `CC`, `CFLAGS`, or `EXTRA_CPPFLAGS` does not recompile objects. Rebuilding the fresh audit objects with `-O0` instead of the original `-O3` issued no compile commands. Header changes can therefore link incompatible stale objects, not merely miss an optimization change.

**Fix:** Compiler-generated header dependencies plus a build-configuration fingerprint, or a standard build system. Separate sanitizer/release object directories.

### 34. Build portability and advertised compiler support are inaccurate

**Medium · Code-confirmed** — `build.sh:5–10`, `README.md:65–68`.

The build hardcodes GNU/ELF linker options, Linux AMD64 static archives, and `-march=native`. It is not a generic portable C23 build, and produced binaries can require the build host's CPU features. GCC 13 uses `-std=c2x`, not the script's `-std=c23`; the documented minimum cannot run the default command as written. The script assumes invocation from the repository root. Python is needed for tests but is not listed among requirements.

**Fix:** Document the actual platform/compiler matrix, choose flags by compiler/platform, provide reproducible dependency builds, avoid native CPU targeting for distributed binaries, and anchor paths to the script location.

### 35. Build configuration overrides are inconsistent and tests can exercise the wrong binary

**Medium · Code-confirmed** — `build.sh:12–14`, `build.sh:37–45`, `build.sh:57`, `tests/test_simulation.py:60`, `tests/test_simulation.py:300`.

`BINARY` is configurable, but PTY tests always execute `./pdf-cli`. They can test an old default executable instead of the newly built one. `./"$BINARY"` also breaks absolute binary paths, and installation concatenates a possibly path-valued BINARY onto PREFIX. Sanitizer compilation does not honor the normal extra flags or CFLAGS, so build modes diverge. Build tool/flag variables are expanded through shell word splitting/globbing rather than robust arrays.

**Fix:** Separate target name from output path, pass the exact built binary to every test, and use consistent compiler/linker configuration.

### 36. Sanitizer success claims exceed actual coverage

**Medium · Code-confirmed** — `build.sh:39–44`, `tests/test_pdfcli.c`.

The sanitizer test executable excludes `src/main.c`, where layout, prompts, navigation, and many allocations live. PTY tests run the normal binary. Bundled static libraries are not sanitizer-instrumented. UBSan normally recovers and can exit successfully after diagnostics, while the script prints “0 errors” solely from exit status. This audit's direct bounds probe reported UB while exiting 0.

**Fix:** Run the full application under sanitizers, make UB fail tests (`-fno-sanitize-recover=all` or appropriate runtime options), and state dependency-instrumentation limitations accurately.

### 37. Test fixtures depend on personal files and use unsafe fixed temporary paths

**Medium · Code-confirmed** — `tests/test_pdfcli.c:42`, `tests/test_pdfcli.c:127–163`, `tests/test_simulation.py:51`, `tests/test_simulation.py:292–294`.

Tests write predictable `/tmp/test_pdfcli_sample.pdf` and `/tmp/sim_test.pdf` with normal `fopen`/`open`: concurrent runs race, and pre-existing symlinks can redirect writes. Major mixed-page and TOC coverage relies on `/home/aditya/Desktop/Sem-5/EO-301/Module-2.pdf`; on other machines it silently skips. The C test still prints PASSED when that file is absent. Its `page_count >= 2` check is insufficient before testing page index 2.

**Fix:** Per-run private temporary directories, cleanup in all paths, self-contained generated/committed fixtures, and explicit skips/failures with correct preconditions.

### 38. Existing tests overstate behavioral guarantees

**Medium · Code-confirmed** — `tests/test_pdfcli.c`, `tests/test_simulation.py`.

Many PTY assertions only establish that the process is alive or some bytes were emitted, not that the right state/image resulted. No self-contained coverage exists for multi-tab close/switch behavior, allocation failure, hostile outline/filename text, split/coalesced escape input, EOF/HUP, malformed/extreme documents, password handling, Unicode, zero-page PDFs, zoom boundaries, oversized page jumps, or output failure. Fixed sleeps and undrained output during bursts can make tests timing-sensitive. PDF test fixtures contain hardcoded inaccurate xref/stream metadata and exercise MuPDF repair rather than a clean-file baseline. Python `assert` checks disappear under `python -O`; C assertions disappear with `NDEBUG`.

**Fix:** Deterministic fixtures and explicit checks, event-driven PTY synchronization, state and image-level assertions, fuzz/property tests for parsers/arithmetic, and fault injection. Keep malformed-PDF fixtures as a separate intentional category.

### 39. Bundled binary dependencies lack reproducible provenance and update automation

**Medium · Risk/gap** — `depends/go-fitz/libs/`, `depends/libsixel/libsixel.a`, repository metadata.

Headers identify MuPDF 1.28.0 and libsixel 1.10.5, but no upstream revision pin/build recipe/checksum manifest establishes exactly how archives and third-party components were built. There is no CI, automated dependency checking, or visible security reporting/update policy. Archive source is unavailable for a comprehensive source audit. No specific known CVE is asserted here.

**Fix:** Pin and reproduce dependency builds, record sources/licenses/versions/toolchains, create an SBOM, and establish CI plus security maintenance.

### 40. README and supplementary documents do not describe current behavior reliably

**Low · Code-confirmed** — `README.md:11–25`, `README.md:38–59`, `README.md:99`, supplementary root Markdown documents.

- `d` scrolls; `D` toggles dark mode.
- Esc hides the HUD rather than quitting.
- `gt` is not implemented.
- Space/PgDn move about 35% of the viewport, not a full page height.
- HUD is hidden initially, despite the prominent tab-bar description.
- Search cycles matching pages, not occurrences; `p` does not reverse search.
- Four implementation files total 1,084 lines, exceeding the repeated under-1,000 claim; compile-time and “instant” performance claims lack a reproducible benchmark.
- `pdf-cli $(fzf)` splits filenames on whitespace and expands glob patterns; use `pdf-cli "$(fzf)"` with cancellation handling for a single selection.
- Large historical analysis/planning files refer to deleted subsystems and should be clearly archived/labeled as historical, not current architecture.

**Fix:** Reconcile documentation with executable behavior and separate historical design notes from maintained user/developer documentation.

## Suggested order of work

1. **Release/security blockers:** establish license compliance; sanitize all untrusted terminal text; repair allocation/reallocation ownership, numeric conversions, and `isprint` misuse.
2. **Core reliability:** incremental input parser; caught MuPDF errors and unconditional cleanup; clamp all navigation; enforce exact zoom limits; fix incremental builds.
3. **Regression foundation:** self-contained fixtures, parser fragmentation/batching tests, full-app fatal sanitizers, allocation failure tests, and CI.
4. **Usability/maintenance:** Unicode input/display, search feedback/cancellation, password handling, horizontal navigation, terminal capability handling, portable builds, dependency provenance, and documentation.

The passing baseline suite does not invalidate the reproduced findings; its current coverage simply does not exercise them.
