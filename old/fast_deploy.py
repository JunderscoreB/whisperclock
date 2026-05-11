#!/usr/bin/env python3
import os
import glob
import subprocess
import sys

def main():
    print("=============================================")
    print("🚀 PEBBLE NATIVE PFS INJECTOR DEPLOYMENT 🚀")
    print("=============================================\n")

    # 1. Find the compiled app
    pbws = glob.glob("build/*.pbw")
    if not pbws:
        print("❌ No .pbw found! Please run 'pebble build' first.")
        sys.exit(1)
    pbw_path = os.path.abspath(pbws[0])
    print(f"📦 Found payload: {os.path.basename(pbw_path)} ({os.path.getsize(pbw_path) / 1024:.1f} KB)")

    # 2. Setup paths
    flash_path = os.path.expanduser("~/.pebble-sdk/4.9.148/emery/qemu_spi_flash.bin")
    injector = os.path.expanduser("~/pebble_dev/injector/pfs_injector")

    if not os.path.exists(injector):
        print("❌ Injector binary not found! Did it compile successfully?")
        sys.exit(1)

    # 3. Unlock the flash drive
    print("🛑 Halting background QEMU instances...")
    subprocess.run(["killall", "qemu-pebble"], stderr=subprocess.DEVNULL)

    # 4. Inject the PBW into the virtual flash drive
    # We write it as 'sideload.pbw' so it's waiting natively on the filesystem
    print(f"💉 Injecting payload directly into virtual SPI Flash...")
    result = subprocess.run([injector, flash_path, pbw_path, "sideload.pbw"])
    
    if result.returncode != 0:
        print("❌ PFS Injection failed.")
        sys.exit(1)

    # 5. Boot the emulator and install
    print("✅ Injection complete! Booting QEMU...")
    print("⌚ The watch will now boot with your massive assets already on the hard drive.")
    
    # We call the standard install, but because the payload is already on the 
    # drive, it bypasses the serial bottleneck entirely!
    os.system("pebble install --emulator emery -vv")

if __name__ == "__main__":
    main()
