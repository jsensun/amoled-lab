# -*- coding: utf-8 -*-
"""
Echo DIY — 自动转写服务
监听 recordings 目录, 发现新 .wav 自动用 faster-whisper 转写为中文文本
输出: 同名 .txt 文件 + 控制台打印

用法:
  python transcribe.py [录音目录]
  例: python transcribe.py recordings
"""
import os, sys, time, glob
from faster_whisper import WhisperModel

# Windows 控制台可能是 GBK, 强制 UTF-8 输出避免 print 崩溃
sys.stdout.reconfigure(encoding="utf-8", errors="replace")

DIR = sys.argv[1] if len(sys.argv) > 1 else "recordings"
os.makedirs(DIR, exist_ok=True)

print("加载 faster-whisper small 模型 ...", flush=True)
model = WhisperModel("small", device="cpu", compute_type="int8")
print(f"模型就绪, 监听 {DIR} (Ctrl+C 退出)", flush=True)

seen = set()
while True:
    for wav in sorted(glob.glob(os.path.join(DIR, "*.wav"))):
        if wav in seen:
            continue
        seen.add(wav)
        txt_path = wav[:-4] + ".txt"
        if os.path.exists(txt_path):
            continue
        print(f"[转写] {os.path.basename(wav)} ...", flush=True)
        try:
            segments, info = model.transcribe(wav, language="zh", beam_size=5, vad_filter=False)
            text = "".join(s.text for s in segments).strip()
            with open(txt_path, "w", encoding="utf-8") as f:
                f.write(text)
            print(f"[完成] {os.path.basename(wav)} -> {text}", flush=True)
        except Exception as e:
            print(f"[失败] {os.path.basename(wav)}: {e}", flush=True)
    time.sleep(2)