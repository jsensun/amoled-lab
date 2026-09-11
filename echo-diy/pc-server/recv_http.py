# -*- coding: utf-8 -*-
"""
Echo DIY — Stage 2 电脑端接收服务器 (零依赖, 仅 Python 标准库)

ESP32 通过 WiFi HTTP POST 上传 WAV → 保存到 recordings/

用法:
  python recv_http.py [端口] [输出目录]
  例: python recv_http.py 8080 recordings

注意: 需在防火墙放行该端口 (或首次运行时允许 Python 访问网络)
"""
import sys, os, time
from http.server import HTTPServer, BaseHTTPRequestHandler

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
OUTDIR = sys.argv[2] if len(sys.argv) > 2 else "recordings"
os.makedirs(OUTDIR, exist_ok=True)


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        body = b"Echo DIY server OK. POST /upload with WAV bytes."
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        data = self.rfile.read(length)
        if data[:4] == b"RIFF" and data[8:12] == b"WAVE":
            ts = time.strftime("%Y%m%d_%H%M%S")
            path = os.path.join(OUTDIR, f"rec_{ts}.wav")
            with open(path, "wb") as f:
                f.write(data)
            dur = (len(data) - 44) / 32000.0
            msg = f"[OK] 保存 {path} ({len(data)} 字节, 约 {dur:.1f} 秒)"
            print(msg, flush=True)
            resp = msg.encode("utf-8")
            self.send_response(200)
        else:
            resp = "[WARN] 非 WAV 数据, 丢弃".encode("utf-8")
            print(resp.decode("utf-8"), flush=True)
            self.send_response(400)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(resp)))
        self.end_headers()
        self.wfile.write(resp)

    def log_message(self, fmt, *args):
        pass  # 静默访问日志


if __name__ == "__main__":
    print(f"Echo DIY 接收服务器: http://0.0.0.0:{PORT}/  (保存到 {OUTDIR})", flush=True)
    HTTPServer(("0.0.0.0", PORT), Handler).serve_forever()