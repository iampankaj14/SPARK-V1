import serial
import time
import sys

duration = float(sys.argv[1]) if len(sys.argv) > 1 else 30.0
print(f"Listening to COM4 (safe no-reset) for {duration}s...", flush=True)
start = time.time()
ser = None

try:
    ser = serial.Serial()
    ser.port = 'COM4'
    ser.baudrate = 115200
    ser.timeout = 0.1
    ser.dtr = False
    ser.rts = False
    ser.open()
    print("[Connected without chip reset]", flush=True)
except Exception as e:
    print(f"[Open Error: {e}]", flush=True)

if ser and ser.is_open:
    while time.time() - start < duration:
        try:
            line = ser.readline()
            if line:
                print(line.decode('utf-8', errors='replace').strip(), flush=True)
        except Exception as e:
            print(f"Read error: {e}", flush=True)
            break
    ser.close()

print("[Finished]", flush=True)
