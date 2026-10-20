#!/usr/bin/env python3
"""Detects what this AI machine has and installs the dashboard feed as a systemd service.

  python3 setup.py               detect, show the plan, ask, install
  python3 setup.py --dry-run     detect and show the plan only; changes nothing
  python3 setup.py --uninstall   remove the services (fans are handed back to the firmware first)

Options: --yes (no questions)  --llama-url URL  --serial PORT|auto|none  --with-fans  --no-firewall

It detects: Linux + systemd, NVIDIA GPU (nvidia-smi), a local llama-server (and whether /metrics is on),
Ollama/vLLM (reported, not supported yet), an ESP32 board on USB, motherboard fan chips, and ufw.
Needs sudo only for the systemd unit (and ufw / fanctl when you opt in).
"""
import argparse, glob, grp, json, os, pwd, re, shutil, socket, subprocess, sys, time, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
FEED = os.path.join(HERE, "p100_feed.py")
FANCTL = os.path.join(HERE, "fanctl.py")
FEED_UNIT, FAN_UNIT = "/etc/systemd/system/esp-feed.service", "/etc/systemd/system/fanctl.service"
NET_PORT, BEACON_PORT = 8765, 8766
LLAMA_PORTS = [8080, 8081, 8000, 8001, 8082, 8083]

G, Y, R, D, B, Z = "\033[32m", "\033[33m", "\033[31m", "\033[2m", "\033[1m", "\033[0m"
if not sys.stdout.isatty():
    G = Y = R = D = B = Z = ""


def ok(m): print(f"  {G}✓{Z} {m}")
def warn(m): print(f"  {Y}!{Z} {m}")
def bad(m): print(f"  {R}✗{Z} {m}")
def info(m): print(f"    {D}{m}{Z}")
def head(m): print(f"\n{B}{m}{Z}")


def sh(cmd, timeout=10):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout).stdout.strip()
    except Exception:
        return ""


def sudo(cmd, **kw):
    return subprocess.run(cmd if os.geteuid() == 0 else ["sudo"] + cmd, **kw)


def ask(question, default=True, assume_yes=False):
    if assume_yes:
        return True
    try:
        tty = open("/dev/tty", "r+")
    except OSError:
        return default  # no terminal at all (CI, ssh -T): take the default
    tty.write(f"{question} [{'Y/n' if default else 'y/N'}] ")
    tty.flush()
    a = tty.readline().strip().lower()
    return default if not a else a.startswith("y")


def http(url, timeout=1.5):
    with urllib.request.urlopen(url, timeout=timeout) as r:
        return r.status, r.read(200000).decode(errors="replace")


# ---------------------------------------------------------------- detection
def detect_gpu():
    if not shutil.which("nvidia-smi"):
        other = sh(["sh", "-c", "lspci 2>/dev/null | grep -i -E 'vga|3d|display'"])
        return None, other
    out = sh(["nvidia-smi", "--query-gpu=name,memory.total,temperature.gpu", "--format=csv,noheader"])
    return [l.strip() for l in out.splitlines() if l.strip()], ""


def detect_llm():
    """Returns (llama-server url or None, notes). Only llama-server's /slots + /metrics are understood by the feed."""
    ports = list(LLAMA_PORTS)
    for line in sh(["pgrep", "-a", "llama-server"]).splitlines():
        m = re.search(r"--port[ =](\d+)", line)
        if m and int(m.group(1)) not in ports:
            ports.insert(0, int(m.group(1)))
    found, notes = None, []
    for port in ports:
        base = f"http://127.0.0.1:{port}"
        try:
            st, body = http(base + "/props")
            if st == 200 and ("model_path" in body or "default_generation_settings" in body):
                found = base
                try: model = os.path.basename(json.loads(body).get("model_path", "")) or "model"
                except ValueError: model = "model"
                notes.append(f"llama-server on port {port}: {model}")
                for ep in ("/slots", "/metrics"):
                    try: http(base + ep)
                    except Exception: notes.append(f"WARN {ep} is off on that server: start llama-server with --metrics and keep --slots for the numbers")
                break
        except Exception:
            pass
    try:
        st, _ = http("http://127.0.0.1:11434/api/tags")
        if st == 200: notes.append("Ollama on port 11434 (not supported yet: it has no /slots or /metrics)")
    except Exception:
        pass
    return found, notes


def detect_board():
    """ESP32-S3 on USB shows up as Espressif (vendor 303a)."""
    espressif = [d for d in glob.glob("/sys/bus/usb/devices/*") if os.path.exists(d + "/idVendor")
                 and open(d + "/idVendor").read().strip() == "303a"]
    ports = sorted(glob.glob("/dev/serial/by-id/*Espressif*")) or sorted(glob.glob("/dev/ttyACM*"))
    return bool(espressif), ports


def detect_fans():
    chips = []
    for d in glob.glob("/sys/class/hwmon/hwmon*"):
        try: name = open(d + "/name").read().strip()
        except OSError: continue
        if re.match(r"(nct67|it87|f71)", name) and glob.glob(d + "/pwm[0-9]"):
            chips.append(name)
    if not chips and shutil.which("modinfo") and sh(["modinfo", "nct6775"]):
        chips.append("nct6775 (driver present, not loaded)")
    return chips


def lan_subnets():
    out = []
    try:
        for ifc in json.loads(sh(["ip", "-j", "-4", "addr"])):
            if ifc["ifname"].startswith(("lo", "docker", "br-", "veth", "tailscale")): continue
            for a in ifc.get("addr_info", []):
                import ipaddress
                net = ipaddress.ip_interface(f"{a['local']}/{a['prefixlen']}").network
                if net.is_private: out.append(str(net))
    except Exception:
        pass
    return out


def ufw_active():
    return "Status: active" in sh(["sudo", "-n", "ufw", "status"]) if shutil.which("ufw") else False


# ---------------------------------------------------------------- install
def user_name():
    return os.environ.get("SUDO_USER") or pwd.getpwuid(os.getuid()).pw_name


def feed_unit(user, llama, serial_arg):
    groups = "dialout" if "dialout" in {g.gr_name for g in grp.getgrall()} else ""
    return f"""[Unit]
Description=Local AI Monitor feed (GPU + LLM stats for the ESP32 dashboard, USB and WiFi)
After=network-online.target fanctl.service
Wants=network-online.target

[Service]
Type=simple
User={user}
{f"SupplementaryGroups={groups}" if groups else ""}
ExecStart={shutil.which("python3") or "/usr/bin/python3"} -I {FEED} {serial_arg} {llama}
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
"""


def write_root(path, text, mode="644"):
    tmp = os.path.join("/tmp", "lam-" + os.path.basename(path))
    with open(tmp, "w") as f: f.write(text)
    if os.path.exists(path) and open(path).read() != text and not os.path.exists(path + ".bak"):
        sudo(["cp", path, path + ".bak"])  # keep the previous unit once
    sudo(["install", "-m", mode, tmp, path], check=True)
    os.unlink(tmp)


def install_fanctl(user):
    src = open(FANCTL).read()
    src = re.sub(r'SOCK_GROUP = ".*?"', f'SOCK_GROUP = "{user}"', src, count=1)
    src = re.sub(r"HUB_HEADERS = \{.*?\}", "HUB_HEADERS = {}", src, count=1)  # that hub is specific to the original rig
    tmp = "/tmp/lam-fanctl.py"
    with open(tmp, "w") as f: f.write(src)
    sudo(["install", "-m", "755", tmp, "/usr/local/bin/fanctl"], check=True)
    sudo(["sh", "-c", "echo nct6775 > /etc/modules-load.d/nct6775.conf"])
    unit = """[Unit]
Description=Temperature-curve fan controller
After=multi-user.target

[Service]
Type=simple
ExecStartPre=-/sbin/modprobe nct6775
ExecStart=/usr/bin/python3 /usr/local/bin/fanctl daemon
ExecStopPost=/usr/bin/python3 /usr/local/bin/fanctl --restore
Restart=always
RestartSec=2
StateDirectory=fanctl
RuntimeDirectory=fanctl

[Install]
WantedBy=multi-user.target
"""
    write_root(FAN_UNIT, unit)
    sudo(["systemctl", "daemon-reload"], check=True)
    sudo(["systemctl", "enable", "--now", "fanctl"], check=True)


def verify():
    time.sleep(4)
    active = sh(["systemctl", "is-active", "esp-feed"]) == "active"
    lines = ""
    try:
        s = socket.create_connection(("127.0.0.1", NET_PORT), 3)
        s.settimeout(3)
        lines = s.recv(2000).decode(errors="replace")
        s.close()
    except OSError:
        pass
    return active, lines


def uninstall():
    head("Uninstalling")
    for unit, path in (("esp-feed", FEED_UNIT), ("fanctl", FAN_UNIT)):
        if os.path.exists(path):
            sudo(["systemctl", "disable", "--now", unit], stderr=subprocess.DEVNULL)
            sudo(["rm", "-f", path])
            ok(f"removed {unit}.service" + (" (fans handed back to the firmware)" if unit == "fanctl" else ""))
    sudo(["systemctl", "daemon-reload"])
    if os.path.exists("/usr/local/bin/fanctl"): sudo(["rm", "-f", "/usr/local/bin/fanctl", "/etc/modules-load.d/nct6775.conf"])
    info("firewall rules (if you added any) stay; list them with: sudo ufw status numbered")


def main():
    ap = argparse.ArgumentParser(description="Install the Local AI Monitor feed on this machine.")
    ap.add_argument("--yes", "-y", action="store_true"), ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--uninstall", action="store_true"), ap.add_argument("--with-fans", action="store_true")
    ap.add_argument("--no-firewall", action="store_true")
    ap.add_argument("--llama-url"), ap.add_argument("--serial", default="auto")
    a = ap.parse_args()

    print(f"{B}Local AI Monitor{Z} — installer")
    if sys.platform != "linux":
        bad("Linux only for now (the feed runs as a systemd service)."); sys.exit(1)
    if a.uninstall:
        uninstall(); return
    if not os.path.isdir("/run/systemd/system"):
        bad("systemd not found. You can still run it by hand:"); info(f"python3 {FEED} auto http://127.0.0.1:8080"); sys.exit(1)
    if not os.path.exists(FEED):
        bad(f"missing {FEED}"); sys.exit(1)

    head("Detecting")
    gpus, other = detect_gpu()
    if gpus:
        for g in gpus: ok(f"GPU: {g}")
        if len(gpus) > 1: info("several GPUs: the dashboard shows the first one")
    else:
        warn("no NVIDIA GPU (nvidia-smi missing): the GPU page will show NO DATA" + (f" — found: {other.splitlines()[0][:70]}" if other else ""))
    llama, notes = detect_llm()
    if a.llama_url: llama = a.llama_url; ok(f"LLM server: {llama} (from --llama-url)")
    for n in notes:
        (warn if n.startswith("WARN") else ok)(n.replace("WARN ", ""))
    if not llama:
        llama = "http://127.0.0.1:8080"
        warn(f"no llama-server found: the LLM page will say OFFLINE until one runs at {llama} (change with --llama-url)")
    usb, ports = detect_board()
    if usb and ports: ok(f"board on USB: {ports[0]}")
    elif usb: warn("an Espressif device is on USB but has no serial port (cable, or permissions)")
    else: info("no board on USB: fine, a board with WiFi connects over the network")
    have_pyserial = False
    try:
        import serial  # noqa: F401
        have_pyserial = True
    except ImportError:
        pass
    if ports and not have_pyserial: warn("pyserial missing: USB will not work until it is installed (WiFi is unaffected)")
    fans = detect_fans()
    fan_note = f"fan controller chip: {', '.join(fans)}" if fans else None
    if fans and not a.with_fans:
        info(fan_note + " — fan control is optional: add --with-fans (tuned for one rig; check the curves in fanctl.py)")
    elif fans:
        ok(fan_note)
    elif a.with_fans:
        warn("--with-fans given but no supported fan chip found: skipping fan control")
        a.with_fans = False
    subnets = lan_subnets()
    if subnets: ok("LAN: " + ", ".join(subnets))
    else: warn("no private LAN address found: WiFi boards will not reach this machine")
    fw = ufw_active() and not a.no_firewall

    head("Plan")
    user = user_name()
    print(f"  • install /etc/systemd/system/esp-feed.service (runs as {user}) from {FEED}")
    print(f"  • serve boards on tcp/{NET_PORT} (+ udp/{BEACON_PORT} discovery), LAN addresses only")
    if fw: print(f"  • ufw is active: allow tcp/{NET_PORT} and udp/{BEACON_PORT} from {', '.join(subnets)}")
    if a.with_fans: print("  • install the fanctl fan controller (root service)")
    if a.dry_run:
        print("\n(dry run: nothing changed)"); return
    if not ask("\nContinue?", True, a.yes):
        print("Cancelled."); return

    head("Installing")
    if ports and not have_pyserial and shutil.which("apt-get"):
        if ask("Install python3-serial with apt for USB support?", True, a.yes):
            sudo(["apt-get", "install", "-y", "python3-serial"], stdout=subprocess.DEVNULL)
    if a.with_fans:
        install_fanctl(user); ok("fanctl installed")
    write_root(FEED_UNIT, feed_unit(user, llama, a.serial))
    sudo(["systemctl", "daemon-reload"], check=True)
    sudo(["systemctl", "enable", "esp-feed"], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    sudo(["systemctl", "restart", "esp-feed"], check=True)
    if fw:
        for sn in subnets:
            sudo(["ufw", "allow", "from", sn, "to", "any", "port", str(NET_PORT), "proto", "tcp"], stdout=subprocess.DEVNULL)
            sudo(["ufw", "allow", "from", sn, "to", "any", "port", str(BEACON_PORT), "proto", "udp"], stdout=subprocess.DEVNULL)
        ok("firewall rules added")
    elif not a.no_firewall and shutil.which("firewall-cmd"):
        warn(f"firewalld found: open tcp/{NET_PORT} and udp/{BEACON_PORT} for your LAN if boards cannot connect")

    active, lines = verify()
    if active and lines.startswith(("P100", "LLM", "FAN", "GPUN")):
        ok("service running and streaming data on the network port")
    elif active:
        warn("service running, but no data seen yet (give it a few seconds): journalctl -u esp-feed -n 20")
    else:
        bad("service did not start: journalctl -u esp-feed -n 30"); sys.exit(1)

    head("Done")
    print("  Flash the board:   firmware/es3c28p-wifi-320x240  (pio run -t upload)")
    print("  Then on its screen: WIFI tab → Choose network → type the password.")
    print("  It finds this machine by itself; no IP to enter.")
    print(f"\n  Logs:       journalctl -u esp-feed -f\n  Uninstall:  python3 {os.path.abspath(__file__)} --uninstall")


if __name__ == "__main__":
    main()
