<h1 align=center>pdf-cli</h1>
<div align=center>

![C Version](https://img.shields.io/badge/C-C23%20Standard-00599C?style=for-the-badge&logo=c&labelColor=181825)
![Stars](https://img.shields.io/github/stars/Aditya-233/pdf-cli?style=for-the-badge&labelColor=181825&color=f9e2af)
![Latest Release](https://img.shields.io/github/v/release/Aditya-233/pdf-cli?style=for-the-badge&labelColor=181825&color=b4befe)
![Last Commit](https://img.shields.io/github/last-commit/Aditya-233/pdf-cli?style=for-the-badge&labelColor=181825&color=a6e3a1)

</div>

A lightweight, ultra-fast terminal PDF viewer written in modern **C23 (ISO/IEC 9899:2024)**. Designed for speed, minimalism (< 1,000 LOC), and native high-resolution **Sixel** graphics rendering in terminals like **Foot**.

---

## Features

- **Blazing Fast (< 1,000 LOC)**: Pure, zero-bloat C23 engine compiling in < 0.5s.
- **Native Sixel Graphics**: Hardware-crisp, high-DPI rasterization tailored for Wayland terminal emulators (e.g. Foot).
- **Multi-Document Tabs**: Open up to 10 documents concurrently with a row 1 tab bar, switching via `1`..`9` or `Ctrl+1`..`Ctrl+9`, and closing with `Ctrl+W`.
- **Continuous Vertical Scroll**: Smooth, seamless vertical scrolling in Fit Width mode flowing across page boundaries.
- **Fit Modes**: Instant toggle between continuous Fit Width and page-fitting Fit Height (`f`).
- **Interactive Table of Contents**: Jump to bookmarks and outline chapters (`T`).
- **In-Document Text Search**: Fast text occurrence search and match cycling (`/`, `n`, `N`).
- **Dark Mode / Invert**: Instant 24-bit RGB pixel inversion for comfortable dark room reading (`d`).
- **Smooth Zoom**: Real-time zoom adjustment from 20% to 500% (`+`, `-`, `0`).
- **Mouse Wheel Navigation**: Responsive mouse wheel scrolling.

---

## Keyboard & Mouse Shortcuts

### Tab Navigation

| Key | Action |
| :--- | :--- |
| `1` .. `9` | Switch directly to tab 1 through 9 (when multiple tabs open) |
| `Ctrl+1` .. `Ctrl+9` | Direct tab jump (or `Alt+1` .. `Alt+9` fallback) |
| `Ctrl+Tab` / `gt` | Cycle to next tab |
| `Ctrl+W` | Close active tab (exits viewer if last tab closed) |

### Document & Page Navigation

| Key / Mouse Action | Action |
| :--- | :--- |
| `j` / `Down` / `WheelDown` | Scroll down (line step) / next page |
| `k` / `Up` / `WheelUp` | Scroll up (line step) / previous page |
| `Space` / `PgDn` | Smooth page-height continuous scroll down |
| `PgUp` | Smooth page-height continuous scroll up |
| `n` / `p` | Next / previous page (or cycle search matches) |
| `g` | Quick page jump (prompts for page number) |
| `T` | Table of contents outline modal (`Enter` jumps, `q`/`Esc` cancels) |
| `/` | In-document text search prompt (`Enter` executes search) |
| `f` | Toggle fit mode (Fit Width vs. Fit Height) |
| `d` | Toggle dark mode (instant color inversion) |
| `+` / `=` | Zoom in (+10%) |
| `-` / `_` | Zoom out (-10%) |
| `0` | Reset zoom to 100% |
| `r` | Refresh display & re-detect terminal dimensions |
| `q` / `Esc` | Quit viewer |

---

## Installation & Building

### Requirements
- **C23 Compliant Compiler**: GCC 13+ or Clang 18+
- **Host Terminal**: Foot, xterm, or any terminal supporting Sixel graphics
- Static libraries for MuPDF and libsixel (bundled in `depends/`)

### Building from Source

```bash
# Clone the repository
git clone https://github.com/Aditya-233/pdf-cli
cd pdf-cli

# Compile with maximum strictness (-Wall -Wextra -Wpedantic -Werror)
./build.sh build

# Run unit tests and AddressSanitizer checks
./build.sh test

# Install to ~/.local/bin/pdf-cli
./build.sh install
```

---

## Usage

```bash
# Open a single PDF
pdf-cli document.pdf

# Open multiple PDFs in tabs
pdf-cli doc1.pdf doc2.pdf doc3.pdf

# Quick fuzzy search and launch via fzf
pdf-cli $(fzf)
```

---

## License

MIT
