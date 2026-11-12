#!/usr/bin/env python3
"""Configure and inspect the RFSoC4x2 QSFP/UDP transport from PYNQ."""

import argparse
import ipaddress
import json
import time
from pathlib import Path

from network_layer import CMAC, NetworkLayer  # registers PYNQ drivers before Overlay()
from pynq import Overlay


CMAC_CONFIGURATION_TX_REG1 = 0x000C
CMAC_CONFIGURATION_RX_REG1 = 0x0014
CMAC_RSFEC_CONFIG = 0x1000
CMAC_RSFEC_ENABLE = 0x107C
CMAC_ENABLE = 0x1
CMAC_RSFEC_CORRECT_INDICATE = 0x3
CMAC_RSFEC_TX_RX_ENABLE = 0x3


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("bitfile", type=Path)
    p.add_argument("--fpga-ip", default="192.168.10.2")
    p.add_argument("--host-ip", default="192.168.10.1")
    p.add_argument("--data-port", type=int, default=10001,
                   help="HELIX data UDP port on both FPGA and host")
    p.add_argument("--control-port", type=int, default=10000,
                   help="HELIX control UDP port on both FPGA and host")
    p.add_argument("--metadata-port", type=int, default=10002,
                   help="HELIX metadata UDP port on both FPGA and host")
    p.add_argument("--mac", default="02:00:00:00:10:02")
    p.add_argument("--gateway", default=None)
    p.add_argument("--mask", default="255.255.255.0")
    p.add_argument("--host-mac", default=None,
                   help="optional static ARP entry for the host")
    p.add_argument("--no-download", action="store_true")
    p.add_argument("--status-only", action="store_true")
    p.add_argument("--wait-link", type=float, default=10.0)
    return p.parse_args()


def get_blocks(overlay):
    cmac = getattr(overlay, "qsfp_cmac")
    network = getattr(overlay, "qsfp_networklayer")
    if not isinstance(cmac, CMAC):
        raise RuntimeError(f"qsfp_cmac driver did not bind: {type(cmac)}")
    if not isinstance(network, NetworkLayer):
        raise RuntimeError(f"qsfp_networklayer driver did not bind: {type(network)}")
    return cmac, network


def enable_cmac(cmac):
    """Enable the CMAC datapaths; the generated core resets them disabled."""
    cmac.write(CMAC_RSFEC_CONFIG, CMAC_RSFEC_CORRECT_INDICATE)
    cmac.write(CMAC_RSFEC_ENABLE, CMAC_RSFEC_TX_RX_ENABLE)
    cmac.write(CMAC_CONFIGURATION_TX_REG1,
               cmac.read(CMAC_CONFIGURATION_TX_REG1) | CMAC_ENABLE)
    cmac.write(CMAC_CONFIGURATION_RX_REG1,
               cmac.read(CMAC_CONFIGURATION_RX_REG1) | CMAC_ENABLE)
    tx_config = cmac.read(CMAC_CONFIGURATION_TX_REG1)
    rx_config = cmac.read(CMAC_CONFIGURATION_RX_REG1)
    fec_config = cmac.read(CMAC_RSFEC_ENABLE)
    if not (tx_config & CMAC_ENABLE and rx_config & CMAC_ENABLE and
            fec_config & CMAC_RSFEC_TX_RX_ENABLE == CMAC_RSFEC_TX_RX_ENABLE):
        raise RuntimeError(
            f"CMAC TX/RX enable failed: tx=0x{tx_config:08x} "
            f"rx=0x{rx_config:08x} fec=0x{fec_config:08x}"
        )


def status(cmac, network):
    return {
        "cmac": cmac.link_status(debug=True),
        "network": dict(network.get_network_info()),
        "sockets": dict(network.read_socket_table()),
        "probes": dict(network.get_debug_stats),
    }


def main():
    args = parse_args()
    overlay = Overlay(str(args.bitfile), download=not args.no_download)
    cmac, network = get_blocks(overlay)
    enable_cmac(cmac)

    if not args.status_only:
        gateway = args.gateway or str(ipaddress.IPv4Network(
            f"{args.fpga_ip}/{args.mask}", strict=False).network_address + 1)
        network.set_mac_address(args.mac)
        network.set_ip_address(args.fpga_ip, gateway)
        network.register_map.ip_mask = int(ipaddress.IPv4Address(args.mask))
        network.sockets[:] = ("", 0, 0, False)
        # Preserve HELIX's original three-socket contract. NetworkLayer TDEST
        # is the socket-table index used by the FPGA data/control/metadata
        # router; HELIX's host library binds these same three UDP ports.
        network.sockets[0] = (args.host_ip, args.data_port,
                              args.data_port, True)
        network.sockets[1] = (args.host_ip, args.control_port,
                              args.control_port, True)
        network.sockets[2] = (args.host_ip, args.metadata_port,
                              args.metadata_port, True)
        network.populate_socket_table()
        if args.host_mac:
            network.write_arp_entry(args.host_mac, args.host_ip)
        else:
            network.arp_discovery()

    deadline = time.monotonic() + args.wait_link
    while time.monotonic() < deadline:
        if cmac.link_status().get("cmac_link"):
            break
        time.sleep(0.25)

    result = status(cmac, network)
    result["transport_contract"] = {
        "data": {
            "socket_id": 0,
            "fpga": f"{args.fpga_ip}:{args.data_port}",
            "host": f"{args.host_ip}:{args.data_port}",
        },
        "control": {
            "socket_id": 1,
            "fpga": f"{args.fpga_ip}:{args.control_port}",
            "host": f"{args.host_ip}:{args.control_port}",
        },
        "metadata": {
            "socket_id": 2,
            "fpga": f"{args.fpga_ip}:{args.metadata_port}",
            "host": f"{args.host_ip}:{args.metadata_port}",
        },
    }
    print(json.dumps(result, indent=2, default=str))
    if not result["cmac"]["cmac_link"]:
        raise SystemExit("CMAC link is down; check QSFP module/cable/NIC FEC mode")


if __name__ == "__main__":
    main()
