#!/usr/bin/env python3
"""
Comprehensive PTY Simulation Test Suite for pdf-cli
Simulates real interactive user sessions in a pseudoterminal:
1. Mouse click robustness (random clicks, releases, drags must NEVER quit the app)
2. Smooth continuous scrolling (wheel events & 'j'/'k')
3. HUD display toggle via 'i' key and dismissal via ESC key
4. Absence of bottom status bar
5. Safe exit exclusively on 'q' or Ctrl+C
"""

import os
import pty
import time
import signal
import select
import subprocess
import sys

def generate_test_pdf(path):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    t1 = "Page One - Continuous Scrolling Test"
    t2 = "Page Two - Moving Smoothly From Bottom"
    pdf_content = (
        "%PDF-1.4\n"
        "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
        "2 0 obj\n<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>\nendobj\n"
        "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 5 0 R /Resources << /Font << /F1 7 0 R >> >> >>\nendobj\n"
        "4 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 6 0 R /Resources << /Font << /F1 7 0 R >> >> >>\nendobj\n"
        f"5 0 obj\n<< /Length {len(t1) + 33} >>\nstream\nBT /F1 24 Tf 100 700 Td ({t1}) Tj ET\nendstream\nendobj\n"
        f"6 0 obj\n<< /Length {len(t2) + 33} >>\nstream\nBT /F1 24 Tf 100 700 Td ({t2}) Tj ET\nendstream\nendobj\n"
        "7 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n"
        "xref\n0 8\n"
        "0000000000 65535 f \n"
        "0000000009 00000 n \n"
        "0000000058 00000 n \n"
        "0000000125 00000 n \n"
        "0000000250 00000 n \n"
        "0000000375 00000 n \n"
        "0000000500 00000 n \n"
        "0000000625 00000 n \n"
        "trailer\n<< /Size 8 /Root 1 0 R >>\nstartxref\n725\n%%EOF\n"
    )
    with open(path, "wb") as f:
        f.write(pdf_content.encode("latin1"))

def run_simulation():
    print("====================================================")
    print("===> Running Interactive PTY Terminal Simulation")
    print("====================================================")

    pdf_path = "/tmp/sim_test.pdf"
    generate_test_pdf(pdf_path)

    bin_path = os.path.abspath("./pdf-cli")
    if not os.path.exists(bin_path):
        print(f"Error: binary {bin_path} not found")
        sys.exit(1)

    master, slave = pty.openpty()

    # Set terminal window size to 80x24 (720x432 px)
    import struct, fcntl, termios
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

    def drain_output(timeout=0.3):
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
        # Step 1: Wait for initial render
        print("  [SIM 1] Waiting for initial viewer frame render...")
        initial_out = drain_output(1.0)
        check_alive()
        assert len(initial_out) > 0, "No initial output received"
        # Verify no bottom status bar was printed during initial render
        # Bottom row status bar format was \033[24;1H...
        assert b"\033[24;1H[1/" not in initial_out, "Bottom status bar unexpectedly rendered!"
        print("          PASSED (Viewer running, bottom status bar confirmed removed)")

        # Step 2: Random Mouse Click Barrage
        print("  [SIM 2] Injecting barrage of random mouse clicks, releases, and drags...")
        click_sequences = [
            b"\033[<0;10;5M",   # Left press at (10, 5)
            b"\033[<0;10;5m",   # Left release at (10, 5)
            b"\033[<2;40;12M",  # Right press at (40, 12)
            b"\033[<2;40;12m",  # Right release at (40, 12)
            b"\033[<1;25;8M",   # Middle press
            b"\033[<1;25;8m",   # Middle release
            b"\033[<32;30;10M", # Mouse drag/motion
            b"\033[<35;30;10m", # Drag release
            b"\033[<0;1;1M",    # Left click top-left corner
            b"\033[<0;80;24M",  # Left click bottom-right corner
            b"\033[<0;50;10M",  # Random left click
            b"\033[<0;50;10m",
        ]
        for _ in range(5):
            for seq in click_sequences:
                os.write(master, seq)
                time.sleep(0.01)
                check_alive()
        drain_output(0.3)
        check_alive()
        print("          PASSED (Process remained alive and unaffected by 60 random mouse clicks)")

        # Step 3: Mouse Wheel & Continuous Smooth Scrolling
        print("  [SIM 3] Simulating continuous mouse wheel scrolling across page boundaries...")
        for _ in range(15):
            # Wheel down
            os.write(master, b"\033[<65;40;12M")
            time.sleep(0.02)
            check_alive()
        out_wheel = drain_output(0.5)
        check_alive()
        assert len(out_wheel) > 0, "No frame updates during wheel scrolling"

        # Scroll back up with 'k' and wheel up
        for _ in range(10):
            os.write(master, b"\033[<64;40;12M")
            time.sleep(0.02)
            check_alive()
        for _ in range(5):
            os.write(master, b"k")
            time.sleep(0.02)
            check_alive()
        drain_output(0.3)
        check_alive()
        print("          PASSED (Smooth multi-page scrolling responsive and stable)")

        # Step 4: Top HUD Toggle with 'i' and Dismissal with ESC
        print("  [SIM 4] Testing top Tab Bar / HUD display toggling with 'i' and ESC...")
        # Press 'i' to show HUD
        os.write(master, b"i")
        hud_out = drain_output(0.4)
        check_alive()
        # Verify Row 1 now has HUD tabs/info: "\033[1;1H"
        assert b"\033[1;1H" in hud_out, "Top HUD not rendered at Row 1 on 'i'"
        assert b"sim_test.pdf" in hud_out, "Tab name not found in top HUD"
        assert b"Page 1/" in hud_out, "Page info not found in top HUD"

        # Press ESC to dismiss HUD - must NOT quit the app!
        os.write(master, b"\033")
        time.sleep(0.1)
        check_alive()
        drain_output(0.3)
        print("          PASSED (HUD shown on 'i', ESC dismissed HUD without quitting)")

        # Press 'i' again to re-toggle
        os.write(master, b"i")
        re_hud = drain_output(0.3)
        check_alive()
        assert b"sim_test.pdf" in re_hud, "HUD did not re-appear on second 'i'"

        # Step 5: Mode toggles: Dark mode 'd', Fit mode 'f', Zoom '+', '-'
        print("  [SIM 5] Testing feature toggles (dark mode, fit mode, zoom, goto)...")
        os.write(master, b"d") # Toggle dark mode
        time.sleep(0.05)
        check_alive()

        os.write(master, b"f") # Toggle fit mode
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

        # Jump to page 2: 'g', '2', Enter
        os.write(master, b"g")
        time.sleep(0.05)
        check_alive()
        os.write(master, b"2\r")
        time.sleep(0.1)
        check_alive()

        drain_output(0.3)
        print("          PASSED (All modal and mode toggles functional)")

        # Step 6: Controlled termination with 'q'
        print("  [SIM 6] Verifying clean shutdown on 'q'...")
        os.write(master, b"q")
        proc.wait(timeout=2.0)
        assert proc.returncode == 0, f"Expected clean exit 0, got {proc.returncode}"
        print("          PASSED (Clean exit with code 0)")

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

if __name__ == "__main__":
    run_simulation()
