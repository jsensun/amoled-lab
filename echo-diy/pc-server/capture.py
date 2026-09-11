# -*- coding: utf-8 -*-
"""临时抓包: 复位板子, 完整捕获串口输出 (文本+二进制标记)"""
import sys, time, serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM3"
dur = float(sys.argv[2]) if len(sys.argv) > 2 else 40

ser = serial.Serial(port, 115200, timeout=0.2)
ser.setDTR(False); time.sleep(0.3); ser.setDTR(True)
print(f"reset done, capturing {dur}s...", flush=True)

t0 = time.time()
buf = b""
while time.time() - t0 < dur:
    chunk = ser.read(4096)
    if not chunk:
        continue
    buf += chunk
    # 按行切分, 二进制段标记
    while True:
        nl = buf.find(b"\n")
        if nl < 0:
            break
        line = buf[:nl].rstrip(b"\r")
        buf = buf[nl+1:]
        try:
            text = line.decode("utf-8")
            print(f"[{time.time()-t0:6.1f}s] {text}", flush=True)
        except UnicodeDecodeError:
            print(f"[{time.time()-t0:6.1f}s] <BIN {len(line)}B>", flush=True)
if buf:
    try:
        print(f"[{time.time()-t0:6.1f}s] {buf.decode('utf-8')}", flush=True)
    except UnicodeDecodeError:
        print(f"[{time.time()-t0:6.1f}s] <BIN {len(buf)}B>", flush=True)
ser.close()
print("capture done", flush=True)