import os
import pty
import time
import subprocess
import select
import struct
import fcntl
import termios

def run_load_benchmark(bin_path, pdf_path):
    master, slave = pty.openpty()
    ws = struct.pack("HHHH", 24, 80, 720, 432)
    fcntl.ioctl(slave, termios.TIOCSWINSZ, ws)

    proc = subprocess.Popen(
        [bin_path, pdf_path],
        stdin=slave,
        stdout=slave,
        stderr=slave,
        close_fds=True
    )
    os.close(slave)

    def drain():
        while True:
            r, _, _ = select.select([master], [], [], 0.02)
            if r:
                try:
                    if not os.read(master, 65536):
                        break
                except OSError:
                    break
            else:
                break

    # Simulate heavy load
    time.sleep(0.5)
    drain()

    # Heavy scrolling through pages
    for _ in range(50):
        os.write(master, b"j")
        time.sleep(0.01)
    drain()

    for _ in range(30):
        os.write(master, b"\033[<65;40;12M")
        time.sleep(0.01)
    drain()

    # Page jumping and zooming
    for p in [10, 50, 100, 200, 500]:
        os.write(master, f"g{p}\r".encode())
        time.sleep(0.05)
        drain()
        os.write(master, b"+")
        time.sleep(0.02)
        drain()
        os.write(master, b"f")
        time.sleep(0.02)
        drain()
        os.write(master, b"D")
        time.sleep(0.02)
        drain()

    # Text search under load
    os.write(master, b"/the\r")
    time.sleep(0.2)
    drain()
    for _ in range(10):
        os.write(master, b"n")
        time.sleep(0.02)
    drain()

    # Read memory and I/O from /proc
    vm_hwm = 0
    vm_rss = 0
    try:
        with open(f"/proc/{proc.pid}/status") as f:
            for line in f:
                if line.startswith("VmHWM:"):
                    vm_hwm = int(line.split()[1])
                elif line.startswith("VmRSS:"):
                    vm_rss = int(line.split()[1])
    except Exception as e:
        print("Status read err:", e)

    io_read = 0
    io_write = 0
    try:
        with open(f"/proc/{proc.pid}/io") as f:
            for line in f:
                if line.startswith("read_bytes:"):
                    io_read = int(line.split()[1])
                elif line.startswith("write_bytes:"):
                    io_write = int(line.split()[1])
    except Exception as e:
        print("IO read err:", e)

    # Quit
    os.write(master, b"q")
    drain()
    proc.wait(timeout=2)
    os.close(master)

    bin_size = os.path.getsize(bin_path)

    return {
        "bin_size_kb": bin_size / 1024,
        "vm_hwm_kb": vm_hwm,
        "vm_rss_kb": vm_rss,
        "io_read_kb": io_read / 1024,
        "io_write_kb": io_write / 1024
    }

if __name__ == "__main__":
    pdf = "/home/aditya/Desktop/Sem-5/EO-301/Module-2.pdf"
    res = run_load_benchmark("./pdf-cli", pdf)
    print(f"Binary Size on SSD: {res['bin_size_kb']:.1f} KB ({res['bin_size_kb']/1024:.2f} MB)")
    print(f"Peak RAM (VmHWM):   {res['vm_hwm_kb']:.1f} KB ({res['vm_hwm_kb']/1024:.2f} MB)")
    print(f"Current RSS:        {res['vm_rss_kb']:.1f} KB ({res['vm_rss_kb']/1024:.2f} MB)")
    print(f"SSD Read IO:        {res['io_read_kb']:.1f} KB")
    print(f"SSD Write IO:       {res['io_write_kb']:.1f} KB")
