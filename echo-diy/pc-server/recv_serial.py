# -*- coding: utf-8 -*-
"""
Echo DIY — Stage 1 电脑端接收脚本
从 USB 串口读取板子发来的 WAV 帧, 保存为 .wav 文件

帧协议:
  [0xAA 0x55] [4字节小端长度] [WAV 原始字节(含44字节头)]

用法:
  python recv_serial.py [COM口] [输出目录]
  例: python recv_serial.py COM3 recordings
"""
import sys, os, time, struct
import serial

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM3"
    outdir = sys.argv[2] if len(sys.argv) > 2 else "recordings"
    os.makedirs(outdir, exist_ok=True)

    ser = serial.Serial(port, 115200, timeout=1)
    # 复位板子, 触发重新录音 (DTR 低→高)
    ser.setDTR(False); time.sleep(0.3); ser.setDTR(True)
    print(f"已复位板子, 监听 {port} ... 等待 WAV (Ctrl+C 退出)")

    # 状态机: 找帧头
    buf = b""
    while True:
        try:
            chunk = ser.read(4096)
        except KeyboardInterrupt:
            break
        if not chunk:
            continue
        buf += chunk

        # 找帧头 AA 55
        while True:
            idx = buf.find(b"\xaa\x55")
            if idx < 0:
                # 保留最后1字节(可能是 AA 的一半)
                buf = buf[-1:]
                break
            if idx > 0:
                buf = buf[idx:]  # 丢弃帧头前的杂讯
            if len(buf) < 6:
                break
            length = struct.unpack("<I", buf[2:6])[0]
            if len(buf) < 6 + length:
                break  # 等更多数据
            wav = buf[6:6+length]
            buf = buf[6+length:]

            # 校验 WAV 头
            if wav[:4] == b"RIFF" and wav[8:12] == b"WAVE":
                ts = time.strftime("%Y%m%d_%H%M%S")
                path = os.path.join(outdir, f"rec_{ts}.wav")
                with open(path, "wb") as f:
                    f.write(wav)
                dur = (length - 44) / 32000.0
                print(f"[OK] 保存 {path}  ({length} 字节, 约 {dur:.1f} 秒)")
            else:
                print(f"[WARN] 收到非 WAV 数据 {length} 字节, 丢弃")

if __name__ == "__main__":
    main()
