import serial
import time

print("Listening to serial port with auto-reconnect...")
start = time.time()
ser = None
last_data_time = time.time()

while time.time() - start < 40:
    if ser is None:
        try:
            ser = serial.Serial('/dev/cu.usbmodem14301', 115200, timeout=0.1)
            ser.dtr = False
            ser.rts = False
            print("[Connected]")
            last_data_time = time.time()
        except Exception as e:
            time.sleep(0.5)
            continue
    try:
        line = ser.readline()
        if line:
            print(line.decode('utf-8', errors='replace').strip())
            last_data_time = time.time()
        else:
            if time.time() - last_data_time > 3.0:
                print("[Timeout - Reconnecting]")
                ser.close()
                ser = None
                time.sleep(0.5)
    except serial.SerialException:
        print("[Disconnected]")
        ser.close()
        ser = None
        time.sleep(0.5)
    except Exception as e:
        print(f"Error: {e}")
        time.sleep(0.5)
if ser:
    ser.close()
print("[Finished]")
