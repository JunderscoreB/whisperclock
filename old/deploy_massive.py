#!/home/dq/.local/share/uv/tools/pebble-tool/bin/python
import sys
import time
from pebble_tool import run_tool
from libpebble2.communication import PebbleConnection

# Save the original packet sending method
original_send = PebbleConnection.send_packet

# Create a throttled version that accepts the correct single 'packet' argument
def throttled_send(self, packet):
    # A 10ms delay limits the bandwidth to a safe speed QEMU can digest
    time.sleep(0.01)
    return original_send(self, packet)

# Apply the monkey-patch globally in memory
PebbleConnection.send_packet = throttled_send

print("==================================================")
print("🚀 RUNNING THROTTLED DEPLOYMENT FOR MASSIVE APPS 🚀")
print("==================================================")

# Override the arguments to run the standard install command
sys.argv = ["pebble", "install", "--emulator", "emery", "-vv"]

# Execute the tool!
sys.exit(run_tool())
