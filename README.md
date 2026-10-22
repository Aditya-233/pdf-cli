<h1 align=center>pdf-cli</h1>
<div align=center>

![C Version](https://img.shields.io/badge/C-C23%20Standard-00599C?style=for-the-badge&logo=c&labelColor=181825)
![Stars](https://img.shields.io/github/stars/Aditya-233/pdf-cli?style=for-the-badge&labelColor=181825&color=f9e2af)
![Latest Release](https://img.shields.io/github/v/release/Aditya-233/pdf-cli?style=for-the-badge&labelColor=181825&color=b4befe)
![Last Commit](https://img.shields.io/github/last-commit/Aditya-233/pdf-cli?style=for-the-badge&labelColor=181825&color=a6e3a1)

</div>
A terminal-based PDF, EPUB, DOCX, HTML, PNG, and JPEG viewer with fuzzy file search, high-resolution rendering, fast page caching, LaTeX auto-reload, intelligent text reflow, and multi-page viewing modes.

<https://github.com/user-attachments/assets/7ba77b3f-7f7a-48aa-bf70-1f432650bdf1>

## Features

- **Multi-Document Tab Support**: Open up to 10 documents concurrently with tab bar on row 1, seamless `Ctrl+1`..`Ctrl+9` switching, and mouse click / wheel navigation
- **Fuzzy File Search**: Interactive file picker with fuzzy search to quickly find your PDFs and EPUBs
- **Smart Content Detection**: Automatically detects and displays text, images, or mixed content pages
- **High-Resolution Image Rendering**: Uses terminal graphics protocols (Sixel/Kitty/iTerm2) for crisp image display
- **Render Cache and Prefetch**: Caches rendered pages and prepares neighboring pages in the background
- **Half Page View**: Supports screen splitting to display half pages at high quality
- **Image Invert**: Inverts the Image while preserving the core colors of the image.
- **HiDPI/Retina Support**: Dynamic cell size detection for sharp rendering on high-DPI displays
- **Auto-Reload**: Automatically reloads when the PDF changes (perfect for LaTeX compilation with `latexmk -pvc`)
- **Fit Modes**: Toggle between height-fit, width-fit, and auto-fit modes
- **Manual Zoom**: Adjust zoom from 10% to 200%
- **In-Document Search**: Search for text within documents
- **Navigation Tools**: Follow document links, browse jump history, and fuzzy-search the table of contents
- **Viewer Control & IPC**: Inspect and control running viewers with `dvctl` or `pdf-cli ctl`, including remote tab switching and opening
- **Intelligent Text Reflow**: Automatically reformats text to fit your terminal width while preserving paragraphs
- **Terminal-Aware**: Detects your terminal type and optimizes rendering accordingly
- **Multiple Formats**: Supports PDF, EPUB, DOCX, HTML, PNG, and JPEG files

## Keyboard & Mouse Shortcuts

### Tab Navigation

| Key / Mouse Action | Action |
|--------------------|--------|
| `1` .. `9` | Switch directly to tab 1 through 9 (when multiple tabs open) |
| `Ctrl+1` .. `Ctrl+9` | Switch directly to tab 1 through 9 (or `Alt+1` .. `Alt+9` fallback) |
| `Ctrl+0` | Switch to tab 10 / last active tab (or `Alt+0` fallback) |
| `Ctrl+Tab` / `gt` | Cycle to next tab |
| `Ctrl+Shift+Tab` / `gT` | Cycle to previous tab |
| `Ctrl+T` | Open file picker to launch a document in a new tab |
| `Ctrl+W` | Close active tab (exits if last tab) |
| `Mouse Left` on Tab Label | Switch to clicked tab |
| `Mouse Left` on Tab `×` | Close clicked tab |
| `Mouse Wheel` on Tab Bar | Cycle forward/backward through tabs |

### Document & Page Navigation

| Key | Action |
|-----|--------|
| `j` / `Space` / `Down` / `Right` | Next page / smooth scroll down (fit-width) |
| `k` / `Up` / `Left` | Previous page / smooth scroll up (fit-width) |
| `PgDn` / `PgUp` | Smooth page-height continuous scrolling |
| `[` / `]` | Decrease / increase scroll ratio (10% to 80%) |
| `g` | Go to specific page (or type `gt`/`gT` for tabs) |
| `T` | Table of contents overlay (fuzzy search, Enter jumps) |
| `Ctrl+Click` / `Left Click` | Follow links, references, citations, and URLs |
| `Cmd+Left` / `Cmd+Right` | Move backward or forward through jump history |
| `b` | Back to file picker |
| `f` | Toggle fit mode (Fit width continuous scroll vs. Fit height) |
| `D` | Cycle page tint (white / dim / dark / invert) |
| `+` / `=` | Zoom in (+10%) |
| `-` | Zoom out (-10%) |
| `i` | Toggle document info & top tab bar HUD |
| `r` | Refresh display (re-detect cell size) |
| `h` | Show help modal |
| `q` | Quit viewer |

## Installation

### NixOSNixOS Installation

in your configuration.nix file add pdf-cli

```nix
environment.systemPackages = with pkgs; [
  pdf-cli
];
```

### Arch Linux Installation

Using yay

```bash
yay -S pdf-cli
```

Using paru

```
paru -S pdf-cli
```

### Building from source

```bash
# Clone this repository
git clone https://github.com/Aditya-233/pdf-cli
cd pdf-cli

# Build using build.sh or make
./build.sh build

# Run test suite
./build.sh test

# Install pdf-cli and the dvctl symlink to ~/.local/bin
./build.sh install
# Or with make:
# make && make install
```

### Usage

```bash
# Search current directory (default)
pdf-cli

# Search specific directory
pdf-cli ~/Documents/papers/

# Open a specific file directly
pdf-cli paper.pdf

# Open multiple documents in tabs
pdf-cli doc1.pdf doc2.pdf doc3.pdf

# Inspect a running viewer
pdf-cli ctl status

# Control tabs via dvctl / IPC
dvctl tab list
dvctl tab next
dvctl tab prev
dvctl tab switch 2
dvctl tab open reference.pdf
dvctl tab close
```

## LaTeX Workflow

The auto-reload feature makes this viewer ideal for LaTeX editing:

1. Open your PDF: `pdf-cli paper.pdf`
2. Run LaTeX compiler in another terminal: `latexmk -pvc paper.tex`
3. The viewer automatically reloads when the PDF updates, preserving your page position

The viewer handles partially-written PDFs gracefully, waiting for the file to stabilize before reloading.

## Dependencies

- **C23 Compliant Compiler** (GCC 13+ or Clang 18+)
- [MuPDF](https://mupdf.com/) (Fitz) - High-performance document parsing & rasterization
- [libsixel](https://github.com/libsixel/libsixel) - Terminal Sixel image encoding
- [yyjson](https://github.com/ibireme/yyjson) - High-performance JSON parser & serializer for presets & IPC

## Supported Terminals

Optimized for terminals with graphics support:

- **Kitty** (recommended) - Native cell size detection via escape sequences
- Foot
- WezTerm
- iTerm2
- Alacritty
- xterm (with Sixel support)

Works in any terminal, but image rendering quality depends on terminal capabilities.

## How It Works

The reader scans the current directory (or specified directory) for supported documents and images. Use the fuzzy search to quickly filter and select a file. The viewer intelligently detects whether document pages contain text, images, or both, and renders them appropriately for terminal display.

PDFs are rendered as images by default (essential for math, diagrams, and formatted content) at a DPI calculated to match your terminal's pixel dimensions for optimal sharpness.

## License

MIT

---

By [Yujon Pradhananga](https://github.com/Yujonpradhananga)
