"""Claude 2026-11-13: live view of the received remote video in a browser, decoded in the browser (WebCodecs), not on the board.
usage: python3 view_server.py <run_dir> [port=18750] [seconds=900]
   The board only streams the bytes of <run_dir>/received.h264 as they are written (from the last SPS on), so the view costs
   the board almost no CPU (decoding 1080p H.264 on the board disturbed the radio link).  WebCodecs needs a secure context:
   open it through an ssh tunnel as http://localhost:<port>/, e.g.  ssh -N -L 18750:127.0.0.1:18750 xilinx@<board>"""
import os, signal, subprocess, sys, threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
run = sys.argv[1]; port = int(sys.argv[2]) if len(sys.argv) > 2 else 18750; life = float(sys.argv[3]) if len(sys.argv) > 3 else 900
src = os.path.join(run, 'received.h264'); here = os.path.dirname(os.path.abspath(__file__))
PAGE = open(os.path.join(here, 'view.html'), 'rb').read()
CUR = []; LOCK = threading.Lock()
def start_offset():
    """Offset of the last SPS (00 00 00 01 67) in the file, so decoding can start immediately."""
    try:
        size = os.path.getsize(src)
        with open(src, 'rb') as f:
            f.seek(max(0, size - 4_000_000)); base = f.tell(); b = f.read()
        i = b.rfind(b'\x00\x00\x00\x01\x67')
        return base + i if i >= 0 else size
    except OSError: return 0
class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def do_POST(self):                              # the page reports what it shows -> <run_dir>/view_stats.log
        n = int(self.headers.get('Content-Length', '0'))
        if self.path != '/stat' or not 0 < n <= 512: self.send_error(400); return
        with open(os.path.join(run, 'view_stats.log'), 'ab') as f: f.write(self.rfile.read(n) + b'\n')
        self.send_response(204); self.send_header('Content-Length', '0'); self.end_headers()
    def do_GET(self):
        if self.path == '/':
            self.send_response(200); self.send_header('Content-Type', 'text/html; charset=utf-8'); self.send_header('Content-Length', str(len(PAGE))); self.send_header('Cache-Control', 'no-store'); self.end_headers(); self.wfile.write(PAGE); return
        if self.path != '/raw': self.send_error(404); return
        with LOCK:                                  # one stream at a time: a new viewer replaces the previous one
            for p in CUR:
                if p.poll() is None:
                    try: os.killpg(p.pid, 15)
                    except ProcessLookupError: pass
            p = subprocess.Popen(['tail', '-c', '+%d' % (start_offset() + 1), '-F', src], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, preexec_fn=os.setsid); CUR[:] = [p]
        self.send_response(200); self.send_header('Content-Type', 'application/octet-stream'); self.send_header('Cache-Control', 'no-store'); self.send_header('Transfer-Encoding', 'chunked'); self.end_headers()
        try:
            while True:
                b = p.stdout.read1(65536)
                if not b: break
                self.wfile.write(b'%x\r\n' % len(b) + b + b'\r\n')
        except (BrokenPipeError, ConnectionResetError): pass
        finally:
            try: os.killpg(p.pid, 15)
            except ProcessLookupError: pass
def stop(*a):
    for p in CUR:
        try: os.killpg(p.pid, 15)
        except ProcessLookupError: pass
    os._exit(0)
signal.signal(signal.SIGTERM, stop)
srv = ThreadingHTTPServer(('0.0.0.0', port), H); srv.daemon_threads = True
tm = threading.Timer(life, stop); tm.daemon = True; tm.start(); print('VIEW_READY port', port, flush=True)
srv.serve_forever()
