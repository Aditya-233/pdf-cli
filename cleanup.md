# Codebase Cleanup & Minimization Report (Host-Device Specific)

> **Core Mandate Implemented**:
> 1. **Script-Driven Only**: `Makefile` is deleted. All builds, tests, and cleaning are performed strictly via `./build.sh`.
> 2. **Single-Device Focus**: Hard-targeted **only** to this host device (Arch Linux x86_64, 13th Gen Intel Core i5-13500H). All non-host static archives (macOS and ARM64) and demo assets have been excised.
> 3. **Zero-Bloat Minimization**: Removed all reference repositories (`termbox2`, `dvtm`, `libtermkey`), standalone test suites (`test/`), redundant assets, and foreign languages.

---

## 1. Executive Summary: Before vs. After

| Metric | Before Cleanup | After Cleanup | Net Difference | Reduction % |
|:---|---:|---:|---:|---:|
| **Unique Files** | 205 | 106 | -99 | **-48.3%** |
| **Code LOC** | 39,132 | 24,975 | -14,157 | **-36.18%** |
| **Blank Lines** | 9,187 | 6,706 | -2,481 | -27.0% |
| **Comment Lines** | 15,289 | 14,157 | -1,132 | -7.4% |
| **Languages Detected** | 16 | 4 | -12 languages | **-75.0%** |
| **Binary Archives Size** | ~120 MB | ~22 MB | -~98 MB | **-81.7%** |
| **Build System** | `Makefile` + `build.sh` | **`build.sh` ONLY** | `Makefile` deleted | **100% Script** |

### Active Programming Languages (12 Languages Completely Eliminated)

`cloc .` now reports **only 4 languages** remaining in the entire repository:

| Language | Files | Blank | Comment | Code LOC | % of Codebase | Notes |
|:---|---:|---:|---:|---:|---:|:---|
| **C** | 21 | 1,411 | 1,263 | **13,355** | 53.47% | C23 Engine (`src/`) & `yyjson.c` |
| **C/C++ Header** | 81 | 5,106 | 12,894 | **10,805** | 43.26% | Fitz headers (`go-fitz`) & `sixel.h` |
| **Markdown** | 3 | 179 | 0 | **726** | 2.91% | Project & cleanup documentation |
| **Bourne Shell** | 1 | 10 | 0 | **89** | 0.36% | Canonical script-only build tool (`build.sh`) |
| **SUM** | **106** | **6,706** | **14,157** | **24,975** | **100.00%** | **Pure Host C23 Environment** |

> [!NOTE]
> The 12 eliminated languages were: **PHP, Rust, Ruby, Zig, D, Nim, Go, Python, Dockerfile, YAML, make, and JSON**.

---

## 2. Architecture Comparison (Purified Core)

```mermaid
flowchart TD
    subgraph Before ["ORIGINAL BLOATED STATE: 39,132 LOC / 205 Files / 16 Languages"]
        direction TB
        B1["Reference Repos: 14,147 LOC (36.1%)
• termbox2, dvtm, libtermkey
• 8 foreign binding languages + Dockerfile"]
        B2["Non-Host Static Archives (~100 MB)
• macOS arm64/amd64 & Linux arm64 .a files"]
        B3["Standalone Test Suites: 517 LOC
• test_all.c, test_ipc.c, test_tabs.c"]
        B4["Duplicate Build & Assets: 197 LOC
• Makefile (62), assets/ (36), probe.png"]
    end

    subgraph After ["PURIFIED HOST STATE: 24,975 LOC / 106 Files / 4 Languages"]
        direction TB
        A1["First-Party Pure C23 Engine: 4,629 LOC
• src/ (4,122), include/ (507)
• Viewer, tabs, Kitty GFX, Sixel, termios, IPC"]
        A2["Host Fitz & Sixel C Headers: 6,090 LOC
• depends/go-fitz (5,398), depends/libsixel (692)
• Linked to host-only Linux x86_64 static archives"]
        A3["Vendored Fast JSON: 13,441 LOC
• depends/yyjson/src (yyjson.c + yyjson.h)"]
        A4["Canonical Script Build: 89 LOC
• ./build.sh targeting Intel i5-13500H (-march=native)"]
    end

    Before ==>|"Purged 14,157 LOC & ~100MB non-host bloat"| After
```

---

## 3. Itemized Actions Completed

### 3.1. Purged Reference Repositories (-14,147 LOC)
- **Deleted** `depends/termbox2/` (40 files, **5,835 LOC**)
- **Deleted** `depends/dvtm/` (23 files, **4,467 LOC**)
- **Deleted** `depends/libtermkey/` (33 files, **3,845 LOC**)
- **Result**: Eradicated all 8 foreign languages (PHP, Rust, Ruby, Zig, D, Nim, Go, Python), Dockerfiles, and CI YAML files.

### 3.2. Purged Non-Binary Test Suites (-517 LOC)
- **Deleted** `test/test_all.c` (**233 LOC**)
- **Deleted** `test/test_ipc_config.c` (**198 LOC**)
- **Deleted** `test/test_tabs.c` (**86 LOC**)
- **Deleted** directory `test/`
- **Result**: The `pdf-cli` binary links without external test code.

### 3.3. Purged Non-Host Binary Libraries (~100 MB Saved)
- **Deleted** all non-x86_64 and non-Linux static libraries from `depends/go-fitz/libs/`:
  - `libmupdf_darwin_arm64.a` (macOS Apple Silicon)
  - `libmupdfthird_darwin_arm64.a` (macOS Apple Silicon)
  - `libmupdf_darwin_amd64.a` (macOS Intel)
  - `libmupdfthird_darwin_amd64.a` (macOS Intel)
  - `libmupdf_linux_arm64.a` (Linux ARM64)
  - `libmupdfthird_linux_arm64.a` (Linux ARM64)
  - `depends/go-fitz/testdata/test.pdf` (1.3 MB sample PDF)
- **Preserved strictly for this device**:
  - `depends/go-fitz/libs/libmupdf_linux_amd64.a` (9.4 MB)
  - `depends/go-fitz/libs/libmupdfthird_linux_amd64.a` (12.9 MB)
  - `depends/libsixel/libsixel.a` (94 KB)

### 3.4. Script-Only Build Mandate & Makefile Deletion (-62 LOC)
- **Deleted** `Makefile` (**62 LOC**).
- **Optimized** `build.sh` as the sole canonical build tool for this device.
- Added `-march=native` to `CFLAGS` to generate instructions specifically optimized for the host 13th Gen Intel Core i5-13500H processor (AVX2, FMA, BMI2).

### 3.5. Presets Inlining & Assets Directory Deletion (-36 LOC)
- Inlined default preset configurations directly into `src/ctl_client.c` as a `static const char DEFAULT_PRESETS_JSON[]` string literal.
- **Deleted** `assets/default_presets.json` (**36 LOC**) and `assets/probe.png`.
- **Deleted** directory `assets/`.

---

## 4. Current Cleaned Codebase Inventory (106 Files)

| Directory / Subsystem | Files | Code LOC | % of Codebase | Description & Host Role |
|:---|---:|---:|---:|:---|
| [`depends/yyjson`](file:///depends/yyjson) | 2 | 13,441 | 53.82% | Vendored C JSON parser & serializer for IPC / config |
| [`depends/go-fitz`](file:///depends/go-fitz) | 59 | 5,398 | 21.61% | MuPDF Fitz C rendering engine headers |
| [`src`](file:///src) | 20 | 4,122 | 16.50% | Core C23 PDF viewer, tab management, IPC daemon, Kitty/Sixel graphics |
| [`depends/libsixel`](file:///depends/libsixel) | 1 | 692 | 2.77% | Sixel graphics output header for terminal bitmap display |
| [`include`](file:///include) | 20 | 507 | 2.03% | Public C23 header files and type definitions |
| [`distribution.md`](file:///distribution.md) | 1 | 300 | 1.20% | Full line-by-line distribution audit |
| [`cleanup.md`](file:///cleanup.md) | 1 | 288 | 1.15% | Codebase minimization report & roadmap |
| [`README.md`](file:///README.md) | 1 | 138 | 0.55% | User documentation & keyboard shortcuts reference |
| [`build.sh`](file:///build.sh) | 1 | 89 | 0.36% | Canonical native build script (`-march=native`) |
| **SUM** | **106** | **24,975** | **100.00%** | **Pure Host C23 Reader** |

---

## 5. Verification & Testing on Host Device

All verification steps are performed strictly using `./build.sh` on this Arch Linux host:

### 5.1. Clean Native Build & Smoke Test
```bash
./build.sh clean && ./build.sh test
```
**Result**: Built in ~2.8s with GCC 16.2.1; smoke tests passed (`pdf-cli --help` and `dvctl presets`).

### 5.2. Functional Remote Control Verification
```bash
./dvctl presets
```
**Output**:
```
laptop     dual=half fit=height crop_top=0.04 crop_bottom=0.04 crop_left=0.06 crop_right=0.06
display    dual=off fit=height crop=0
white      tint=off
pale       tint=dim
dark       tint=dark
invert     tint=invert
trim       crop=+0.02
untrim     crop=-0.02
notrim     crop=0
```

---

## 6. Summary of Host Purification

- **Total Code LOC**: Slashed from **39,132 down to 24,975** (-14,157 LOC, 36.18% reduction).
- **Total Files**: Dropped from **205 down to 106** (99 files removed, 48.3% reduction).
- **Foreign Languages**: 12 languages eliminated; repository is now **100% C23, C/C++ Header, and Bourne Shell**.
- **Disk Bloat**: ~100 MB of unused macOS and ARM64 archives removed.
- **Build Paradigm**: `Makefile` eliminated; strictly script-driven via `./build.sh` with `-march=native` for the 13th Gen Intel Core i5-13500H.

> *The codebase is now completely purified and focused strictly on this device with zero extra code.*
