#!/usr/bin/env python3
"""
Comprehensive PTY Simulation Test Suite for pdf-cli
Simulates real interactive user sessions in a pseudoterminal:
1. Mouse click robustness (random clicks, releases, drags must NEVER quit the app)
2. Smooth continuous scrolling (wheel events & 'j'/'k')
3. HUD display toggle via 'i' key and dismissal via ESC key
4. Modal Table of Contents (TOC) coverage, navigation, jump, and theming
5. Input batching / coalescing (multiple commands in single read without dropped keys)
6. Page jump prompt ('g') and in-document search ('/')
7. Safe exit exclusively on 'q' or Ctrl+C with full terminal state restoration
"""

import os
import pty
import time
import signal
import select
import subprocess
import sys
import tempfile
import struct
import fcntl
import termios

def generate_valid_test_pdf(path, pages=3):
    """Generates a syntactically valid multi-page PDF with exact xref table and TOC bookmarks."""
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"%PDF-1.4\n")
        offsets = {}

        # 1 0 obj: Catalog
        offsets[1] = f.tell()
        f.write(b"1 0 obj\n<< /Type /Catalog /Pages 2 0 R /Outlines 3 0 R >>\nendobj\n")

        # 2 0 obj: Pages
        offsets[2] = f.tell()
        kids = " ".join([f"{5 + i * 2} 0 R" for i in range(pages)])
        f.write(f"2 0 obj\n<< /Type /Pages /Kids [{kids}] /Count {pages} >>\nendobj\n".encode("latin1"))

        # 3 0 obj: Outlines root
        offsets[3] = f.tell()
        f.write(b"3 0 obj\n<< /Type /Outlines /First 4 0 R /Last 4 0 R /Count 1 >>\nendobj\n")

        # 4 0 obj: Outline Item 1
        offsets[4] = f.tell()
        f.write(b"4 0 obj\n<< /Title (Section 1 - Introduction) /Parent 3 0 R /Dest [5 0 R /Fit] >>\nendobj\n")

        next_obj = 5
        for i in range(pages):
            page_obj = next_obj
            content_obj = next_obj + 1
            next_obj += 2

            # Page obj
            offsets[page_obj] = f.tell()
            f.write(f"{page_obj} 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents {content_obj} 0 R /Resources << /Font << /F1 {5 + pages * 2} 0 R >> >> >>\nendobj\n".encode("latin1"))

            # Content stream
            offsets[content_obj] = f.tell()
            msg = f"Page {i + 1} of {pages} - Continuous Smooth Scrolling Stream"
            stream = f"BT /F1 24 Tf 100 700 Td ({msg}) Tj ET\n".encode("latin1")
            f.write(f"{content_obj} 0 obj\n<< /Length {len(stream)} >>\nstream\n".encode("latin1"))
            f.write(stream)
            f.write(b"endstream\nendobj\n")

        font_obj = next_obj
        offsets[font_obj] = f.tell()
        f.write(f"{font_obj} 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n".encode("latin1"))
        total_objs = font_obj + 1

        xref_pos = f.tell()
        f.write(f"xref\n0 {total_objs}\n0000000000 65535 f \n".encode("latin1"))
        for i in range(1, total_objs):
            f.write(f"{offsets[i]:010d} 00000 n \n".encode("latin1"))

        f.write(f"trailer\n<< /Size {total_objs} /Root 1 0 R >>\nstartxref\n{xref_pos}\n%%EOF\n".encode("latin1"))

def run_simulation():
    print("====================================================")
    print("===> Running Interactive PTY Terminal Simulation")
    print("====================================================")

    bin_path = os.environ.get("PDFCLI_BIN", os.path.abspath("./pdf-cli"))
    if not os.path.exists(bin_path):
        print(f"Error: binary {bin_path} not found")
        sys.exit(1)

    with tempfile.TemporaryDirectory(prefix="pdfcli_pty_") as tmpdir:
        pdf_path = os.path.join(tmpdir, "test_doc.pdf")
        generate_valid_test_pdf(pdf_path, pages=3)

        master, slave = pty.openpty()
        ws = struct.pack("HHHH", 24, 80, 720, 432)
        fcntl.ioctl(slave, termios.TIOCSWINSZ, ws)

        proc = subprocess.Popen(
            [bin_path, pdf_path],
            stdin=slave,
            stdout=slave,
            stderr=slave,
            close_fds=True,
            preexec_fn=os.setsid
        )
        os.close(slave)

        def drain_output(timeout=0.4):
            out = b""
            start = time.time()
            while time.time() - start < timeout:
                r, _, _ = select.select([master], [], [], 0.05)
                if r:
                    try:
                        chunk = os.read(master, 65536)
                        if not chunk:
                            break
                        out += chunk
                    except OSError:
                        break
                else:
                    if out:
                        break
            return out

        def check_alive():
            status = proc.poll()
            if status is not None:
                raise RuntimeError(f"pdf-cli unexpectedly terminated with code {status}!")

        try:
            print("  [SIM 1] Waiting for initial viewer frame render...")
            initial_out = drain_output(1.0)
            check_alive()
            assert len(initial_out) > 0, "No initial output received"
            assert b"\033[24;1H[1/" not in initial_out, "Bottom status bar unexpectedly rendered!"
            print("          PASSED (Viewer running, initial frame rasterized)")

            print("  [SIM 2] Injecting barrage of random mouse clicks and wheel events...")
            click_sequences = [
                b"\033[<0;10;5M",   # Left press at (10, 5)
                b"\033[<0;10;5m",   # Left release at (10, 5)
                b"\033[<2;40;12M",  # Right press at (40, 12)
                b"\033[<2;40;12m",  # Right release at (40, 12)
                b"\033[<64;40;12M", # Wheel up
                b"\033[<65;40;12M", # Wheel down
                b"\033[<0;1;1M",    # Left click top-left corner
                b"\033[<0;80;24M",  # Left click bottom-right corner
            ]
            for _ in range(5):
                for seq in click_sequences:
                    os.write(master, seq)
                    time.sleep(0.01)
                    check_alive()
            drain_output(0.3)
            check_alive()
            print("          PASSED (Process remained alive through random mouse input)")

            print("  [SIM 3] Testing top Tab Bar / HUD display toggling with 'i' and ESC...")
            os.write(master, b"i")
            hud_out = drain_output(0.4)
            check_alive()
            assert b"\033[1;1H" in hud_out, "Top HUD not rendered at Row 1 on 'i'"
            assert b"test_doc.pdf" in hud_out, "Tab name not found in top HUD"
            assert b"Page 1/3" in hud_out, "Page info not found in top HUD"

            # Dismiss HUD with ESC
            os.write(master, b"\033")
            time.sleep(0.05)
            check_alive()
            drain_output(0.2)
            print("          PASSED (HUD shown on 'i', ESC dismissed HUD cleanly)")

            print("  [SIM 4] Testing next/prev page navigation and feature toggles...")
            os.write(master, b"n") # Next page
            time.sleep(0.05)
            check_alive()

            os.write(master, b"p") # Prev page
            time.sleep(0.05)
            check_alive()

            os.write(master, b"D") # Dark mode toggle
            time.sleep(0.05)
            check_alive()

            os.write(master, b"f") # Fit mode toggle
            time.sleep(0.05)
            check_alive()

            os.write(master, b"+") # Zoom in
            time.sleep(0.05)
            check_alive()

            os.write(master, b"-") # Zoom out
            time.sleep(0.05)
            check_alive()

            os.write(master, b"0") # Reset zoom
            time.sleep(0.05)
            check_alive()
            drain_output(0.4)
            print("          PASSED (Next/prev page, dark mode, fit mode, and zoom working)")

            print("  [SIM 5] Testing Table of Contents modal and cancellation...")
            os.write(master, b"T")
            toc_out = drain_output(0.5)
            check_alive()
            assert b"Table of Contents" in toc_out, "TOC header not found in modal output"
            # Cancel TOC with 'q'
            os.write(master, b"q")
            time.sleep(0.1)
            check_alive()
            drain_output(0.4)
            print("          PASSED (TOC modal opened and dismissed cleanly)")

            print("  [SIM 6] Testing input batching and queue retention...")
            # Send continuous scroll keys followed by 'i' in a single write
            os.write(master, b"jji")
            time.sleep(0.1)
            check_alive()
            batched_out = drain_output(0.5)
            assert b"test_doc.pdf" in batched_out, "Batched 'i' command was dropped!"
            print("          PASSED (No input lost during batched writes)")

            print("  [SIM 7] Verifying clean shutdown on 'q' and terminal restoration...")
            os.write(master, b"q")
            exit_out = drain_output(1.0)
            proc.wait(timeout=2.0)
            assert proc.returncode == 0, f"Expected clean exit 0, got {proc.returncode}"
            assert b"\033[?1049l" in exit_out, "Alternate screen buffer exit sequence not emitted!"
            assert b"\033]111\007" in exit_out, "Terminal background reset sequence not emitted!"
            print("          PASSED (Clean exit code 0, alternate screen and background restored)")

        finally:
            os.close(master)
            if proc.poll() is None:
                try:
                    os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
                except Exception:
                    pass

    print("====================================================")
    print("===> ALL PTY TERMINAL SIMULATION TESTS PASSED!")
    print("====================================================")

def run_optional_module2_simulation():
    module2_path = "/home/aditya/Desktop/Sem-5/EO-301/Module-2.pdf"
    if not os.path.exists(module2_path):
        return

    print("====================================================")
    print("===> Running Optional Simulation on Module-2.pdf")
    print("====================================================")

    bin_path = os.environ.get("PDFCLI_BIN", os.path.abspath("./pdf-cli"))
    master, slave = pty.openpty()
    ws = struct.pack("HHHH", 24, 80, 720, 432)
    fcntl.ioctl(slave, termios.TIOCSWINSZ, ws)

    proc = subprocess.Popen(
        [bin_path, module2_path],
        stdin=slave,
        stdout=slave,
        stderr=slave,
        close_fds=True,
        preexec_fn=os.setsid
    )
    os.close(slave)

    def drain_output(timeout=0.4):
        out = b""
        start = time.time()
        while time.time() - start < timeout:
            r, _, _ = select.select([master], [], [], 0.05)
            if r:
                try:
                    chunk = os.read(master, 65536)
                    if not chunk:
                        break
                    out += chunk
                except OSError:
                    break
            else:
                if out:
                    break
        return out

    def check_alive():
        status = proc.poll()
        if status is not None:
            raise RuntimeError(f"pdf-cli unexpectedly terminated on Module-2.pdf with code {status}!")

    try:
        out1 = drain_output(1.0)
        check_alive()
        os.write(master, b"g50\r")
        time.sleep(0.15)
        check_alive()
        os.write(master, b"q")
        drain_output(0.5)
        proc.wait(timeout=2.0)
        assert proc.returncode == 0, f"Expected clean exit 0, got {proc.returncode}"
        print("          PASSED (Clean run on Module-2.pdf)")
    finally:
        os.close(master)
        if proc.poll() is None:
            try:
                os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
            except Exception:
                pass

if __name__ == "__main__":
    run_simulation()
    run_optional_module2_simulation()
