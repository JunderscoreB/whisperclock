#!/bin/bash
# ==============================================================================
# WhisperClock Smart Deployment Pipeline
# Safely manages the QEMU lifecycle and handles Cold Boot race conditions.
# ==============================================================================

PAYLOAD="build/whisperclock.pbw"
MAX_RETRIES=3
RETRY_COUNT=0
SUCCESS=0

echo "[*] Terminating ghost pypkjs background daemons..."
pebble kill > /dev/null 2>&1

echo "[*] Wiping Emery virtual flash to prevent fragmentation..."
pebble wipe > /dev/null 2>&1

# Give the QEMU process a moment to completely exit before restarting
sleep 2 

while [ $RETRY_COUNT -lt $MAX_RETRIES ]; do
    echo "[*] Attempt $((RETRY_COUNT+1)) of $MAX_RETRIES: Booting Emery & Installing WhisperClock..."
    
    # We run the install command. If it succeeds, it returns a 0 exit code.
    pebble install $PAYLOAD --emulator emery --logs -vv
    
    # Capture the exit code of the pebble install command
    if [ $? -eq 0 ]; then
        echo "[*] ========================================="
        echo "[*] SUCCESS! Payload successfully written."
        echo "[*] ========================================="
        SUCCESS=1
        break
    else
        echo "[!] Deployment timeout or crash detected."
        echo "[!] Letting the emulator breathe for 3 seconds before retrying..."
        sleep 3
        RETRY_COUNT=$((RETRY_COUNT+1))
    fi
done

if [ $SUCCESS -eq 0 ]; then
    echo "[!] Deployment failed after $MAX_RETRIES attempts. Check host CPU load."
    exit 1
fi
