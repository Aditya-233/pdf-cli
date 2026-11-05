# Dependencies and Licensing Notice

This document details third-party components linked by or bundled with `pdf-cli`, their respective licenses, provenance, corresponding source provisions, and reproducible build instructions.

---

## 1. Summary of Components & Licensing

| Component | Bundled Version | License | Origin / Upstream | Role in `pdf-cli` |
|:---|:---|:---|:---|:---|
| **pdf-cli** (First-Party Code) | 0.2.0 | MIT | Original work | Core viewer application |
| **MuPDF** (`libmupdf`, `libmupdfthird`) | 1.28.0 | AGPL-3.0-or-later (or Commercial) | [mupdf.com](https://mupdf.com) / [Artifex Software](https://github.com/ArtifexSoftware/mupdf) | PDF rasterization, font decoding, outline & text parsing |
| **libsixel** | 1.10.5 | MIT / Public Domain | [libsixel](https://github.com/libsixel/libsixel) | Sixel graphics terminal protocol encoder |

---

## 2. Licensing Compliance & Corresponding Source

### MuPDF (Artifex Software, Inc.)
- **License**: GNU Affero General Public License v3.0 or later (`AGPL-3.0-or-later`), unless licensed under commercial terms from Artifex Software, Inc.
- **Copy of License**: Bundled in [depends/go-fitz/COPYING](depends/go-fitz/COPYING).
- **Corresponding Source**:
  In compliance with GNU AGPLv3 Section 6, the exact corresponding source for the bundled MuPDF build is available upstream:
  - Repository: `git://git.ghostscript.com/mupdf.git` or `https://github.com/ArtifexSoftware/mupdf`
  - Release Tag: `1.28.0`
  - Upstream Tarball: `https://mupdf.com/downloads/archive/mupdf-1.28.0-source.tar.gz`

### libsixel (Hayaki Saito)
- **License**: MIT License / Public Domain.
- **Copy of License**: Bundled in [depends/libsixel/LICENSE](depends/libsixel/LICENSE).
- **Upstream Repository**: `https://github.com/libsixel/libsixel`

---

## 3. Checksums of Bundled Static Archives

| File | SHA-256 Checksum |
|:---|:---|
| `depends/libsixel/libsixel.a` | `94e422fc07f0c073148f28ed2274c8fb408ccc730d804799f4ab2073ffe321af` |
| `depends/go-fitz/libs/libmupdf_linux_amd64.a` | `d19c82fa9a439d2fbf2def09699e8ff30bc9398bdedef0da7f44fe5223040ced` |
| `depends/go-fitz/libs/libmupdfthird_linux_amd64.a` | `472a2285034a63f0c03a4cc36ecda89cc8fdc9f970c97c47a8ed0b2e1ca1e16e` |

---

## 4. Reproducible Build Recipes

### Rebuilding MuPDF 1.28.0 Static Libraries:
```bash
git clone --recursive --branch 1.28.0 https://github.com/ArtifexSoftware/mupdf.git
cd mupdf
make -j$(nproc) HAVE_X11=no HAVE_GLUT=no build=release libs
# Generated archives:
# build/release/libmupdf.a -> depends/go-fitz/libs/libmupdf_linux_amd64.a
# build/release/libmupdfthird.a -> depends/go-fitz/libs/libmupdfthird_linux_amd64.a
```

### Rebuilding libsixel 1.10.5 Static Library:
```bash
git clone --branch v1.10.5 https://github.com/libsixel/libsixel.git
cd libsixel
meson setup builddir --default-library=static -Dtests=disabled
meson compile -C builddir
# Generated archive:
# builddir/src/libsixel.a -> depends/libsixel/libsixel.a
```

---

## 5. Security & Maintenance Policy

- Dependencies are audited and pinned.
- Automated CI builds run static analysis and sanitizers on all pull requests and commits.
- Security vulnerabilities in dependencies should be reported via GitHub Security Advisories.
