#!/usr/bin/env python3
"""fanctl - temperature-curve fan controller (root daemon + tiny CLI).

  fanctl daemon              run the controller (systemd service)
  fanctl status              show fans / temps / profile
  fanctl profile idle|normal|performance|auto
  fanctl set <fan-id> <pct>  manual duty for one fan (e.g. `fanctl set cpu 40`)
  fanctl --restore           hand all fans back to firmware control (used by systemd on stop)

Fans: CPU fan (motherboard header 2), Case Hub (header 1, 5 fans, no RPM wire, follows the P100), RX 580.
Safety (always applied, even in manual mode):
  CPU >= 92C -> CPU fan 100% | RX580 >= 90C -> its fan 100%
  P100 >= 70C -> every fan >= 80% | P100 >= 78C -> every fan 100%
  unreadable temperature source -> that fan 100%
On any exit the fans are returned to their original firmware (automatic) mode.
"""
import atexit, glob, json, os, signal, socket, subprocess, sys, threading, time

STATE = "/var/lib/fanctl/state.json"
ORIG = "/run/fanctl/orig.json"
SOCK = "/run/fanctl.sock"
SOCK_GROUP = "local1"

# (temp C, duty %) points, linearly interpolated; flat outside the range
CURVES = {
    # (temp C, duty %) points, linearly interpolated. Every profile tops out at 60 % (owner: 50-60 % is enough at full load).
    # "cpu" follows CPU package temp; "auto" is the smooth everyday curve (low floor, gentle slope, no steps).
    "cpu": {
        "idle":        [(0, 22), (60, 22), (72, 38), (82, 50), (88, 60)],
        "auto":        [(0, 25), (58, 25), (68, 38), (78, 50), (86, 60)],
        "normal":      [(0, 30), (55, 30), (68, 42), (78, 54), (85, 60)],
        "performance": [(0, 35), (50, 35), (65, 50), (75, 60)],
    },
    "amd": {
        "idle":        [(0, 20), (50, 20), (62, 32), (72, 50), (80, 60)],
        "auto":        [(0, 22), (52, 22), (65, 36), (75, 50), (82, 60)],
        "normal":      [(0, 30), (45, 30), (58, 42), (68, 55), (76, 60)],
        "performance": [(0, 35), (45, 35), (55, 52), (65, 60)],
    },
}
CURVES["case"] = CURVES["cpu"]
# 2 x 40 mm 15k rpm server fans in a 3D-printed duct on the P100 (header 1, no RPM wire). Follows the P100 temperature.
# Community reference (MakerWorld P100 40 mm shroud): silent below 55 C, about 70 % at 75 C for a 14k rpm fan.
# Here the card is power-limited to 150 W and sits near 70 C, so a 60 % ceiling is enough.
CURVES["hub"] = {
    "idle":        [(0, 30), (55, 30), (65, 38), (72, 47), (77, 56), (80, 60)],
    "auto":        [(0, 30), (50, 30), (58, 37), (66, 45), (72, 53), (77, 60)],
    "normal":      [(0, 35), (48, 35), (58, 44), (66, 52), (72, 60)],
    "performance": [(0, 40), (45, 40), (55, 50), (62, 60)],
}
HUB_HEADERS = {1: "Case Hub"}  # motherboard PWM headers with a controller/hub but no RPM wire
QUIET_MAX = 60  # percent; emergency overrides (P100 >= 80C, CPU >= 93C, RX580 >= 90C, no sensor) still go to 100
PROFILES = ("idle", "normal", "performance", "auto")
MIN_PCT = {"cpu": 20, "amd": 15, "case": 20, "hub": 30}  # hub fans stop at 25%; never go below 30%


def read(path, default=None):
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return default


def write(path, val):
    with open(path, "w") as f:
        f.write(str(val))


def hwmon(name):
    for d in sorted(glob.glob("/sys/class/hwmon/hwmon*")):
        if read(d + "/name") == name:
            return d
    return None


def interp(points, t):
    if t <= points[0][0]:
        return points[0][1]
    for (t0, p0), (t1, p1) in zip(points, points[1:]):
        if t <= t1:
            return p0 + (p1 - p0) * (t - t0) / (t1 - t0)
    return points[-1][1]


class Fan:
    def __init__(self, fid, name, kind, src, pwm_path, rpm_path, enable_path, manual_val, auto_val, no_tach=False):
        self.id, self.name, self.kind, self.src = fid, name, kind, src
        self.no_tach = no_tach
        self.pwm_path, self.rpm_path, self.enable_path = pwm_path, rpm_path, enable_path
        self.manual_val, self.auto_val = manual_val, auto_val
        self.cur = None      # duty we last commanded (%)
        self.manual = None   # manual duty request (%) or None
        self.temp = None

    def rpm(self):
        if self.no_tach:
            return -1
        v = read(self.rpm_path)
        return int(v) if v and v.isdigit() else 0

    def duty(self):
        v = read(self.pwm_path)
        return round(int(v) * 100 / 255) if v and v.isdigit() else 0

    def take_manual_control(self):
        if read(self.enable_path) != str(self.manual_val):
            write(self.enable_path, self.manual_val)

    def apply(self, pct):
        self.take_manual_control()
        write(self.pwm_path, max(0, min(255, round(pct * 255 / 100))))


class Controller:
    def __init__(self):
        self.lock = threading.RLock()
        self.profile = "idle"
        self.fans = []
        self.p100 = None
        self.p100_at = 0
        self.cpu_path = None
        self.amd_path = None
        self.ema = {}
        self.controlling = False
        self.discover()
        self.load_state()

    # ---- discovery ----
    def discover(self):
        cpu = hwmon("coretemp")
        self.cpu_path = cpu + "/temp1_input" if cpu else None
        amd = hwmon("amdgpu")
        self.amd_path = amd + "/temp1_input" if amd else None
        self.fans = []
        nct = hwmon("nct6779") or hwmon("nct6775")
        if nct:
            for i in range(1, 8):
                rpm_p, pwm_p = f"{nct}/fan{i}_input", f"{nct}/pwm{i}"
                if not os.path.exists(pwm_p):
                    continue
                if i in HUB_HEADERS:
                    self.fans.append(Fan("hub", HUB_HEADERS[i], "hub", "hub", pwm_p, rpm_p,
                                         f"{nct}/pwm{i}_enable", 1, 0, no_tach=True))  # firmware default: full speed
                    continue
                rpm = read(rpm_p, "0")
                if not (rpm.isdigit() and int(rpm) > 0):
                    continue  # nothing with a tach signal on this header
                sel = read(f"{nct}/pwm{i}_temp_sel", "")
                is_cpu = sel in ("7", "8")  # PECI agents = CPU
                self.fans.append(Fan("cpu" if is_cpu else f"case{i}", "CPU Fan" if is_cpu else f"Case Fan {i}",
                                     "cpu" if is_cpu else "case", "cpu" if is_cpu else "case",
                                     pwm_p, rpm_p, f"{nct}/pwm{i}_enable", 1, 5))
        if amd and os.path.exists(amd + "/pwm1"):
            self.fans.append(Fan("rx580", "RX 580", "amd", "amd", amd + "/pwm1", amd + "/fan1_input",
                                 amd + "/pwm1_enable", 1, 2))
        # remember firmware modes so we can hand control back (only the first time after boot)
        os.makedirs(os.path.dirname(ORIG), exist_ok=True)
        if not os.path.exists(ORIG):
            with open(ORIG, "w") as f:
                json.dump({fan.enable_path: read(fan.enable_path) for fan in self.fans}, f)

    def restore(self, skip_hub=False):
        try:
            with open(ORIG) as f:
                orig = json.load(f)
        except (OSError, ValueError):
            orig = {}
        for fan in self.fans:
            if skip_hub and fan.kind == "hub":
                continue
            val = orig.get(fan.enable_path) or str(fan.auto_val)
            if val == str(fan.manual_val):
                val = str(fan.auto_val)
            try:
                write(fan.enable_path, val)
            except OSError:
                pass
        self.controlling = skip_hub

    # ---- persistence ----
    def load_state(self):
        try:
            with open(STATE) as f:
                s = json.load(f)
            if s.get("profile") in PROFILES:
                self.profile = s["profile"]
            for fan in self.fans:
                m = s.get("manual", {}).get(fan.id)
                fan.manual = m if isinstance(m, (int, float)) else None
        except (OSError, ValueError):
            pass

    def save_state(self):
        os.makedirs(os.path.dirname(STATE), exist_ok=True)
        tmp = STATE + ".tmp"
        with open(tmp, "w") as f:
            json.dump({"profile": self.profile, "manual": {x.id: x.manual for x in self.fans if x.manual is not None}}, f)
        os.replace(tmp, STATE)

    # ---- sensors ----
    def temp_of(self, path):
        v = read(path) if path else None
        try:
            return int(v) / 1000.0
        except (TypeError, ValueError):
            return None

    def poll_p100(self):
        if time.time() - self.p100_at < 2:
            return
        self.p100_at = time.time()
        try:
            out = subprocess.check_output(["nvidia-smi", "-i", "0", "--query-gpu=temperature.gpu",
                                           "--format=csv,noheader,nounits"], text=True, timeout=2)
            self.p100 = float(out.strip())
        except Exception:
            pass

    # ---- control ----
    def target(self, fan, cpu, amd, profile=None):
        p = self.p100
        src = {"cpu": cpu, "amd": amd,
               "case": None if cpu is None else max(cpu, (p or 0) + 10),
               "hub": p if p is not None else (None if cpu is None else cpu - 10)}[fan.src]
        fan.temp = src
        if src is None:
            return 100.0  # fail safe
        prev = self.ema.get(fan.id, src)
        self.ema[fan.id] = prev = 0.85 * prev + 0.15 * src  # heavy smoothing: ignore short spikes
        if fan.manual is not None:
            pct = float(fan.manual)
        else:
            pct = interp(CURVES[fan.kind][profile or self.profile], prev)
        pct = max(pct, MIN_PCT[fan.kind])
        # hard limits on raw temperatures
        # quiet mode (owner request 2026-10-19): fans capped at QUIET_MAX unless something is really hot
        pct = min(pct, QUIET_MAX)
        if prev >= {"cpu": 93, "amd": 90, "case": 93, "hub": 88}[fan.kind]:
            pct = 100
        if self.p100 is not None and self.p100 >= 80:
            pct = 100
        return min(100.0, pct)

    def tick(self):
        with self.lock:
            self.poll_p100()
            cpu, amd = self.temp_of(self.cpu_path), self.temp_of(self.amd_path)
            self.released = False
            self.run_fans(self.fans, cpu, amd, None)
            self.controlling = True

    def run_fans(self, fans, cpu, amd, profile):
        for fan in fans:
            tgt = self.target(fan, cpu, amd, profile)
            if fan.cur is None or not self.controlling:
                fan.cur = fan.duty()
            urgent = self.p100 is not None and self.p100 >= 70 or (fan.temp or 0) >= 80
            if fan.manual is not None:
                fan.cur = tgt                        # manual: follow immediately
            elif tgt > fan.cur + (0 if urgent else 3):
                fan.cur = min(tgt, fan.cur + (15 if urgent else 3))   # speed up steadily
            elif tgt < fan.cur - 3:
                fan.cur = max(tgt, fan.cur - (4 if fan.cur - tgt > 12 else 1))   # slow down gently, faster when far above target
            # within +-3% of target: hold still (dead band, no hunting)
            fan.apply(fan.cur)
        self.controlling = True

    def safety_override_auto(self, cpu, amd):
        """Firmware control is trusted, except for the P100 limit the firmware knows nothing about."""
        if self.p100 is not None and self.p100 >= 70:
            for fan in self.fans:
                fan.apply(100 if self.p100 >= 78 else 80)
                self.controlling = True

    # ---- commands ----
    def status(self):
        with self.lock:
            prof = self.profile
            if prof != "auto" and any(f.manual is not None for f in self.fans):
                prof = "manual"
            return {"profile": prof, "p100": self.p100, "cpu": self.temp_of(self.cpu_path),
                    "fans": [{"id": f.id, "name": f.name, "rpm": f.rpm(), "pct": f.duty(),
                              "temp": None if f.temp is None else round(f.temp),
                              "manual": f.manual is not None} for f in self.fans]}

    def command(self, req):
        c = req.get("cmd")
        with self.lock:
            if c == "profile" and req.get("name") in PROFILES:
                self.profile = req["name"]
                for f in self.fans:
                    f.manual = None
            elif c == "manual":
                for f in self.fans:
                    if f.id == req.get("fan"):
                        if self.profile == "auto":
                            self.profile = "normal"
                        f.manual = max(0, min(100, int(req.get("pct", 0))))
            elif c != "status":
                return {"error": "bad command"}
            if c != "status":
                self.save_state()
                self.tick()
            return self.status()


def serve(ctl):
    try:
        os.unlink(SOCK)
    except OSError:
        pass
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.bind(SOCK)
    try:
        import grp
        os.chown(SOCK, 0, grp.getgrnam(SOCK_GROUP).gr_gid)
    except (KeyError, OSError):
        pass
    os.chmod(SOCK, 0o660)
    s.listen(8)
    while True:
        conn, _ = s.accept()
        try:
            conn.settimeout(2)
            data = b""
            while not data.endswith(b"\n") and len(data) < 4096:
                chunk = conn.recv(1024)
                if not chunk:
                    break
                data += chunk
            resp = ctl.command(json.loads(data or b"{}"))
        except Exception as e:
            resp = {"error": str(e)}
        try:
            conn.sendall((json.dumps(resp) + "\n").encode())
        except OSError:
            pass
        conn.close()


def daemon():
    ctl = Controller()
    print("fans:", [f.id for f in ctl.fans], "profile:", ctl.profile, flush=True)

    def bye(*_):
        ctl.restore()
        try:
            os.unlink(ORIG)
        except OSError:
            pass
        sys.exit(0)

    signal.signal(signal.SIGTERM, bye)
    signal.signal(signal.SIGINT, bye)
    atexit.register(ctl.restore)
    threading.Thread(target=serve, args=(ctl,), daemon=True).start()
    while True:
        try:
            ctl.tick()
        except Exception as e:
            print("tick error:", e, file=sys.stderr, flush=True)
            try:
                for f in ctl.fans:  # something is wrong: fail safe, loud fans
                    f.apply(100)
            except Exception:
                pass
        time.sleep(1)


def client(req):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(3)
    s.connect(SOCK)
    s.sendall((json.dumps(req) + "\n").encode())
    data = b""
    while not data.endswith(b"\n"):
        chunk = s.recv(4096)
        if not chunk:
            break
        data += chunk
    return json.loads(data)


def show(st):
    print(f"profile: {st['profile']}   P100: {st['p100']}C   CPU: {st['cpu']}C")
    for f in st["fans"]:
        print(f"  {f['id']:<7} {f['name']:<12} {f['rpm']:>5} rpm  {f['pct']:>3}%  temp {f['temp']}C  {'MANUAL' if f['manual'] else ''}")


def main():
    a = sys.argv[1:]
    if not a or a[0] in ("-h", "--help"):
        print(__doc__)
    elif a[0] == "daemon":
        daemon()
    elif a[0] == "--restore":
        c = Controller.__new__(Controller)
        c.fans, c.controlling = [], False
        Controller.discover(c)
        c.restore()
        try:
            os.unlink(ORIG)
        except OSError:
            pass
    elif a[0] == "status":
        show(client({"cmd": "status"}))
    elif a[0] == "profile" and len(a) == 2:
        show(client({"cmd": "profile", "name": a[1]}))
    elif a[0] == "set" and len(a) == 3:
        show(client({"cmd": "manual", "fan": a[1], "pct": int(a[2])}))
    else:
        print(__doc__)
        sys.exit(2)


if __name__ == "__main__":
    main()
