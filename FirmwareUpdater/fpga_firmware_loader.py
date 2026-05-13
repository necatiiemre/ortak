#!/usr/bin/env python3
"""
DCU FPGA Firmware Loader (AFDX)

Sends firmware update packets to FPGA via Cumulus switch.
Packet format matches RemoteConfigSender: ETH + VLAN(97) + IP + UDP + Payload

Prerequisites:
    1. Run cumulus_setup.py first to bring up the Cumulus switch
    2. Root privileges required for raw packet injection

Usage:
    sudo python3 fpga_firmware_loader.py
"""

import argparse
import os
import sys
import math
import time
import struct
from tqdm import tqdm
from scapy.all import sendp, sniff, Raw, Ether, IP, UDP, get_if_list, conf

# ==============================================================================
# DEFAULT CONFIGURATION (matching RemoteConfigSender / DTN system)
# ------------------------------------------------------------------------------
# Edit these to tune the firmware update behaviour. Each value is also
# overridable via the matching --<name> CLI flag for one-off changes.
# ==============================================================================

DEFAULT_INTERFACE = "ens1f0np0"
DEFAULT_VLAN_ID = 97
DEFAULT_VL_ID = 0
DEFAULT_NETWORK = 'A'
DEFAULT_USER_IP = (1, 33, 1)
DEFAULT_INITIAL_DELAY_SEC = 80
DEFAULT_INTER_PACKET_DELAY_MS = 4
DEFAULT_LRU_ID = 0x2600  # standalone runs default to DTN; mainSoftware passes --lru-id explicitly

# FPGA readiness check: passive sniff on eno12399 for unsolicited HM packets.
# Once 28V is applied the FPGAs broadcast a 6-packet cycle without any query:
#   1187, 1083, 1187, 1083, 438, 94 -> Assistant 8+8 ports, Manager 8+8+3 ports, MCU
# Each is IPv4 multicast UDP src=100 dst=100, dst IP 224.224.0.0/16.
# Status_enable parsing matches dpdk/src/HealthMonitor/HealthMonitor.c:230-327:
#   only the 1187-byte packet carries the device header; udp_payload[6] is
#   status_enable, 0x03 = Assistant FPGA, 0x01 = Manager FPGA. The 1083/438
#   mini-header packets and the 94-byte MCU packet are ignored for readiness -
#   seeing one of each FPGA's 1187-byte packet is sufficient.
DEFAULT_READY_INTERFACE = "eno12399"
DEFAULT_READY_TIMEOUT_SEC = 60
DEFAULT_READY_POST_DELAY_SEC = 5
HEALTH_UDP_PORT = 100
HEALTH_DST_MULTICAST_PREFIX = "224.224."
HEALTH_PKT_SIZE_WITH_HEADER = 1187
HEALTH_STATUS_OFFSET = 6
HEALTH_STATUS_ASSISTANT = 0x03
HEALTH_STATUS_MANAGER = 0x01

# ==============================================================================
# 1. CORE NETWORKING FUNCTIONS
# ==============================================================================

def calculate_ipv4_checksum(header_bytes):
    if len(header_bytes) % 2 == 1:
        header_bytes += b'\x00'
    checksum = 0
    for i in range(0, len(header_bytes), 2):
        w = (header_bytes[i] << 8) + header_bytes[i + 1]
        checksum += w
    while (checksum >> 16) > 0:
        checksum = (checksum & 0xFFFF) + (checksum >> 16)
    return ~checksum & 0xFFFF


def setup_scapy_environment():
    conf.sniff_promisc = True
    conf.checkIPaddr = False
    conf.verb = 0


def send_firmware_update(spi_file, interface, lru_id, vlan_id=DEFAULT_VLAN_ID,
                         vl_id=DEFAULT_VL_ID, network=DEFAULT_NETWORK,
                         user_ip=DEFAULT_USER_IP,
                         initial_delay_sec=DEFAULT_INITIAL_DELAY_SEC,
                         inter_packet_delay_ms=DEFAULT_INTER_PACKET_DELAY_MS):
    """Generates and sends AFDX firmware update packets with VLAN tagging."""
    CHUNK_SIZE = 1280

    if not os.path.exists(spi_file):
        print(f"\n[ERROR] Could not find file: {spi_file}")
        return False

    file_size = os.path.getsize(spi_file)
    total_packets = math.ceil(file_size / CHUNK_SIZE)

    print("\n" + "=" * 50)
    print(f"Starting Firmware Update")
    print("=" * 50)
    print(f"File:           {spi_file} ({file_size} bytes)")
    print(f"Total Packets:  {total_packets}")
    print(f"Interface:      {interface}")
    print(f"LRU ID:         {hex(lru_id)}")
    print(f"VLAN ID:        {vlan_id}")
    print(f"VL ID:          {vl_id}")
    print(f"Network:        {network.upper()}")
    print(f"Timing:         {initial_delay_sec}s initial delay, {inter_packet_delay_ms}ms inter-packet")
    print("=" * 50 + "\n")

    # --- 1. ETHERNET HEADER ---
    # Dest MAC: 03:00:00:00 + [2 byte VL-ID]
    dest_mac = b'\x03\x00\x00\x00' + struct.pack('>H', vl_id)
    # Source MAC: 02:00:00:00:00 + [network byte]
    net_byte = 0x20 if network.upper() == 'A' else 0x40
    src_mac = b'\x02\x00\x00\x00\x00' + bytes([net_byte])

    # EtherType = 802.1Q VLAN tag
    eth_header = dest_mac + src_mac + b'\x81\x00'

    # --- 2. VLAN TAG (4 bytes: 2 TCI + 2 EtherType) ---
    # TCI: priority(3) + CFI(1) + VLAN ID(12)
    vlan_tci = (0 << 13) | (0 << 12) | (vlan_id & 0xFFF)
    vlan_tag = struct.pack('>H', vlan_tci) + b'\x08\x00'  # Next proto = IPv4

    try:
        with open(spi_file, 'rb') as f:
            afdx_seq = 0

            pbar = tqdm(total=total_packets, desc="Sending Packets", unit="pkt",
                        bar_format='{l_bar}{bar}| {n_fmt}/{total_fmt} [{elapsed}<{remaining}]')

            for i in range(total_packets):
                reverse_counter = total_packets - 1 - i
                data_chunk = f.read(CHUNK_SIZE)

                # --- 3. AFDX PAYLOAD ---
                afdx_payload = (struct.pack('>H', lru_id) + b'\x57\xbb' +
                                struct.pack('>H', reverse_counter) + data_chunk)

                # --- 4. UDP HEADER ---
                udp_len = 8 + len(afdx_payload)
                udp_header = b'\x00\x64\x00\x64' + struct.pack('>H', udp_len) + b'\x00\x00'

                # --- 5. IP HEADER ---
                ip_len = 20 + udp_len
                src_ip = bytes([0x0a, user_ip[0], user_ip[1], user_ip[2]])
                dst_ip = b'\xe0\xe0' + struct.pack('>H', vl_id)

                ip_header_no_checksum = (
                    b'\x45\x00' + struct.pack('>H', ip_len) + b'\xd4\x3b' +
                    b'\x00\x00' + b'\x01\x11' + b'\x00\x00' + src_ip + dst_ip
                )

                ip_chksum = calculate_ipv4_checksum(ip_header_no_checksum)
                ip_header = (ip_header_no_checksum[:10] +
                             struct.pack('>H', ip_chksum) +
                             ip_header_no_checksum[12:])

                # --- 6. ASSEMBLE FULL PACKET (with VLAN tag) ---
                full_raw_packet = (eth_header + vlan_tag + ip_header +
                                   udp_header + afdx_payload + bytes([afdx_seq]))

                # --- 7. SEND ---
                sendp(Raw(load=full_raw_packet), iface=interface, verbose=False)

                if afdx_seq == 0:
                    afdx_seq = 1
                elif afdx_seq == 255:
                    afdx_seq = 1
                else:
                    afdx_seq += 1

                pbar.update(1)

                # --- 8. TIMING CONTROL ---
                if i == 0:
                    pbar.write(f"\n[INFO] Init Packet Sent. Waiting {initial_delay_sec} seconds for LRU flash erase...")
                    time.sleep(initial_delay_sec)
                    pbar.write("[INFO] Resuming transmission...")
                elif i < total_packets - 1:
                    time.sleep(inter_packet_delay_ms / 1000.0)

            pbar.close()
            return True

    except Exception as e:
        if 'pbar' in locals():
            pbar.close()
        print(f"\n[ERROR] Transmission failed: {str(e)}")
        return False


# ==============================================================================
# 2. USER INTERFACE
# ==============================================================================

def select_interface():
    interfaces = get_if_list()
    if not interfaces:
        print("[ERROR] No network interfaces found. Are you running as Root?")
        sys.exit(1)

    # Show default first if available
    default_idx = None
    print("\n--- Available Network Interfaces ---")
    for idx, iface in enumerate(interfaces):
        marker = " <-- default" if iface == DEFAULT_INTERFACE else ""
        print(f"  [{idx + 1}] {iface}{marker}")
        if iface == DEFAULT_INTERFACE:
            default_idx = idx + 1

    while True:
        try:
            prompt = f"\nSelect Interface Number"
            if default_idx:
                prompt += f" [{default_idx}]"
            prompt += ": "
            choice_str = input(prompt).strip()

            if not choice_str and default_idx:
                return interfaces[default_idx - 1]

            choice = int(choice_str)
            if 1 <= choice <= len(interfaces):
                return interfaces[choice - 1]
            print("Invalid choice. Try again.")
        except ValueError:
            print("Please enter a valid number.")


def get_valid_input(prompt, cast_type=str, validator=None, error_msg="Invalid input.", default=None):
    while True:
        display_prompt = prompt
        if default is not None:
            display_prompt = prompt.rstrip(": ") + f" [{default}]: "

        val = input(display_prompt).strip().strip("'\"")

        if not val and default is not None:
            return default

        try:
            val = cast_type(val)
            if validator and not validator(val):
                print(error_msg)
                continue
            return val
        except ValueError:
            print(error_msg)


# ==============================================================================
# 3. MAIN
# ==============================================================================

def parse_lru_id(value):
    s = str(value).strip().lower()
    return int(s, 16) if s.startswith("0x") else int(s)


def wait_for_fpga_ready(interface, timeout_sec, post_delay_sec):
    """Passively sniff `interface` for unsolicited FPGA health packets.

    Once 28V is applied the FPGAs broadcast a 6-packet cycle to
    224.224.0.0/16:100 without needing any query. We only need to see one
    1187-byte packet from each FPGA (status_enable 0x03 = ASSISTANT,
    0x01 = MANAGER) to consider the device ready. After both are observed,
    sleeps `post_delay_sec` before returning True. Returns False on timeout.
    """
    state = {"assistant": False, "manager": False}

    def is_done(_pkt):
        return state["assistant"] and state["manager"]

    def cb(pkt):
        if not pkt.haslayer(IP) or not pkt.haslayer(UDP):
            return
        if not pkt[IP].dst.startswith(HEALTH_DST_MULTICAST_PREFIX):
            return
        if pkt[UDP].dport != HEALTH_UDP_PORT:
            return
        if pkt[UDP].sport != HEALTH_UDP_PORT:
            return
        if len(pkt) != HEALTH_PKT_SIZE_WITH_HEADER:
            return  # only the 1187-byte packet carries status_enable
        try:
            status_enable = bytes(pkt[UDP].payload)[HEALTH_STATUS_OFFSET]
        except Exception:
            return

        if status_enable == HEALTH_STATUS_ASSISTANT and not state["assistant"]:
            state["assistant"] = True
            print(f"[FW-READY] ASSISTANT FPGA detected "
                  f"(1187-byte from {pkt[IP].src} to {pkt[IP].dst}, "
                  f"status_enable=0x03)")
        elif status_enable == HEALTH_STATUS_MANAGER and not state["manager"]:
            state["manager"] = True
            print(f"[FW-READY] MANAGER FPGA detected "
                  f"(1187-byte from {pkt[IP].src} to {pkt[IP].dst}, "
                  f"status_enable=0x01)")

    print(f"[FW-READY] Listening on {interface} for ASSISTANT + MANAGER "
          f"health packets (UDP {HEALTH_UDP_PORT}, dst {HEALTH_DST_MULTICAST_PREFIX}*, "
          f"timeout {timeout_sec}s)...")
    try:
        sniff(iface=interface, prn=cb, filter=f"udp port {HEALTH_UDP_PORT}",
              stop_filter=is_done, timeout=timeout_sec, store=False)
    except Exception as e:
        print(f"[ERROR] sniff failed on {interface}: {e}")
        return False

    if not (state["assistant"] and state["manager"]):
        print(f"[ERROR] FPGA not ready within {timeout_sec}s. Seen: "
              f"ASSISTANT={state['assistant']}, MANAGER={state['manager']}")
        return False

    print(f"[FW-READY] Both FPGAs ready. Sleeping {post_delay_sec}s before "
          f"sending firmware...")
    time.sleep(post_delay_sec)
    return True


def main():
    parser = argparse.ArgumentParser(description="DCU FPGA Firmware Loader")
    parser.add_argument("--spi-path", default=None,
                        help="Absolute path to .spi file (skips interactive prompt)")
    parser.add_argument("--lru-id", default=hex(DEFAULT_LRU_ID),
                        help="LRU ID, hex (0x2600) or decimal. Default: %(default)s")
    parser.add_argument("--interface", default=DEFAULT_INTERFACE,
                        help="Network interface name. Default: %(default)s")
    parser.add_argument("--vlan-id", type=int, default=DEFAULT_VLAN_ID,
                        help="VLAN ID 1..4094. Default: %(default)s")
    parser.add_argument("--vl-id", type=int, default=DEFAULT_VL_ID,
                        help="AFDX VL ID. Default: %(default)s")
    parser.add_argument("--network", default=DEFAULT_NETWORK,
                        help="AFDX network 'A' or 'B'. Default: %(default)s")
    parser.add_argument("--initial-delay", type=int, default=DEFAULT_INITIAL_DELAY_SEC,
                        help="Wait (s) after first packet. Default: %(default)s")
    parser.add_argument("--inter-packet-delay", type=int, default=DEFAULT_INTER_PACKET_DELAY_MS,
                        help="BAG / inter-packet delay (ms). Default: %(default)s")
    parser.add_argument("--non-interactive", action="store_true",
                        help="Skip interface menu and YES confirmation (use defaults/CLI args)")
    parser.add_argument("--wait-ready", action="store_true",
                        help="Sniff for FPGA health packets and only send once both arrive")
    parser.add_argument("--ready-interface", default=DEFAULT_READY_INTERFACE,
                        help="Interface to sniff for FPGA health packets. Default: %(default)s")
    parser.add_argument("--ready-timeout", type=int, default=DEFAULT_READY_TIMEOUT_SEC,
                        help="Timeout (s) waiting for FPGA health packets. Default: %(default)s")
    parser.add_argument("--ready-delay", type=int, default=DEFAULT_READY_POST_DELAY_SEC,
                        help="Delay (s) after both packets seen, before sending. Default: %(default)s")
    args = parser.parse_args()

    print("""
   ====================================================
          DCU FPGA Firmware Loader (VLAN-Tagged)
   ====================================================
   Packet format: ETH + VLAN + IPv4 + UDP + AFDX Payload
   Matches RemoteConfigSender packet structure
   ====================================================
    """)

    if os.name == 'posix':
        if os.geteuid() != 0:
            print("[ERROR] Raw packet injection requires root privileges.")
            print("[!] Please run with: sudo python3 fpga_firmware_loader.py")
            sys.exit(1)

    setup_scapy_environment()

    if args.non_interactive:
        selected_iface = args.interface
        print(f"[+] Using interface: {selected_iface}")
    else:
        selected_iface = select_interface()
        print(f"[+] Selected Interface: {selected_iface}")

    if args.spi_path:
        if not os.path.isfile(args.spi_path):
            print(f"[ERROR] SPI file not found: {args.spi_path}")
            sys.exit(1)
        spi_path = args.spi_path
        print(f"[+] Using SPI file from CLI: {spi_path}")
    else:
        spi_path = get_valid_input(
            "\nEnter absolute path to .spi file: ",
            validator=lambda x: os.path.isfile(x),
            error_msg="File does not exist. Please enter a valid path."
        )

    try:
        lru_id = parse_lru_id(args.lru_id)
    except ValueError:
        print(f"[ERROR] Invalid --lru-id value: {args.lru_id}")
        sys.exit(1)

    vlan_id = args.vlan_id
    bag_rate = args.inter_packet_delay
    initial_delay = args.initial_delay

    print(f"LRU ID:                  {hex(lru_id)}")
    print(f"VLAN ID:                 {vlan_id}")
    print(f"VL ID:                   {args.vl_id}")
    print(f"Network:                 {args.network}")
    print(f"Inter-packet delay (ms): {bag_rate}")
    print(f"Initial delay (s):       {initial_delay}")

    if not args.non_interactive:
        print("\n" + "!" * 50)
        print("ACTION REQUIRED:")
        print(f"1. Set LRU ID on the physical device to {hex(lru_id)}")
        print("2. Power UP the device.")
        print(f"3. Ensure Cumulus switch is configured (run cumulus_setup.py first)")
        print("!" * 50)

        while True:
            ready = input("\nType 'YES' to confirm device is ready and start transmission: ").strip().upper()
            if ready == 'YES':
                break
            print("Waiting for confirmation...")

    if args.wait_ready:
        if not wait_for_fpga_ready(args.ready_interface,
                                    args.ready_timeout,
                                    args.ready_delay):
            sys.exit(1)

    success = send_firmware_update(
        spi_file=spi_path,
        interface=selected_iface,
        lru_id=lru_id,
        vlan_id=vlan_id,
        vl_id=args.vl_id,
        network=args.network,
        initial_delay_sec=initial_delay,
        inter_packet_delay_ms=bag_rate
    )

    if success:
        print("\n[+] Done! Firmware update completed successfully.")
    else:
        print("\n[-] Errors occurred during transmission.")


if __name__ == "__main__":
    main()