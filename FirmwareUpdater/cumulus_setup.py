#!/usr/bin/env python3
"""
Cumulus Switch Setup Script (Standalone)

Sets up the Cumulus switch independently from the main test system.
Deploys the interfaces file and configures egress untagged VLANs.

Must be run from a machine that has SSH access to the Cumulus switch (10.1.33.3).

Usage:
    sudo python3 cumulus_setup.py
"""

import argparse
import sys
import time
import paramiko
import os

# ==============================================================================
# CUMULUS SWITCH CONNECTION
# ==============================================================================

CUMULUS_IP = "10.1.33.3"
CUMULUS_USER = "cumulus"
CUMULUS_PASS = "%T86Ovk7RCH%h@CC"
CUMULUS_SSH_PORT = 22

INTERFACES_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               "..", "CumulusInterfaces", "DTNIRSW", "interfaces")

# ==============================================================================
# EGRESS UNTAGGED VLAN CONFIGURATION
# ==============================================================================
# Port pair: (switch_port, vlan_id)
# swp25s0-s3 = DTN Port 0-3 (FPGA side, 1G breakout)
# swp26s0-s3 = DTN Port 4-7
# swp27s0-s3 = DTN Port 8-11
# swp28s0-s3 = DTN Port 12-15
# swp29s0-s3 = DTN Port 16-19
# swp30s0-s3 = DTN Port 20-23
# swp31s0-s3 = DTN Port 24-27
# swp32s0-s3 = DTN Port 28-31

EGRESS_UNTAGGED_CONFIG = [
    # swp25 (Port 2 / DTN 0-3) - VLAN 97-100
    ("swp25s0", 97),
    ("swp25s1", 98),
    ("swp25s2", 99),
    ("swp25s3", 100),
    # swp26 (Port 3 / DTN 4-7) - VLAN 101-104
    ("swp26s0", 101),
    ("swp26s1", 102),
    ("swp26s2", 103),
    ("swp26s3", 104),
    # swp27 (Port 0 / DTN 8-11) - VLAN 105-108
    ("swp27s0", 105),
    ("swp27s1", 106),
    ("swp27s2", 107),
    ("swp27s3", 108),
    # swp28 (Port 1 / DTN 12-15) - VLAN 109-112
    ("swp28s0", 109),
    ("swp28s1", 110),
    ("swp28s2", 111),
    ("swp28s3", 112),
    # swp29 (Port 4 / DTN 16-19) - VLAN 113-116
    ("swp29s0", 113),
    ("swp29s1", 114),
    ("swp29s2", 115),
    ("swp29s3", 116),
    # swp30 (Port 5 / DTN 20-23) - VLAN 117-120
    ("swp30s0", 117),
    ("swp30s1", 118),
    ("swp30s2", 119),
    ("swp30s3", 120),
    # swp31 (Port 6 / DTN 24-27) - VLAN 121-124
    ("swp31s0", 121),
    ("swp31s1", 122),
    ("swp31s2", 123),
    ("swp31s3", 124),
    # swp32 (Port 7 / DTN 28-31) - VLAN 125-128
    ("swp32s0", 125),
    ("swp32s1", 126),
    ("swp32s2", 127),
    ("swp32s3", 128),
]


class CumulusSetup:
    def __init__(self, ip=CUMULUS_IP, user=CUMULUS_USER, password=CUMULUS_PASS, port=CUMULUS_SSH_PORT):
        self.ip = ip
        self.user = user
        self.password = password
        self.port = port
        self.ssh = None

    def connect(self):
        print(f"[Cumulus] Connecting to {self.ip}...")
        self.ssh = paramiko.SSHClient()
        self.ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        try:
            self.ssh.connect(self.ip, port=self.port, username=self.user,
                             password=self.password, timeout=10)
            print(f"[Cumulus] Connected successfully.")
            return True
        except Exception as e:
            print(f"[Cumulus] Connection failed: {e}")
            return False

    def disconnect(self):
        if self.ssh:
            self.ssh.close()
            self.ssh = None
            print("[Cumulus] Disconnected.")

    def execute(self, command, use_sudo=False):
        if not self.ssh:
            print("[Cumulus] Not connected!")
            return False, ""

        if use_sudo:
            command = f"echo '{self.password}' | sudo -S {command}"

        stdin, stdout, stderr = self.ssh.exec_command(command, timeout=30)
        exit_code = stdout.channel.recv_exit_status()
        output = stdout.read().decode('utf-8', errors='replace').strip()
        error = stderr.read().decode('utf-8', errors='replace').strip()

        if exit_code != 0 and error:
            filtered = "\n".join(
                line for line in error.splitlines()
                if "[sudo]" not in line and "password for" not in line
            )
            if filtered:
                print(f"[Cumulus] Warning: {filtered}")

        return exit_code == 0, output

    def deploy_interfaces(self, interfaces_path):
        print("\n" + "=" * 50)
        print("[Cumulus] Deploying Network Interfaces")
        print("=" * 50)

        resolved = os.path.abspath(interfaces_path)
        if not os.path.isfile(resolved):
            print(f"[Cumulus] Error: interfaces file not found: {resolved}")
            return False

        print(f"[Cumulus] Using: {resolved}")

        # Upload via SFTP
        print("[Cumulus] [Step 1/3] Uploading interfaces file...")
        try:
            sftp = self.ssh.open_sftp()
            remote_tmp = "/tmp/interfaces_upload"
            sftp.put(resolved, remote_tmp)
            sftp.close()
        except Exception as e:
            print(f"[Cumulus] SFTP upload failed: {e}")
            return False

        # Move to /etc/network/interfaces with sudo
        print("[Cumulus] [Step 2/3] Installing interfaces file...")
        ok, _ = self.execute(f"cp {remote_tmp} /etc/network/interfaces", use_sudo=True)
        if not ok:
            print("[Cumulus] Error: Failed to copy interfaces file")
            return False

        # Run ifreload -a
        print("[Cumulus] [Step 3/3] Reloading network interfaces (ifreload -a)...")
        ok, output = self.execute("ifreload -a", use_sudo=True)
        if not ok:
            print("[Cumulus] Warning: ifreload -a returned non-zero (changes may still be applied)")

        print("[Cumulus] Network interfaces deployed successfully!")
        return True

    def configure_egress_untagged(self):
        print("\n" + "=" * 50)
        print("[Cumulus] Configuring Egress Untagged VLANs")
        print("=" * 50)

        success_count = 0
        fail_count = 0

        for port, vlan_id in EGRESS_UNTAGGED_CONFIG:
            cmd = f"bridge vlan add dev {port} vid {vlan_id} untagged"
            ok, _ = self.execute(cmd, use_sudo=True)
            if ok:
                print(f"  [OK] {port} -> VLAN {vlan_id} (untagged)")
                success_count += 1
            else:
                print(f"  [FAIL] {port} -> VLAN {vlan_id}")
                fail_count += 1

        print(f"\n[Cumulus] Egress untagged: {success_count} OK, {fail_count} FAIL")
        return fail_count == 0

    def verify_bridge_vlans(self):
        print("\n" + "=" * 50)
        print("[Cumulus] Verifying Bridge VLAN Table")
        print("=" * 50)

        ok, output = self.execute("bridge vlan show", use_sudo=True)
        if ok and output:
            print(output)
        else:
            print("[Cumulus] Could not retrieve bridge VLAN table")

    def full_setup(self, interfaces_path=None):
        if interfaces_path is None:
            interfaces_path = INTERFACES_FILE

        if not self.connect():
            return False

        try:
            # Step 1: Deploy interfaces file
            if not self.deploy_interfaces(interfaces_path):
                return False

            print("\n[Cumulus] Waiting 3 seconds for interfaces to stabilize...")
            time.sleep(3)

            # Step 2: Configure egress untagged VLANs
            if not self.configure_egress_untagged():
                print("[Cumulus] Warning: Some egress untagged configurations failed")

            # Step 3: Verify
            self.verify_bridge_vlans()

            print("\n" + "=" * 50)
            print("[Cumulus] Switch Setup Complete!")
            print("=" * 50)
            return True

        finally:
            self.disconnect()


def main():
    parser = argparse.ArgumentParser(description="Cumulus Switch Setup")
    parser.add_argument("--interfaces-path", default=None,
                        help="Absolute path to interfaces file (skips lookup/prompt)")
    parser.add_argument("--yes", "-y", action="store_true",
                        help="Skip the start confirmation prompt")
    args = parser.parse_args()

    print("""
   ====================================================
          Cumulus Switch Setup (Standalone)
   ====================================================
    """)

    interfaces_path = args.interfaces_path or INTERFACES_FILE

    if not os.path.isfile(interfaces_path):
        print(f"[ERROR] Interfaces file not found: {interfaces_path}")
        if args.interfaces_path:
            sys.exit(1)
        print("[INFO] Expected at: CumulusInterfaces/DTNIRSW/interfaces")
        alt = input("Enter path to interfaces file (or press Enter to abort): ").strip()
        if not alt:
            sys.exit(1)
        interfaces_path = alt

    print(f"Interfaces file: {interfaces_path}")
    print(f"Cumulus switch:  {CUMULUS_IP}")
    print()

    if not args.yes:
        confirm = input("Start Cumulus setup? (y/n): ").strip().lower()
        if confirm != 'y':
            print("Aborted.")
            sys.exit(0)

    setup = CumulusSetup()
    success = setup.full_setup(interfaces_path)

    if success:
        print("\n[+] Cumulus switch is ready. You can now run the firmware updater.")
    else:
        print("\n[-] Cumulus setup failed. Check connection and try again.")
        sys.exit(1)


if __name__ == "__main__":
    main()