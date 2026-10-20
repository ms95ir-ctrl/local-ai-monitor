#!/usr/bin/env python3
"""Feed the ESP32 dashboard, ~4 updates/second, over USB serial and/or WiFi:
  P100,...  GPU stats from nvidia-smi
  LLM,...   llama-server stats from /slots and /metrics (read-only; never sends prompts)

USB serial is optional: if the port is missing the feed keeps running for WiFi clients.
WiFi: a TCP server on NET_PORT streams the same lines to connected boards and receives their SET,... commands.
A UDP beacon ("ESPDASH,<port>,<hostname>") every 2 s on BEACON_PORT lets boards on the LAN find this machine.
Only peers on private (LAN) addresses are accepted.

usage: p100_feed.py [serial-port] [llama-server-url]
"""
import ipaddress, json, re, socket, subprocess, sys, threading, time, urllib.request
import serial

NET_PORT, BEACON_PORT = 8765, 8766

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
LLAMA = sys.argv[2] if len(sys.argv) > 2 else "http://127.0.0.1:8081"
FIELDS = "temperature.gpu,utilization.gpu,memory.used,memory.total,power.draw,power.limit,clocks.sm,clocks.mem"


def num(s, default="0"):
    s = s.strip()
    try:
        float(s)
        return s
    except ValueError:
        return default


def gpu_line():
    out = subprocess.check_output(
        ["nvidia-smi", "-i", "0", f"--query-gpu={FIELDS}", "--format=csv,noheader,nounits"], text=True, timeout=5)
    return "P100," + ",".join(num(x) for x in out.strip().split(",")) + "\n"


def http(path, timeout=1.0):
    with urllib.request.urlopen(LLAMA + path, timeout=timeout) as r:
        return r.read().decode()


def parse_metrics(txt):
    m = {}
    for line in txt.splitlines():
        if line.startswith("llamacpp:"):
            k, _, v = line.partition(" ")
            try:
                m[k[9:]] = float(v)
            except ValueError:
                pass
    return m


class Llm:
    def __init__(self):
        self.model, self.quant = "llama-server", ""
        self.props_at = 0
        self.uptime_base, self.uptime_at = 0, 0
        self.prev_m = None
        self.prev_slot = None  # (t, task_id, n_decoded, n_processed)
        self.gen = self.prompt = 0.0
        self.ctx_task, self.ctx_used = None, 0

    def refresh_static(self):
        now = time.time()
        if now - self.props_at < 30:
            return
        self.props_at = now
        try:
            p = json.loads(http("/props"))
            name = re.sub(r"\.gguf$", "", p.get("model_path", p.get("model_alias", "llama-server")).split("/")[-1])
            q = re.search(r"[-.]((?:IQ|Q|F|BF)\d[A-Za-z0-9_]*)$", name)
            self.quant = q.group(1) if q else "-"   # never empty: the firmware sscanf fails on an empty field and keeps the default name
            self.model = (name[: q.start()] if q else name).replace(",", " ")
        except Exception:
            pass
        try:  # uptime of the llama-server process serving this port
            port = LLAMA.rsplit(":", 1)[-1]
            ps = subprocess.check_output(["ps", "-eo", "etimes=,args="], text=True)
            for line in ps.splitlines():
                if "llama-server" in line and f"--port {port}" in line:
                    self.uptime_base, self.uptime_at = int(line.split()[0]), now
                    break
        except Exception:
            pass

    def line(self):
        self.refresh_static()
        try:
            slots = json.loads(http("/slots"))
            m = parse_metrics(http("/metrics"))
        except Exception:
            self.prev_slot = self.prev_m = None
            return f"LLM,0,0,0,0,0,0,0,0,0,0,{self.quant},{self.model}\n"
        now = time.time()

        # pick the busiest slot (or slot 0)
        busy = [s for s in slots if s.get("is_processing")]
        s = busy[0] if busy else slots[0]
        nt = (s.get("next_token") or [{}])[0]
        decoded = int(nt.get("n_decoded", 0) or 0)
        processed = int(s.get("n_prompt_tokens_processed", 0) or 0)
        n_prompt = int(s.get("n_prompt_tokens", 0) or 0)
        task = s.get("id_task")
        proc = bool(s.get("is_processing"))
        n_ctx = int(s.get("n_ctx", 0))

        # context in use: grows with the current task, kept after it finishes
        if task != self.ctx_task:
            self.ctx_task, self.ctx_used = task, 0
        self.ctx_used = max(self.ctx_used, n_prompt + decoded)

        state = 1
        if proc:
            state = 3 if decoded > 0 else 2
            prev = self.prev_slot
            if prev and prev[1] == task and now > prev[0]:
                dt = now - prev[0]
                if state == 3 and decoded >= prev[2]:
                    self.gen = (decoded - prev[2]) / dt if decoded > prev[2] else self.gen
                elif state == 2 and processed > prev[3]:
                    self.prompt = (processed - prev[3]) / dt
        self.prev_slot = (now, task, decoded, processed)

        # when a request completes, the server counters give exact averages for it
        pm = self.prev_m
        if pm:
            dtok = m.get("tokens_predicted_total", 0) - pm.get("tokens_predicted_total", 0)
            dsec = m.get("tokens_predicted_seconds_total", 0) - pm.get("tokens_predicted_seconds_total", 0)
            if dtok > 0 and dsec > 0 and not proc:
                self.gen = dtok / dsec
            ptok = m.get("prompt_tokens_total", 0) - pm.get("prompt_tokens_total", 0)
            psec = m.get("prompt_seconds_total", 0) - pm.get("prompt_seconds_total", 0)
            if ptok > 0 and psec > 0:
                self.prompt = ptok / psec
        self.prev_m = m

        pt, ps = m.get("tokens_predicted_total", 0), m.get("tokens_predicted_seconds_total", 0)
        qt, qs = m.get("prompt_tokens_total", 0), m.get("prompt_seconds_total", 0)
        avg_gen = pt / ps if ps else 0
        avg_prompt = qt / qs if qs else 0
        up = self.uptime_base + (now - self.uptime_at if self.uptime_at else 0)
        return (f"LLM,{state},{self.ctx_used},{n_ctx},{self.gen:.1f},{self.prompt:.1f},{int(pt)},{int(qt)},"
                f"{avg_gen:.1f},{avg_prompt:.1f},{int(up)},{self.quant},{self.model}\n")


FAN_SOCK = "/run/fanctl.sock"


def fanctl(req):
    """Talk to the root fanctl daemon; returns its status dict or None if it isn't running."""
    import socket
    try:
        c = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        c.settimeout(1.0)
        c.connect(FAN_SOCK)
        c.sendall((json.dumps(req) + "\n").encode())
        data = b""
        while not data.endswith(b"\n"):
            chunk = c.recv(4096)
            if not chunk:
                break
            data += chunk
        c.close()
        r = json.loads(data)
        return None if "error" in r else r
    except Exception:
        return None


fan_ids = []


def fan_line(st=None):
    st = st or fanctl({"cmd": "status"})
    if not st:
        return "FAN,none,0\n"
    global fan_ids
    fan_ids = [f["id"] for f in st["fans"]]
    parts = ["FAN", st["profile"], str(len(st["fans"]))]
    for f in st["fans"]:
        temp = f["temp"] if f["temp"] is not None else 0
        parts += [f["name"].replace(",", " "), str(f["rpm"]), str(f["pct"]), str(temp), "1" if f["manual"] else "0"]
    return ",".join(parts) + "\n"


def handle_command(line):
    """Commands sent by the ESP32 touch UI: SET,PROFILE,<name> | SET,FAN,<idx>,<pct>"""
    p = line.strip().split(",")
    if len(p) >= 3 and p[0] == "SET" and p[1] == "PROFILE":
        return fanctl({"cmd": "profile", "name": p[2]})
    if len(p) >= 4 and p[0] == "SET" and p[1] == "FAN":
        try:
            return fanctl({"cmd": "manual", "fan": fan_ids[int(p[2])], "pct": int(p[3])})
        except (ValueError, IndexError):
            return None
    return None


def open_port():
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = PORT, 115200, 0
    s.dtr = s.rts = False  # avoid pulsing EN/BOOT and resetting the board
    s.open()
    return s


class GpuSampler(threading.Thread):
    """One long-running nvidia-smi sampling ~5x/s (no fork per reading); keeps the latest P100 line."""
    def __init__(self):
        super().__init__(daemon=True)
        self.line, self.at = None, 0.0

    def run(self):
        while True:
            try:
                p = subprocess.Popen(["nvidia-smi", "-i", "0", f"--query-gpu={FIELDS}", "--format=csv,noheader,nounits", "-lms", "200"],
                                     stdout=subprocess.PIPE, text=True)
                for row in p.stdout:
                    parts = row.strip().split(",")
                    if len(parts) >= 8:
                        self.line, self.at = "P100," + ",".join(num(x) for x in parts[:8]) + "\n", time.time()
                p.wait()
            except Exception as e:
                print("sampler error:", e, file=sys.stderr, flush=True)
            time.sleep(1)


class Poller(threading.Thread):
    """Runs a slow source (llama-server HTTP, fanctl socket) in the background and caches its latest line."""
    def __init__(self, fn, period):
        super().__init__(daemon=True)
        self.fn, self.period, self.line, self.kick = fn, period, None, threading.Event()

    def run(self):
        while True:
            try: self.line = self.fn()
            except Exception as e: print("poll error:", e, file=sys.stderr, flush=True)
            self.kick.wait(self.period); self.kick.clear()


class NetHub:
    """TCP side of the feed: streams lines to every connected board, collects their SET,... commands."""
    def __init__(self, cmds):
        self.cmds, self.clients, self.lock = cmds, [], threading.Lock()
        srv = socket.socket()
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("", NET_PORT))
        srv.listen(4)
        self.srv = srv
        threading.Thread(target=self.accept_loop, daemon=True).start()
        threading.Thread(target=self.beacon_loop, daemon=True).start()

    def accept_loop(self):
        while True:
            try:
                c, addr = self.srv.accept()
                if not ipaddress.ip_address(addr[0]).is_private:
                    c.close()
                    continue
                c.settimeout(0.5)
                c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                with self.lock:
                    self.clients.append(c)
                print("net client", addr[0], flush=True)
                threading.Thread(target=self.reader, args=(c, addr[0]), daemon=True).start()
            except Exception as e:
                print("accept error:", e, file=sys.stderr, flush=True)
                time.sleep(1)

    def reader(self, c, ip):
        rx = b""
        try:
            while True:
                try:
                    chunk = c.recv(512)
                except socket.timeout:
                    continue
                if not chunk:
                    break
                rx += chunk
                while b"\n" in rx:
                    ln, rx = rx.split(b"\n", 1)
                    text = ln.decode(errors="replace")
                    if text.startswith("SET,"):
                        print("rx", text.strip(), "(wifi)", flush=True)
                        self.cmds.append(text)
        except OSError:
            pass
        self.drop(c)
        print("net client gone", ip, flush=True)

    def drop(self, c):
        with self.lock:
            if c in self.clients:
                self.clients.remove(c)
        try: c.close()
        except OSError: pass

    def send(self, data):
        with self.lock:
            clients = list(self.clients)
        for c in clients:
            try: c.sendall(data)
            except OSError: self.drop(c)

    @staticmethod
    def broadcasts():
        out = {"255.255.255.255"}
        try:
            for ifc in json.loads(subprocess.check_output(["ip", "-j", "-4", "addr"], text=True, timeout=3)):
                if ifc["ifname"].startswith(("lo", "docker", "br-", "veth", "tailscale")):
                    continue
                for a in ifc.get("addr_info", []):
                    if a.get("broadcast"):
                        out.add(a["broadcast"])
        except Exception:
            pass
        return out

    def beacon_loop(self):
        u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        u.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        msg = f"ESPDASH,{NET_PORT},{socket.gethostname()}\n".encode()
        targets, at = set(), 0
        while True:
            if time.time() - at > 30:  # interfaces come and go (WiFi roaming)
                targets, at = self.broadcasts(), time.time()
            for t in targets:
                try: u.sendto(msg, (t, BEACON_PORT))
                except OSError: pass
            time.sleep(2)


def main():
    gpu, llm = GpuSampler(), Llm()
    gpu.start()
    llm_p, fan_p = Poller(llm.line, 0.5), Poller(fan_line, 1.0)
    llm_p.start(); fan_p.start()
    lock = threading.Lock()
    cmds = []
    net = NetHub(cmds)
    ser, rx = None, b""

    def emit(text):
        data = text.encode()
        net.send(data)
        if ser: ser.write(data)

    def cmd_worker():
        while True:
            while not cmds: time.sleep(0.02)
            text = cmds.pop(0)
            st = handle_command(text)
            if st:
                with lock:
                    try: emit(fan_line(st))
                    except Exception: pass
            fan_p.kick.set()
    threading.Thread(target=cmd_worker, daemon=True).start()
    nxt, tick, last_open = time.time(), 0, 0.0
    while True:
        try:
            if ser is None and time.time() - last_open > 2:  # USB is optional: keep serving WiFi clients without it
                last_open = time.time()
                try: ser = open_port()
                except (serial.SerialException, OSError): ser = None
            line = gpu.line if gpu.line and time.time() - gpu.at < 2 else None
            with lock:
                if line: emit(line)
                if tick % 2 == 0 and llm_p.line: emit(llm_p.line)
                if tick % 4 == 0 and fan_p.line: emit(fan_p.line)
            tick += 1
            nxt += 0.25
            while True:  # service touch-UI commands until the next tick
                if ser:
                    rx += ser.read(512)
                    while b"\n" in rx:
                        ln, rx = rx.split(b"\n", 1)
                        text = ln.decode(errors="replace")
                        if text.startswith("SET,"):
                            print("rx", text.strip(), flush=True)
                            cmds.append(text)
                if time.time() >= nxt: break
                time.sleep(0.02)
            if nxt < time.time() - 1: nxt = time.time()  # fell far behind (suspend, etc.): resync
        except (serial.SerialException, OSError):
            ser = None
            time.sleep(1); nxt = time.time()
        except Exception as e:
            print("error:", e, file=sys.stderr, flush=True)
            time.sleep(0.25)


if __name__ == "__main__":
    main()
