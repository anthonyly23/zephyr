#!/usr/bin/env python3
import argparse
import time

import serial


def run_rx_benchmark(port, baud, duration_s, read_size, settle_s):
    ser = serial.Serial(
        port=port,
        baudrate=baud,
        timeout=0,
        write_timeout=1.0,
    )

    try:
        time.sleep(settle_s)
        ser.dtr = True
        ser.rts = True
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        start = time.perf_counter()
        deadline = start + duration_s
        rx_bytes = 0

        while time.perf_counter() < deadline:
            avail = ser.in_waiting
            if avail > 0:
                data = ser.read(min(avail, read_size))
                rx_bytes += len(data)
            else:
                time.sleep(0.0005)

        end = time.perf_counter()
        elapsed = max(end - start, 1e-9)
        rx_mibs = (rx_bytes / elapsed) / (1024 * 1024)

        return {
		    "baud": baud,
		    "elapsed_s": elapsed,
		    "rx_bytes": rx_bytes,
		    "rx_mibs": rx_mibs,
		}
    finally:
        ser.close()


def print_result(result):
    print(
        f"baud={result['baud']:>10} | "
        f"time={result['elapsed_s']:.2f}s | "
        f"rx={result['rx_bytes']:,} B ({result['rx_mibs']:.3f} MiB/s)"
    )


def main():
    parser = argparse.ArgumentParser(description="CDC ACM TX-only throughput benchmark")
    parser.add_argument("--port", required=True, help="COM port, example: COM20")
    parser.add_argument("--duration", type=float, default=20.0, help="Benchmark duration in seconds")
    parser.add_argument("--read-size", type=int, default=16384, help="Maximum bytes to read per iteration")
    parser.add_argument("--settle", type=float, default=0.5, help="Settle time before start (s)")
    parser.add_argument("--baud", type=int, default=115200, help="CDC ACM line coding to request")
    args = parser.parse_args()

    print(f"Running RX benchmark on {args.port} at baud={args.baud}")
    result = run_rx_benchmark(
        port=args.port,
        baud=args.baud,
        duration_s=args.duration,
        read_size=args.read_size,
        settle_s=args.settle,
    )
    print_result(result)


if __name__ == "__main__":
    main()