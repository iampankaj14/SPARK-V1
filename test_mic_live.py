import serial
import time
import sys

PORT = 'COM4'
BAUD = 115200

print("=" * 70)
print("     DESKIMON INMP441 LIVE MICROPHONE DIAGNOSTIC TOOL")
print("=" * 70)
print(f"Connecting to {PORT} at {BAUD} baud...")
print("Instructions:")
print(" 1. Watch the live VU-meter below.")
print(" 2. Gently scratch or tap the INMP441 mic module (gold/black hole).")
print(" 3. Click the on-screen 'Tap-to-Talk' button or press the screen.")
print(" 4. Press Ctrl+C to exit.")
print("=" * 70)

try:
    ser = serial.Serial(PORT, BAUD, timeout=1)
    time.sleep(0.5)
    ser.reset_input_buffer()
    
    while True:
        line = ser.readline().decode('latin1', errors='ignore').strip()
        if not line:
            continue
        
        if "INMP441_MIC" in line:
            # Highlight live mic level
            print(f"\033[92m{line}\033[0m")
        elif "MIC_SOURCE_AUDIT" in line:
            # Highlight tap-to-talk trigger
            print(f"\033[93;1m{line}\033[0m")
        elif "LATENCY_AUDIT" in line or "DESKIMON_UI" in line:
            print(f"\033[96m{line}\033[0m")
        elif any(k in line for k in ["AFE", "WAKENET", "Listening"]):
            print(line)

except KeyboardInterrupt:
    print("\nExiting diagnostic.")
    if 'ser' in locals() and ser.is_open:
        ser.close()
except Exception as e:
    print(f"Error: {e}")
