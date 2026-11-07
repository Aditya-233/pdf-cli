<h1 align=center>pdf-cli</h1>
<div align=center>

![C Version](https://img.shields.io/badge/C-C23%20Standard-00599C?style=for-the-badge&logo=c&labelColor=181825)
![Stars](https://img.shields.io/github/stars/Aditya-233/pdf-cli?style=for-the-badge&labelColor=181825&color=f9e2af)
![Latest Release](https://img.shields.io/github/v/release/Aditya-233/pdf-cli?style=for-the-badge&labelColor=181825&color=b4befe)
![Last Commit](https://img.shields.io/github/last-commit/Aditya-233/pdf-cli?style=for-the-badge&labelColor=181825&color=a6e3a1)

</div>

A lightweight, ultra-fast terminal PDF viewer written in modern **C23 (ISO/IEC 9899:2024)**. Designed for speed, minimal resource footprint, and native high-resolution **Sixel** graphics rendering in terminals like **Foot**, **xterm**, and other Sixel-capable terminal emulators.

---

## Features

- **Pure C23 Engine**: Clean, modular C23 implementation compiling cleanly with zero warnings under `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror` and GCC `-fanalyzer`.
- **Native Sixel Graphics**: High-DPI rasterization tailored for Wayland and X11 terminal emulators (e.g. Foot, xterm).
- **Multi-Document Tabs**: Open up to 10 documents concurrently with a row 1 tab bar, switching via `1`..`9`, `0` (tab 10), or `Ctrl+1`..`Ctrl+9`, and closing with `Ctrl+W`.
- **Continuous Vertical Scroll**: Smooth, seamless vertical scrolling in Fit Width mode flowing across page boundaries.
- **Fit Modes**: Instant toggle between continuous Fit Width and true dual-axis Fit Page (`f`).
- **Interactive Table of Contents**: Jump to bookmarks and outline chapters (`T`).
- **In-Document Text Search**: Fast text occurrence search and match cycling with occurrence counts and visual feedback (`/`, `n`, `p` / `N`).
- **Dark Mode / Invert**: Instant 24-bit RGB pixel inversion for comfortable dark room reading (`D`).
- **Exact Zoom**: Real-time zoom adjustment from 20% to 500% in exact 10% steps (`+`, `-`, `0`).
- **Mouse & Trackpad Support**: Responsive mouse wheel scrolling and tab click navigation.
- **Terminal Injection Immune**: Centralized UTF-8 sanitization prevents ANSI/OSC escape sequence injection from hostile document filenames and outline metadata.

---

## Keyboard & Mouse Shortcuts

### Tab Navigation

| Key / Mouse Action | Action |
| :--- | :--- |
| `1` .. `9` | Switch directly to tab 1 through 9 (when multiple tabs open) |
| `0` | Switch directly to tab 10 (when 10 tabs open) |
| `Ctrl+1` .. `Ctrl+9` | Direct tab jump |
| `]` / `Ctrl+Tab` | Cycle to next tab |
| `[` / `Ctrl+Shift+Tab` | Cycle to previous tab |
| `Ctrl+W` | Close active tab (exits viewer if last tab closed) |
| Mouse Left Click (Row 1) | Click on tab name in HUD to switch to it |

### Document & Page Navigation

| Key / Mouse Action | Action |
| :--- | :--- |
| `j` / `Down` / `WheelDown` | Continuous scroll down (line step) |
| `k` / `Up` / `WheelUp` | Continuous scroll up (line step) |
| `Space` / `PgDn` / `d` | Scroll down screen step (~35% viewport height) |
| `PgUp` / `u` | Scroll up screen step (~35% viewport height) |
| `h` / `l` / `Left` / `Right` | Pan horizontally when zoomed |
| `n` / `J` | Next page (or cycle to next search match) |
| `p` / `K` / `N` | Previous page (or cycle to previous search match) |
| `Home` / `<` | Jump to start of document |
| `End` / `>` / `G` | Jump to end of document |
| `i` | Toggle top Tab bar / HUD display |
| `g` | Quick page jump (prompts for page number) |
| `T` | Table of contents outline modal (`Enter` jumps, `q`/`Esc` cancels) |
| `/` | In-document text search prompt (`Enter` executes, empty clears) |
| `f` | Toggle fit mode (Fit Width vs. Fit Page) |
| `D` | Toggle dark mode (instant color inversion) |
| `+` / `=` | Zoom in (+10%, up to 500%) |
| `-` / `_` | Zoom out (-10%, down to 20%) |
| `0` | Reset zoom to 100% |
| `r` | Refresh display & re-detect terminal dimensions |
| `Esc` | Dismiss top HUD or cancel active modal prompt |
| `q` / `Ctrl+C` | Quit viewer |

---

## Installation & Building

### Requirements
- **C23 Compliant Compiler**: GCC 13+ or Clang 18+
- **Host Terminal**: Foot, xterm, or any terminal supporting Sixel graphics
- **System Libraries**: `libjpeg` (`libjpeg-dev`), `zlib` (`zlib1g-dev`), `libm`, `pthread`
- Bundled C dependencies in `depends/` (MuPDF, libsixel)

### Building from Source

```bash
# Clone the repository
git clone https://github.com/Aditya-233/pdf-cli
cd pdf-cli

# Compile with maximum strictness (-Wall -Wextra -Wpedantic -Werror)
./build.sh build

# Run unit tests, AddressSanitizer/UBSan, PTY simulation, and linters (GCC -fanalyzer & Clangd)
./build.sh test

# Run static analysis and language server checks
./build.sh lint

# Install to ~/.local/bin/pdf-cli
./build.sh install
```

---

## Usage

```bash
# Open a single PDF
pdf-cli document.pdf

# Open in dark mode
pdf-cli -d document.pdf

# Open multiple PDFs in tabs
pdf-cli doc1.pdf doc2.pdf doc3.pdf

# Safe fuzzy search and launch via fzf (with quoting and cancellation check)
file="$(fzf)" && [ -n "$file" ] && pdf-cli "$file"
```

---

## Licensing & Dependencies

- **pdf-cli**: Original work under the MIT License.
- **MuPDF**: Bundled in `depends/go-fitz/` under GNU AGPLv3 (Artifex Software, Inc.).
- **libsixel**: Bundled in `depends/libsixel/` under the MIT License.
