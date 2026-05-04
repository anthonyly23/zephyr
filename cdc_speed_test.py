#!/usr/bin/env python3
import argparse
import time
import serial
from serial.serialutil import SerialTimeoutException

def run_echo_benchmark(port, baud, duration_s, chunk_size, window_size, settle_s,
                       drain_timeout_s, write_timeout_s, min_chunk_size):
    ser = serial.Serial(
        port=port,
        baudrate=baud,
        timeout=0,          # non-blocking reads
        write_timeout=write_timeout_s
    )

    try:
        # Give device/host stack a short stabilization period.
        time.sleep(settle_s)
        ser.dtr = True
        ser.rts = True
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        tx_bytes = 0
        rx_bytes = 0
        outstanding = 0
        dynamic_chunk = max(min_chunk_size, chunk_size)
        timeout_events = 0

        start = time.perf_counter()
        deadline = start + duration_s

        while time.perf_counter() < deadline:
            # Drain first so echo backpressure cannot starve TX progress.
            avail = ser.in_waiting
            if avail > 0:
                data = ser.read(min(avail, 32768))
                got = len(data)
                rx_bytes += got
                outstanding = max(0, outstanding - got)

            # Then submit a single chunk if window allows.
            if outstanding + dynamic_chunk <= window_size:
                payload = b"A" * dynamic_chunk
                try:
                    n = ser.write(payload)
                except SerialTimeoutException:
                    timeout_events += 1
                    # Back off aggressively on timeouts, then retry next loop.
                    if dynamic_chunk > min_chunk_size:
                        dynamic_chunk = max(min_chunk_size, dynamic_chunk // 2)
                    time.sleep(0.001)
                    continue

                if n > 0:
                    tx_bytes += n
                    outstanding += n

                    # Grow cautiously when stable.
                    if dynamic_chunk < chunk_size:
                        dynamic_chunk = min(chunk_size, dynamic_chunk * 2)
                else:
                    time.sleep(0.0005)
            else:
                # Window full: wait for more echoed data.
                time.sleep(0.0005)

        # Drain remaining echoed data for a short tail window
        drain_deadline = time.perf_counter() + drain_timeout_s
        while outstanding > 0 and time.perf_counter() < drain_deadline:
            avail = ser.in_waiting
            if avail > 0:
                data = ser.read(min(avail, outstanding))
                got = len(data)
                rx_bytes += got
                outstanding = max(0, outstanding - got)
            else:
                time.sleep(0.0005)

        end = time.perf_counter()
        elapsed = max(end - start, 1e-9)

        tx_mbps = (tx_bytes / elapsed) / (1024 * 1024)
        rx_mbps = (rx_bytes / elapsed) / (1024 * 1024)

        return {
            "baud": baud,
            "elapsed_s": elapsed,
            "tx_bytes": tx_bytes,
            "rx_bytes": rx_bytes,
            "tx_mbps": tx_mbps,
            "rx_mbps": rx_mbps,
            "loss_bytes": max(0, tx_bytes - rx_bytes),
            "timeout_events": timeout_events,
            "final_chunk": dynamic_chunk,
        }

    finally:
        ser.close()

def parse_baud_list(text):
    vals = []
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        vals.append(int(part))
    if not vals:
        raise ValueError("Empty baud list")
    return vals

def print_result(r):
    print(
        f"baud={r['baud']:>10} | "
        f"time={r['elapsed_s']:.2f}s | "
        f"tx={r['tx_bytes']:,} B ({r['tx_mbps']:.3f} MiB/s) | "
        f"rx={r['rx_bytes']:,} B ({r['rx_mbps']:.3f} MiB/s) | "
        f"loss={r['loss_bytes']:,} B | "
        f"timeouts={r['timeout_events']} | "
        f"chunk_end={r['final_chunk']}"
    )

def main():
    ap = argparse.ArgumentParser(description="USB CDC ACM echo throughput benchmark (pyserial)")
    ap.add_argument("--port", required=True, help="COM port, example: COM8")
    ap.add_argument("--duration", type=float, default=10.0, help="Benchmark duration in seconds")
    ap.add_argument("--chunk", type=int, default=2048, help="Write chunk size in bytes")
    ap.add_argument("--min-chunk", type=int, default=256, help="Minimum adaptive chunk size in bytes")
    ap.add_argument("--window", type=int, default=64 * 1024, help="TX pipeline window in bytes")
    ap.add_argument("--write-timeout", type=float, default=5.0, help="Serial write timeout in seconds")
    ap.add_argument("--settle", type=float, default=0.5, help="Settle time before start (s)")
    ap.add_argument("--drain-timeout", type=float, default=2.0, help="Drain timeout after test (s)")
    ap.add_argument("--baud", type=int, default=115200, help="Single baud to test")
    ap.add_argument(
        "--sweep",
        default="",
        help="Comma-separated baud sweep, example: 115200,921600,2000000,3000000"
    )
    args = ap.parse_args()

    if args.sweep:
        bauds = parse_baud_list(args.sweep)
        print(f"Running sweep on {args.port}: {bauds}")
    else:
        bauds = [args.baud]
        print(f"Running single test on {args.port} at baud={args.baud}")

    results = []
    for b in bauds:
        r = run_echo_benchmark(
            port=args.port,
            baud=b,
            duration_s=args.duration,
            chunk_size=args.chunk,
            window_size=args.window,
            settle_s=args.settle,
            drain_timeout_s=args.drain_timeout,
            write_timeout_s=args.write_timeout,
            min_chunk_size=args.min_chunk,
        )
        results.append(r)
        print_result(r)

    best = max(results, key=lambda x: x["rx_mbps"])
    print("\nBest observed RX throughput:")
    print_result(best)

    print("\nNote:")
    print("For USB CDC ACM, baud is often virtual and may not change true USB throughput.")
    print("Use the best measured RX MiB/s as your practical speed result.")

if __name__ == "__main__":
    main()