#!/usr/bin/env python3
"""Control the HELIX/QSFP/PS-DMA packet router from the RFSoC PS."""

from __future__ import annotations

import argparse
import time

from pynq import MMIO


MODE_GPIO_BASE = 0xB0120000
MODE_GPIO_RANGE = 0x10000
GPIO_DATA = 0x00
GPIO2_DATA = 0x08
CLEAR_STATUS = 1 << 2

MODES = {
    "qsfp-helix": 0,
    "ps-helix": 1,
    "ps-qsfp": 2,
    "stop": 3,
}


def decode_status(value: int) -> dict[str, int | bool]:
    return {
        "active_mode": value & 0x3,
        "switch_pending": bool(value & (1 << 2)),
        "rx_packet_active": bool(value & (1 << 3)),
        "tx_packet_active": bool(value & (1 << 4)),
        "ps_to_helix_keep_error": bool(value & (1 << 5)),
        "clear_request_seen": bool(value & (1 << 6)),
        "ps_axis_valid": bool(value & (1 << 7)),
    }


def wait_for_mode(mmio: MMIO, requested: int, timeout: float) -> dict[str, int | bool]:
    deadline = time.monotonic() + timeout
    while True:
        status = decode_status(mmio.read(GPIO2_DATA))
        if status["active_mode"] == requested and not status["switch_pending"]:
            return status
        if time.monotonic() >= deadline:
            raise TimeoutError(
                "mode switch did not reach a packet boundary; "
                f"requested={requested} status={status}"
            )
        time.sleep(0.001)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=[*MODES, "status", "clear"])
    parser.add_argument("--timeout", type=float, default=1.0)
    args = parser.parse_args()

    mmio = MMIO(MODE_GPIO_BASE, MODE_GPIO_RANGE)

    if args.action == "status":
        print(decode_status(mmio.read(GPIO2_DATA)))
        return 0

    current_mode = mmio.read(GPIO_DATA) & 0x3
    if args.action == "clear":
        mmio.write(GPIO_DATA, current_mode | CLEAR_STATUS)
        time.sleep(0.001)
        mmio.write(GPIO_DATA, current_mode)
        print(decode_status(mmio.read(GPIO2_DATA)))
        return 0

    requested = MODES[args.action]
    mmio.write(GPIO_DATA, requested)
    print(wait_for_mode(mmio, requested, args.timeout))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
