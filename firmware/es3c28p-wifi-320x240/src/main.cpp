// Local AI Monitor, ES3C28P WiFi variant: 320x240 landscape layout for the LCDWIKI ES3C28P (2.8", ILI9341V).
// Same pages and line protocol as the USB variant (../es3c28p-320x240), plus a WiFi tab: pick a network on the
// touchscreen, type the password, and the board finds the host by itself and streams the same lines over TCP.
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include "fonts.h"

// Pins from the ES3C28P specification, section 4.2. LCD pins are set in platformio.ini.
constexpr int PIN_BL = 45, TP_RST = 18, TP_SDA = 16, TP_SCL = 15;  // touch INT is GPIO17 (unused, polled)
constexpr int W = 320, H = 240;
constexpr int HIST = 292;  // 1px per sample = 292px; graph point every 500 ms -> ~2.4 min

#ifndef TFT_ROTATION
#define TFT_ROTATION 1
#endif

TFT_eSPI tft;
TFT_eSprite fb(&tft);  // full-frame off-screen buffer (PSRAM): draw everything, push once -> no flicker

// ---------- data ----------
struct Gpu {
  int temp = 0, util = 0, memUsedMiB = 0, memTotalMiB = 16384, smClk = 0, memClk = 0;
  float power = 0, powerLimit = 250;
} gpu;

struct Llm {
  int state = 0;  // 0 offline, 1 idle, 2 prompt, 3 generating
  int ctxUsed = 0, ctxTotal = 0;
  float gen = 0, prompt = 0, avgGen = 0, avgPrompt = 0;
  long predTotal = 0, promptTotal = 0, uptime = 0;
  char quant[16] = "", model[40] = "llama-server";
} llm;

struct FanInfo { char name[24]; int rpm, pct, temp; bool manual; };
struct Fans {
  char profile[12] = "none";  // idle | normal | performance | auto | manual | none (no daemon)
  int n = 0;
  FanInfo f[4];
} fans;
uint32_t lastFanRx = 0, lastUserFan = 0;
bool haveFan = false;

uint8_t histUtil[HIST], histTemp[HIST];
int histLen = 0;
uint32_t lastGpuRx = 0, lastLlmRx = 0;
bool haveGpu = false, haveLlm = false;

// ---------- theme ----------
uint16_t BG, CARD, EDGE, GRIDC, TEXT, DIM, GREEN, AMBER, RED, CYAN, ORANGE, VIOLET, ONACC;

uint16_t mix(uint16_t a, uint16_t b, uint8_t t) {  // t=0 -> a, 255 -> b
  uint16_t r = (((a >> 11) & 31) * (255 - t) + ((b >> 11) & 31) * t) / 255;
  uint16_t g = (((a >> 5) & 63) * (255 - t) + ((b >> 5) & 63) * t) / 255;
  uint16_t bl = ((a & 31) * (255 - t) + (b & 31) * t) / 255;
  return (r << 11) | (g << 5) | bl;
}
uint16_t level(int v, int warn, int crit) { return v >= crit ? RED : v >= warn ? AMBER : GREEN; }

// ---------- fonts / text ----------
enum { F_LABEL, F_BODY, F_BODYB, F_TITLE, F_VAL, F_BIG, F_COUNT };
const uint8_t *FONTS[F_COUNT] = {font_label, font_body, font_bodyb, font_title, font_val, font_big};
int curFont = -1;

void useFont(int f) {
  if (f == curFont) return;
  if (curFont >= 0) fb.unloadFont();
  fb.loadFont(FONTS[f]);
  curFont = f;
}
void text(const char *s, int x, int y, int font, uint16_t col, uint8_t datum = TL_DATUM) {
  useFont(font);
  fb.setTextDatum(datum);
  fb.setTextColor(col);
  fb.drawString(s, x, y);
}
int twidth(const char *s, int font) {
  useFont(font);
  return fb.textWidth(s);
}
void fmtInt(char *out, size_t n, long v) {  // 1234567 -> "1,234,567"; 123456789 -> "123.5M" (small screen)
  char t[24];
  if (v >= 100000000) {
    snprintf(out, n, "%.1fM", v / 1e6);
    return;
  }
  snprintf(t, sizeof t, "%ld", v);
  int len = strlen(t), o = 0;
  for (int i = 0; i < len && o < (int)n - 1; i++) {
    if (i && (len - i) % 3 == 0) out[o++] = ',';
    out[o++] = t[i];
  }
  out[o] = 0;
}

// ---------- widgets ----------
void card(int x, int y, int w, int h, uint16_t border = 0) {
  uint16_t e = border ? border : EDGE;
  fb.fillSmoothRoundRect(x, y, w, h, 10, e, BG);
  fb.fillSmoothRoundRect(x + 1, y + 1, w - 2, h - 2, 9, CARD, e);
}
int pillWidth(const char *label) { return twidth(label, F_LABEL) + 26; }
void pill(int xr, int y, const char *label, uint16_t fg, uint16_t bgc) {
  int w = pillWidth(label);
  int x = xr - w;
  fb.fillSmoothRoundRect(x, y, w, 22, 11, bgc, BG);
  fb.fillSmoothCircle(x + 12, y + 11, 3, fg, bgc);
  text(label, x + 21, y + 11, F_LABEL, fg, ML_DATUM);
}
void chip(int x, int y, const char *label, uint16_t fg, uint16_t bgc) {
  int w = twidth(label, F_LABEL) + 16;
  fb.fillSmoothRoundRect(x, y, w, 20, 10, bgc, BG);
  text(label, x + w / 2, y + 10, F_LABEL, fg, MC_DATUM);
}
void bar(int x, int y, int w, int h, int pct, uint16_t col, uint16_t track = 0) {
  fb.fillSmoothRoundRect(x, y, w, h, h / 2, track ? track : GRIDC, CARD);
  int fw = constrain(pct, 0, 100) * w / 100;
  if (fw >= h) fb.fillSmoothRoundRect(x, y, fw, h, h / 2, col, track ? track : GRIDC);
  else if (fw > 0) fb.fillSmoothRoundRect(x, y, h, h, h / 2, col, track ? track : GRIDC);
}
void gauge(int cx, int cy, int pct, uint16_t col, const char *val, const char *unit) {
  constexpr int R = 30, IR = 24;
  fb.drawSmoothArc(cx, cy, R, IR, 30, 330, GRIDC, CARD, true);
  int p = constrain(pct, 0, 100);
  if (p > 0) {
    uint32_t end = 30 + (300 * p) / 100;
    if (end < 36) end = 36;
    fb.drawSmoothArc(cx, cy, R, IR, 30, end, col, CARD, true);
  }
  text(val, cx, cy - 3, F_VAL, TEXT, MC_DATUM);
  text(unit, cx, cy + 15, F_LABEL, DIM, MC_DATUM);
}

// ---------- chrome ----------
int page = 0;  // 0 = GPU, 1 = LLM, 2 = FANS, 3 = WIFI
constexpr int NTABS = 4;
constexpr int TABS_Y = 210, TAB_W = 70, TAB_H = 26;
int tabX(int i) { return (W - (NTABS * TAB_W + (NTABS - 1) * 8)) / 2 + i * (TAB_W + 8); }

void tabs() {
  const char *names[NTABS] = {"GPU", "LLM", "FANS", "WIFI"};
  for (int i = 0; i < NTABS; i++) {
    bool on = page == i;
    fb.fillSmoothRoundRect(tabX(i), TABS_Y, TAB_W, TAB_H, 13, on ? CYAN : CARD, BG);
    text(names[i], tabX(i) + TAB_W / 2, TABS_Y + TAB_H / 2, F_BODYB, on ? ONACC : DIM, MC_DATUM);
  }
}

// ---------- pages ----------
void pageGpu() {
  bool live = haveGpu && millis() - lastGpuRx < 3500;
  text("Tesla P100", 10, 16, F_TITLE, TEXT, ML_DATUM);
  text("16 GB  \xC2\xB7  PCIe", 10 + twidth("Tesla P100", F_TITLE) + 8, 19, F_BODY, DIM, ML_DATUM);
  if (live) pill(W - 8, 5, "LIVE", GREEN, mix(BG, GREEN, 40));
  else pill(W - 8, 5, "NO DATA", RED, mix(BG, RED, 40));

  struct GaugeSpec { const char *label; char val[12]; const char *unit; int pct; uint16_t col; };
  GaugeSpec g[4];
  auto set = [&](int i, const char *label, const char *unit, int pct, uint16_t col) {
    g[i].label = label; g[i].unit = unit; g[i].pct = pct; g[i].col = col;
  };
  int mp = gpu.memTotalMiB ? gpu.memUsedMiB * 100 / gpu.memTotalMiB : 0;
  int pp = gpu.powerLimit > 0 ? (int)(gpu.power * 100 / gpu.powerLimit) : 0;
  set(0, "UTIL", "%", gpu.util, level(gpu.util, 70, 95));
  snprintf(g[0].val, 12, "%d", gpu.util);
  set(1, "TEMP", "\xC2\xB0" "C", gpu.temp, level(gpu.temp, 65, 80));
  snprintf(g[1].val, 12, "%d", gpu.temp);
  set(2, "VRAM", "GB", mp, level(mp, 80, 95));
  snprintf(g[2].val, 12, "%.1f", gpu.memUsedMiB / 1024.0f);
  set(3, "POWER", "W", pp, level(pp, 70, 90));
  snprintf(g[3].val, 12, "%d", (int)(gpu.power + 0.5f));

  for (int i = 0; i < 4; i++) {
    int x = 5 + i * 79, y = 32;
    card(x, y, 74, 86);
    gauge(x + 37, y + 38, g[i].pct, g[i].col, g[i].val, g[i].unit);
    text(g[i].label, x + 37, y + 76, F_LABEL, DIM, MC_DATUM);
  }

  // history card
  const int cx = 5, cy = 122, cw = 310, ch = 82;
  card(cx, cy, cw, ch);
  fb.fillSmoothCircle(cx + 14, cy + 14, 4, CYAN, CARD);
  text("GPU util", cx + 23, cy + 14, F_LABEL, DIM, ML_DATUM);
  fb.fillSmoothCircle(cx + 82, cy + 14, 4, ORANGE, CARD);
  text("Temp \xC2\xB0""C", cx + 91, cy + 14, F_LABEL, DIM, ML_DATUM);
  char clk[40];
  snprintf(clk, sizeof clk, "SM %d \xC2\xB7 MEM %d MHz", gpu.smClk, gpu.memClk);
  text(clk, cx + cw - 10, cy + 14, F_LABEL, DIM, MR_DATUM);

  const int px = cx + 9, pt = cy + 28, pb = cy + ch - 8, ph = pb - pt;
  for (int p = 0; p <= 100; p += 50) fb.drawFastHLine(px, pb - p * ph / 100, HIST, GRIDC);
  if (histLen >= 2) {
    uint16_t fill = mix(CARD, CYAN, 55);
    int ox = px + HIST - histLen;
    for (int i = 0; i < histLen; i++) {
      int y = pb - histUtil[i] * ph / 100;
      fb.fillRect(ox + i, y, 1, pb - y, fill);
    }
    for (int i = 1; i < histLen; i++) {
      fb.drawWideLine(ox + i - 1, pb - histTemp[i - 1] * ph / 100, ox + i, pb - histTemp[i] * ph / 100, 2.0f, ORANGE, CARD);
      fb.drawWideLine(ox + i - 1, pb - histUtil[i - 1] * ph / 100, ox + i, pb - histUtil[i] * ph / 100, 2.0f, CYAN, CARD);
    }
  }
}

void bigStat(int x, int y, int w, const char *label, float v, const char *sub, uint16_t col, bool active) {
  card(x, y, w, 88, active ? mix(EDGE, col, 160) : 0);
  text(label, x + 12, y + 14, F_LABEL, DIM, ML_DATUM);
  char s[16];
  snprintf(s, sizeof s, v >= 100 ? "%.0f" : "%.1f", v);
  uint16_t c = active ? col : (v > 0 ? TEXT : DIM);
  text(s, x + 10, y + 24, F_BIG, c);
  text("tok/s", x + 10 + twidth(s, F_BIG) + 6, y + 60, F_BODYB, DIM, BL_DATUM);
  text(sub, x + 12, y + 76, F_BODY, DIM, ML_DATUM);
}

void pageLlm() {
  bool live = haveLlm && millis() - lastLlmRx < 3500 && llm.state > 0;
  const char *state = !live ? "OFFLINE" : llm.state == 3 ? "GENERATING" : llm.state == 2 ? "READING PROMPT" : "IDLE";
  uint16_t sc = !live ? RED : llm.state == 3 ? GREEN : llm.state == 2 ? AMBER : DIM;

  // title: shorten the model name so it never runs into the state pill
  int avail = W - 8 - pillWidth(state) - 10 - 8;
  bool showQuant = llm.quant[0] && avail - (twidth(llm.quant, F_LABEL) + 16) >= 90;
  if (showQuant) avail -= twidth(llm.quant, F_LABEL) + 16 + 8;
  char title[40];
  strlcpy(title, llm.model, sizeof title);
  while (strlen(title) > 1 && twidth(title, F_TITLE) > avail) title[strlen(title) - 1] = 0;
  text(title, 10, 16, F_TITLE, TEXT, ML_DATUM);
  if (showQuant) chip(10 + twidth(title, F_TITLE) + 8, 6, llm.quant, VIOLET, mix(BG, VIOLET, 50));
  pill(W - 8, 5, state, sc, mix(BG, sc, 40));

  char sub[40];
  snprintf(sub, sizeof sub, "avg %.1f tok/s", llm.avgGen);
  bigStat(5, 32, 152, "GENERATION", llm.gen, sub, GREEN, live && llm.state == 3);
  snprintf(sub, sizeof sub, "avg %.1f tok/s", llm.avgPrompt);
  bigStat(163, 32, 152, "PROMPT PROCESSING", llm.prompt, sub, AMBER, live && llm.state == 2);

  // context
  card(5, 124, 310, 38);
  text("CONTEXT", 19, 136, F_LABEL, DIM, ML_DATUM);
  int pct = llm.ctxTotal ? (int)(100L * llm.ctxUsed / llm.ctxTotal) : 0;
  char a[16], b[16], line[64];
  fmtInt(a, sizeof a, llm.ctxUsed);
  fmtInt(b, sizeof b, llm.ctxTotal);
  snprintf(line, sizeof line, "%s / %s  \xC2\xB7  %d%%", a, b, pct);
  text(line, 301, 136, F_BODYB, TEXT, MR_DATUM);
  bar(19, 148, 282, 8, pct, level(pct, 70, 90), 0);

  // totals
  char v[24], v1[24], v2[24];
  const char *lab[3] = {"GENERATED", "PROMPTED", "UPTIME"};
  fmtInt(v, sizeof v, llm.predTotal);
  fmtInt(v1, sizeof v1, llm.promptTotal);
  long u = llm.uptime;
  if (u >= 86400) snprintf(v2, sizeof v2, "%ldd %ldh", u / 86400, (u % 86400) / 3600);
  else if (u >= 3600) snprintf(v2, sizeof v2, "%ldh %ldm", u / 3600, (u % 3600) / 60);
  else snprintf(v2, sizeof v2, "%ldm %lds", u / 60, u % 60);
  const char *vals[3] = {v, v1, v2};
  uint16_t cols[3] = {GREEN, AMBER, CYAN};
  for (int i = 0; i < 3; i++) {
    int x = 5 + i * 105;
    card(x, 166, 100, 38);
    fb.fillSmoothRoundRect(x + 10, 174, 4, 4, 2, cols[i], CARD);
    text(lab[i], x + 18, 176, F_LABEL, DIM, ML_DATUM);
    text(vals[i], x + 10, 193, F_BODYB, TEXT, ML_DATUM);
  }
}


// ---- fans page ----
struct Rect { int x, y, w, h; };
const char *PROF[4] = {"idle", "normal", "performance", "auto"};
const char *PROF_LABEL[4] = {"IDLE", "NORMAL", "PERF", "AUTO"};
uint16_t profColor(int i) { return i == 0 ? CYAN : i == 1 ? GREEN : i == 2 ? ORANGE : VIOLET; }
Rect segRect(int i) { return {5 + i * 79, 32, 74, 28}; }
Rect fanCard(int i, int n) {
  int cols = n <= 1 ? 1 : 2, rows = (n + cols - 1) / cols;
  int cw = cols == 1 ? 310 : 152, ch = (138 - (rows - 1) * 6) / rows;
  return {5 + (i % cols) * (cw + 6), 66 + (i / cols) * (ch + 6), cw, ch};
}
int sliderX0(const Rect &c) { return c.x + 16; }
int sliderX1(const Rect &c) { return c.x + c.w - 16; }
int sliderY(const Rect &c) { return c.y + c.h - (c.h > 100 ? 22 : 13); }
bool inRect(int x, int y, const Rect &r, int pad = 0) {
  return x >= r.x - pad && x < r.x + r.w + pad && y >= r.y - pad && y < r.y + r.h + pad;
}
Rect sliderHit(const Rect &c) { return {c.x + 2, c.y + c.h - (c.h > 100 ? 40 : 30), c.w - 4, c.h > 100 ? 40 : 30}; }

void pageFans() {
  bool live = haveFan && millis() - lastFanRx < 3500 && strcmp(fans.profile, "none") != 0;
  text("Fans", 10, 16, F_TITLE, TEXT, ML_DATUM);
  if (!live) pill(W - 8, 5, "NO CONTROLLER", RED, mix(BG, RED, 40));
  else if (!strcmp(fans.profile, "manual")) pill(W - 8, 5, "MANUAL", AMBER, mix(BG, AMBER, 40));
  else pill(W - 8, 5, "CURVES", GREEN, mix(BG, GREEN, 40));

  int active = -1;
  for (int i = 0; i < 4; i++) if (live && !strcmp(fans.profile, PROF[i])) active = i;
  for (int i = 0; i < 4; i++) {
    Rect r = segRect(i);
    bool on = i == active;
    fb.fillSmoothRoundRect(r.x, r.y, r.w, r.h, 11, on ? profColor(i) : CARD, BG);
    if (!on) fb.drawSmoothRoundRect(r.x, r.y, 11, 10, r.w, r.h, EDGE, CARD);
    text(PROF_LABEL[i], r.x + r.w / 2, r.y + r.h / 2, F_LABEL, on ? ONACC : DIM, MC_DATUM);
  }

  if (!live || fans.n == 0) {
    card(5, 66, 310, 138);
    text(live ? "No controllable fans found" : "Fan controller not running", W / 2, 124, F_BODYB, DIM, MC_DATUM);
    text(live ? "" : "start the fanctl service on the PC", W / 2, 146, F_BODY, DIM, MC_DATUM);
    return;
  }
  for (int i = 0; i < fans.n; i++) {
    Rect c = fanCard(i, fans.n);
    const FanInfo &f = fans.f[i];
    uint16_t acc = f.manual ? AMBER : (active >= 0 ? profColor(active) : CYAN);
    bool tall = c.h > 100;
    card(c.x, c.y, c.w, c.h);
    text(f.name, c.x + 10, c.y + (tall ? 15 : 11), F_BODYB, TEXT, ML_DATUM);
    char t[24];
    snprintf(t, sizeof t, "%d\xC2\xB0""C", f.temp);
    text(t, c.x + c.w - 10, c.y + (tall ? 15 : 11), F_LABEL, DIM, MR_DATUM);
    int ry = c.y + (tall ? 54 : 31);
    char r[16];
    if (f.rpm < 0) {
      text("PWM only, no tach", c.x + 10, ry, F_BODY, DIM, ML_DATUM);
    } else {
      snprintf(r, sizeof r, "%d", f.rpm);
      text(r, c.x + 10, ry, F_VAL, TEXT, ML_DATUM);
      text("rpm", c.x + 10 + twidth(r, F_VAL) + 4, ry + 4, F_LABEL, DIM, ML_DATUM);
    }
    snprintf(r, sizeof r, "%d%%", f.pct);
    text(r, c.x + c.w - 10, ry, c.w > 200 ? F_VAL : F_BODYB, acc, MR_DATUM);
    // slider
    int x0 = sliderX0(c), x1 = sliderX1(c), sy = sliderY(c);
    int kx = x0 + constrain(f.pct, 0, 100) * (x1 - x0) / 100;
    fb.fillSmoothRoundRect(x0, sy - 3, x1 - x0, 6, 3, GRIDC, CARD);
    if (kx > x0) fb.fillSmoothRoundRect(x0, sy - 3, kx - x0, 6, 3, acc, GRIDC);
    fb.fillSmoothCircle(kx, sy, 9, TEXT, CARD);
    fb.fillSmoothCircle(kx, sy, 4, acc, TEXT);
  }
}

bool dirty = true;
uint32_t lastDraw = 0;

// ================= WiFi: connection, host discovery, menu =================
void onLine(const char *line);

constexpr uint16_t BEACON_PORT = 8766;  // the host broadcasts "ESPDASH,<tcp port>,<hostname>" here every 2 s
Preferences prefs;
WiFiUDP beacon;
WiFiClient hostLink;
bool beaconOpen = false;
IPAddress hostIp;
uint16_t hostPort = 0;
char hostName[24] = "";
uint32_t lastHostTry = 0, lastLinkRx = 0;

char savedSsid[33] = "", savedPass[65] = "";
bool joining = false;
uint32_t joinStart = 0;
char joinErr[48] = "";

enum { V_STATUS, V_LIST, V_KEY };
int wview = V_STATUS;
struct NetEntry { char ssid[33]; int rssi; bool open; };
NetEntry nets[24];
int nNets = 0, listTop = 0;
bool scanning = false;
char kbSsid[33] = "", kbPass[65] = "";
int kbMode = 0;  // 0 abc, 1 ABC (one shot), 2 symbols 1, 3 symbols 2
bool kbShow = false, kbNeedSsid = false;
int kbFlash = -1;
uint32_t kbFlashAt = 0;

struct LineBuf {
  char b[320];
  int n = 0;
  void feed(char c) {
    if (c == '\n') { b[n] = 0; if (n) onLine(b); n = 0; }
    else if (c != '\r' && n < (int)sizeof b - 1) b[n++] = c;
  }
};
LineBuf linkBuf;

void startJoin(const char *ssid, const char *pass) {
  hostLink.stop();
  hostIp = IPAddress();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(ssid, pass);
  joining = true;
  joinStart = millis();
  joinErr[0] = 0;
  dirty = true;
}

void startScan() {
  WiFi.mode(WIFI_STA);
  WiFi.scanDelete();
  WiFi.scanNetworks(true);
  scanning = true;
  nNets = 0;
  listTop = 0;
  dirty = true;
}

void collectScan(int r) {
  nNets = 0;
  for (int i = 0; i < r && nNets < 24; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;  // hidden network
    int rssi = WiFi.RSSI(i);
    bool dup = false;
    for (int k = 0; k < nNets; k++)
      if (!strcmp(nets[k].ssid, s.c_str())) { dup = true; if (rssi > nets[k].rssi) nets[k].rssi = rssi; }
    if (dup) continue;
    strlcpy(nets[nNets].ssid, s.c_str(), sizeof nets[0].ssid);
    nets[nNets].rssi = rssi;
    nets[nNets].open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    nNets++;
  }
  for (int i = 1; i < nNets; i++) {  // strongest first
    NetEntry t = nets[i];
    int k = i - 1;
    while (k >= 0 && nets[k].rssi < t.rssi) { nets[k + 1] = nets[k]; k--; }
    nets[k + 1] = t;
  }
  WiFi.scanDelete();
  scanning = false;
  dirty = true;
}

void wifiBegin() {
  prefs.begin("wifi", false);
  prefs.getString("ssid", savedSsid, sizeof savedSsid);
  prefs.getString("pass", savedPass, sizeof savedPass);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (savedSsid[0]) WiFi.begin(savedSsid, savedPass);
}

void forgetNetwork() {
  prefs.clear();
  savedSsid[0] = savedPass[0] = 0;
  hostLink.stop();
  WiFi.disconnect(true);
  joining = false;
  dirty = true;
}

// Called from loop(): joins, host discovery over the UDP beacon, and the TCP data link.
void netLoop() {
  static wl_status_t last = WL_IDLE_STATUS;
  wl_status_t st = WiFi.status();
  if (st != last) { last = st; dirty = true; }

  if (scanning) {
    int r = WiFi.scanComplete();
    if (r >= 0) collectScan(r);
    else if (r == WIFI_SCAN_FAILED) { scanning = false; dirty = true; }
  }

  if (joining) {
    if (st == WL_CONNECTED) {
      strlcpy(savedSsid, kbSsid, sizeof savedSsid);
      strlcpy(savedPass, kbPass, sizeof savedPass);
      prefs.putString("ssid", savedSsid);
      prefs.putString("pass", savedPass);  // saved only once it worked, so a typo is never stored
      joining = false;
      wview = V_STATUS;
      kbPass[0] = 0;
      dirty = true;
    } else if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL || millis() - joinStart > 20000) {
      WiFi.disconnect();
      joining = false;
      strlcpy(joinErr, st == WL_NO_SSID_AVAIL ? "Network not found" : "Could not join. Wrong password?", sizeof joinErr);
      if (savedSsid[0] && strcmp(savedSsid, kbSsid)) WiFi.begin(savedSsid, savedPass);  // go back to the working network
      dirty = true;
    }
  }

  if (st != WL_CONNECTED) {
    if (beaconOpen) { beacon.stop(); beaconOpen = false; }
    hostLink.stop();
    return;
  }
  if (!beaconOpen) { beacon.begin(BEACON_PORT); beaconOpen = true; }

  // host discovery
  int len = beacon.parsePacket();
  if (len > 0) {
    char pk[64];
    int n = beacon.read(pk, sizeof pk - 1);
    pk[n > 0 ? n : 0] = 0;
    int port = 0;
    char name[24] = "";
    if (sscanf(pk, "ESPDASH,%d,%23[^\n]", &port, name) >= 1 && port > 0) {
      hostIp = beacon.remoteIP();
      hostPort = port;
      strlcpy(hostName, name, sizeof hostName);
    }
  }

  if (!hostLink.connected()) {
    if (hostIp != IPAddress() && millis() - lastHostTry > 3000) {
      lastHostTry = millis();
      if (hostLink.connect(hostIp, hostPort, 800)) {
        hostLink.setNoDelay(true);
        lastLinkRx = millis();
        linkBuf.n = 0;
        dirty = true;
      }
    }
    return;
  }
  while (hostLink.available()) {
    linkBuf.feed(hostLink.read());
    lastLinkRx = millis();
  }
  if (millis() - lastLinkRx > 8000) { hostLink.stop(); dirty = true; }  // the host sends 4x/s; silence means it is gone
}

// ---------- drawing helpers ----------
void sigBars(int x, int y, int rssi, uint16_t col, uint16_t back) {  // 4 bars, bottom edge at y
  int lit = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : rssi > -85 ? 1 : 0;
  for (int i = 0; i < 4; i++) {
    int h = 4 + i * 3;
    fb.fillRect(x + i * 5, y - h, 3, h, i < lit ? col : mix(back, DIM, 90));
  }
}

void button(const Rect &r, const char *label, uint16_t fill, uint16_t fg) {
  fb.fillSmoothRoundRect(r.x, r.y, r.w, r.h, 9, fill, BG);
  text(label, r.x + r.w / 2, r.y + r.h / 2, F_BODYB, fg, MC_DATUM);
}
void fitText(char *out, size_t n, const char *s, int font, int maxw) {
  strlcpy(out, s, n);
  while (strlen(out) > 1 && twidth(out, font) > maxw) out[strlen(out) - 1] = 0;
}

const Rect B_CHOOSE = {5, 132, 152, 34}, B_FORGET = {163, 132, 152, 34};
const Rect B_BACK = {5, 184, 74, 22}, B_UP = {83, 184, 74, 22}, B_DOWN = {163, 184, 74, 22}, B_RESCAN = {241, 184, 74, 22};
constexpr int LIST_ROWS = 5;
Rect listRow(int i) { return {5, 32 + i * 30, 310, 28}; }

void pageWifi() {
  bool up = WiFi.status() == WL_CONNECTED;
  bool trying = !up && (joining || savedSsid[0]);
  text("WiFi", 10, 16, F_TITLE, TEXT, ML_DATUM);
  if (up) pill(W - 8, 5, "ONLINE", GREEN, mix(BG, GREEN, 40));
  else if (trying) pill(W - 8, 5, "CONNECTING", AMBER, mix(BG, AMBER, 40));
  else pill(W - 8, 5, "OFFLINE", RED, mix(BG, RED, 40));

  if (wview == V_LIST) {
    if (scanning) {
      card(5, 32, 310, 148);
      text("Scanning for networks...", W / 2, 106, F_BODYB, DIM, MC_DATUM);
    } else if (nNets == 0) {
      card(5, 32, 310, 148);
      text("No networks found", W / 2, 106, F_BODYB, DIM, MC_DATUM);
    }
    for (int i = 0; i < LIST_ROWS && !scanning && listTop + i < nNets; i++) {
      const NetEntry &e = nets[listTop + i];
      Rect r = listRow(i);
      bool cur = up && !strcmp(WiFi.SSID().c_str(), e.ssid);
      card(r.x, r.y, r.w, r.h, cur ? mix(EDGE, GREEN, 160) : 0);
      char nm[40];
      fitText(nm, sizeof nm, e.ssid, F_BODYB, e.open ? 190 : 230);
      text(nm, r.x + 12, r.y + r.h / 2, F_BODYB, TEXT, ML_DATUM);
      if (e.open) text("open", r.x + r.w - 60, r.y + r.h / 2, F_LABEL, AMBER, MR_DATUM);
      sigBars(r.x + r.w - 32, r.y + r.h / 2 + 7, e.rssi, cur ? GREEN : CYAN, CARD);
    }
    bool canUp = listTop > 0, canDown = listTop + LIST_ROWS < nNets;
    button(B_BACK, "Back", CARD, TEXT);
    button(B_UP, "Up", canUp ? CARD : BG, canUp ? TEXT : GRIDC);
    button(B_DOWN, "Down", canDown ? CARD : BG, canDown ? TEXT : GRIDC);
    button(B_RESCAN, "Rescan", CARD, CYAN);
    return;
  }

  card(5, 32, 310, 94);
  char l[96], nm[40];
  if (up) {
    fitText(nm, sizeof nm, WiFi.SSID().c_str(), F_TITLE, 230);
    text(nm, 17, 50, F_TITLE, TEXT, ML_DATUM);
    sigBars(W - 38, 56, WiFi.RSSI(), GREEN, CARD);
    snprintf(l, sizeof l, "%s  \xC2\xB7  %d dBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    text(l, 17, 74, F_BODY, DIM, ML_DATUM);
    if (hostLink.connected()) {
      snprintf(l, sizeof l, "Monitor: %s  \xC2\xB7  %s", hostName[0] ? hostName : "host", hostIp.toString().c_str());
      text(l, 17, 98, F_BODYB, GREEN, ML_DATUM);
    } else {
      text("Looking for the monitor host...", 17, 98, F_BODYB, AMBER, ML_DATUM);
    }
    text("Also works over USB at any time", 17, 116, F_LABEL, DIM, ML_DATUM);
  } else if (trying) {
    fitText(nm, sizeof nm, joining ? kbSsid : savedSsid, F_TITLE, 270);
    text(nm, 17, 50, F_TITLE, TEXT, ML_DATUM);
    text("Connecting...", 17, 74, F_BODY, AMBER, ML_DATUM);
  } else {
    text("Not connected", 17, 50, F_TITLE, TEXT, ML_DATUM);
    text("Tap Choose network to join your WiFi.", 17, 74, F_BODY, DIM, ML_DATUM);
  }
  button(B_CHOOSE, up || trying ? "Change network" : "Choose network", CYAN, ONACC);
  bool canForget = savedSsid[0];
  button(B_FORGET, "Forget", canForget ? CARD : BG, canForget ? RED : GRIDC);
  if (joinErr[0]) text(joinErr, W / 2, 182, F_BODY, RED, MC_DATUM);
}

// ---------- on-screen keyboard ----------
enum { K_CHAR, K_SHIFT, K_BKSP, K_MODE, K_SPACE, K_SHOW, K_JOIN, K_SYM };
struct Key { int16_t x, y, w, h; uint8_t act; char c; const char *label; };
const char *KB_ROWS[4][3] = {
  {"qwertyuiop", "asdfghjkl", "zxcvbnm"},
  {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"},
  {"1234567890", "-/:;()$&@\"", ".,?!'_%"},
  {"[]{}#%^*+=", "_\\|~<>`';\"", ".,?!&@-"},
};
constexpr int KB_Y = 60, KB_PITCH = 43, KB_H = 40;

int layoutKeys(Key *k) {
  int n = 0;
  bool alpha = kbMode < 2;
  for (int r = 0; r < 3; r++) {
    const char *row = KB_ROWS[kbMode][r];
    int cnt = strlen(row);
    int x0 = r == 2 ? 50 : (W - cnt * 32) / 2 + 1;
    for (int i = 0; i < cnt; i++) k[n++] = {(int16_t)(x0 + i * 32), (int16_t)(KB_Y + r * KB_PITCH), 30, KB_H, K_CHAR, row[i], nullptr};
  }
  int y3 = KB_Y + 2 * KB_PITCH, y4 = KB_Y + 3 * KB_PITCH;
  k[n++] = {2, (int16_t)y3, 44, KB_H, (uint8_t)(alpha ? K_SHIFT : K_SYM), 0, alpha ? "Aa" : (kbMode == 2 ? "#+=" : "123")};
  k[n++] = {276, (int16_t)y3, 42, KB_H, K_BKSP, 0, "del"};
  k[n++] = {2, (int16_t)y4, 46, KB_H, K_MODE, 0, alpha ? "123" : "abc"};
  k[n++] = {52, (int16_t)y4, 150, KB_H, K_SPACE, ' ', "space"};
  k[n++] = {206, (int16_t)y4, 46, KB_H, K_SHOW, 0, kbShow ? "hide" : "show"};
  k[n++] = {256, (int16_t)y4, 62, KB_H, K_JOIN, 0, "Join"};
  return n;
}
const Rect KB_CANCEL = {284, 4, 32, 22};

void wifiKeyboard() {
  char l[96], nm[40];
  fitText(nm, sizeof nm, kbSsid, F_BODYB, 200);
  if (joining) snprintf(l, sizeof l, "Joining %s...", nm);
  else if (kbNeedSsid) snprintf(l, sizeof l, "Network name");
  else snprintf(l, sizeof l, "Password for %s", nm);
  text(l, 8, 14, F_BODYB, joining ? AMBER : TEXT, ML_DATUM);
  fb.fillSmoothRoundRect(KB_CANCEL.x, KB_CANCEL.y, KB_CANCEL.w, KB_CANCEL.h, 8, CARD, BG);
  text("X", KB_CANCEL.x + KB_CANCEL.w / 2, KB_CANCEL.y + KB_CANCEL.h / 2, F_BODYB, DIM, MC_DATUM);

  card(4, 28, 312, 28, joinErr[0] ? RED : 0);
  char shown[72];
  const char *src = kbNeedSsid ? kbSsid : kbPass;
  int len = strlen(src);
  if (kbShow || kbNeedSsid) strlcpy(shown, src, sizeof shown);
  else { memset(shown, '*', len); shown[len] = 0; }
  const char *p = shown;
  while (*p && twidth(p, F_BODYB) > 288) p++;  // keep the end of long entries visible
  text(p, 12, 42, F_BODYB, TEXT, ML_DATUM);
  if (joinErr[0]) text(joinErr, 300, 42, F_LABEL, RED, MR_DATUM);

  Key k[40];
  int n = layoutKeys(k);
  for (int i = 0; i < n; i++) {
    bool flash = i == kbFlash;
    bool special = k[i].act != K_CHAR && k[i].act != K_SPACE;
    uint16_t fill = flash ? CYAN : k[i].act == K_JOIN ? GREEN : special ? EDGE : CARD;
    fb.fillSmoothRoundRect(k[i].x, k[i].y, k[i].w, k[i].h, 6, fill, BG);
    uint16_t fg = flash || k[i].act == K_JOIN ? ONACC : k[i].act == K_SPACE ? DIM : TEXT;
    if (k[i].act == K_SHIFT && kbMode == 1) fg = CYAN;
    if (k[i].label) text(k[i].label, k[i].x + k[i].w / 2, k[i].y + k[i].h / 2, F_BODYB, fg, MC_DATUM);
    else {
      char c[2] = {k[i].c, 0};
      text(c, k[i].x + k[i].w / 2, k[i].y + k[i].h / 2, F_TITLE, fg, MC_DATUM);
    }
  }
}

void kbCancel() {
  joining = false;
  joinErr[0] = 0;
  kbPass[0] = 0;
  if (savedSsid[0] && WiFi.status() != WL_CONNECTED) WiFi.begin(savedSsid, savedPass);
  wview = V_LIST;
  dirty = true;
}

void kbJoin() {
  if (kbNeedSsid) { kbNeedSsid = false; kbPass[0] = 0; return; }
  startJoin(kbSsid, kbPass);
}

void kbPress(int x, int y) {  // acts on touch-down for a responsive feel
  if (inRect(x, y, KB_CANCEL, 8)) { kbCancel(); return; }
  if (joining) return;
  Key k[40];
  int n = layoutKeys(k);
  for (int i = 0; i < n; i++) {
    if (x < k[i].x - 1 || x >= k[i].x + k[i].w + 1 || y < k[i].y - 1 || y >= k[i].y + k[i].h + 1) continue;
    kbFlash = i;
    kbFlashAt = millis();
    char *buf = kbNeedSsid ? kbSsid : kbPass;
    size_t cap = kbNeedSsid ? sizeof kbSsid : sizeof kbPass;
    int len = strlen(buf);
    joinErr[0] = 0;
    switch (k[i].act) {
      case K_CHAR: case K_SPACE:
        if (len < (int)cap - 1) { buf[len] = k[i].c; buf[len + 1] = 0; }
        if (kbMode == 1) kbMode = 0;
        break;
      case K_BKSP: if (len) buf[len - 1] = 0; break;
      case K_SHIFT: kbMode = kbMode == 0 ? 1 : 0; break;
      case K_MODE: kbMode = kbMode < 2 ? 2 : 0; break;
      case K_SYM: kbMode = kbMode == 2 ? 3 : 2; break;
      case K_SHOW: kbShow = !kbShow; break;
      case K_JOIN: kbJoin(); break;
    }
    dirty = true;
    return;
  }
}

void wifiTap(int x, int y) {  // taps on the status and network-list views (the keyboard uses kbPress)
  if (wview == V_STATUS) {
    if (inRect(x, y, B_CHOOSE, 4)) { wview = V_LIST; startScan(); }
    else if (savedSsid[0] && inRect(x, y, B_FORGET, 4)) forgetNetwork();
    joinErr[0] = 0;
    dirty = true;
    return;
  }
  if (inRect(x, y, B_BACK, 4)) { wview = V_STATUS; scanning = false; }
  else if (inRect(x, y, B_RESCAN, 4)) startScan();
  else if (inRect(x, y, B_UP, 4)) listTop = max(0, listTop - LIST_ROWS);
  else if (inRect(x, y, B_DOWN, 4)) { if (listTop + LIST_ROWS < nNets) listTop += LIST_ROWS; }
  else {
    for (int i = 0; i < LIST_ROWS && listTop + i < nNets; i++)
      if (inRect(x, y, listRow(i))) {
        const NetEntry &e = nets[listTop + i];
        strlcpy(kbSsid, e.ssid, sizeof kbSsid);
        kbPass[0] = 0; kbMode = 0; kbShow = false; kbNeedSsid = false;
        joinErr[0] = 0;
        wview = V_KEY;
        if (e.open) startJoin(kbSsid, "");
        break;
      }
  }
  dirty = true;
}

void render() {
  curFont = -1;  // fonts are unloaded implicitly per frame start
  fb.fillSprite(BG);
  if (page == 3 && wview == V_KEY) {
    wifiKeyboard();
  } else {
    if (page == 0) pageGpu(); else if (page == 1) pageLlm(); else if (page == 2) pageFans(); else pageWifi();
    tabs();
  }
  fb.pushSprite(0, 0);
  if (curFont >= 0) { fb.unloadFont(); curFont = -1; }
  dirty = false;
  lastDraw = millis();
}

// ---------- input: serial ----------
void pushHist(uint8_t *h, uint8_t val) {
  if (histLen < HIST) { h[histLen] = val; return; }
  memmove(h, h + 1, HIST - 1);
  h[HIST - 1] = val;
}

void onLine(const char *line) {
  if (!strncmp(line, "P100,", 5)) {
    Gpu n = gpu;
    if (sscanf(line, "P100,%d,%d,%d,%d,%f,%f,%d,%d", &n.temp, &n.util, &n.memUsedMiB, &n.memTotalMiB,
               &n.power, &n.powerLimit, &n.smClk, &n.memClk) < 8) return;
    gpu = n;
    static uint32_t lastHist = 0;
    if (millis() - lastHist >= 450) {  // numbers update on every reading (~4 Hz); the graph gets a point every ~500 ms
      lastHist = millis();
      pushHist(histUtil, constrain(gpu.util, 0, 100));
      pushHist(histTemp, constrain(gpu.temp, 0, 100));
      if (histLen < HIST) histLen++;
    }
    lastGpuRx = millis();
    haveGpu = true;
  } else if (!strncmp(line, "LLM,", 4)) {
    Llm n = llm;
    char quant[16] = "", model[40] = "";
    int got = sscanf(line, "LLM,%d,%d,%d,%f,%f,%ld,%ld,%f,%f,%ld,%15[^,],%39[^\n]", &n.state, &n.ctxUsed,
                     &n.ctxTotal, &n.gen, &n.prompt, &n.predTotal, &n.promptTotal, &n.avgGen, &n.avgPrompt,
                     &n.uptime, quant, model);
    if (got < 10) return;
    if (got >= 11) strlcpy(n.quant, quant, sizeof n.quant);
    if (got >= 12 && model[0]) strlcpy(n.model, model, sizeof n.model);
    llm = n;
    lastLlmRx = millis();
    haveLlm = true;
  } else if (!strncmp(line, "FAN,", 4)) {
    if (millis() - lastUserFan < 900) return;  // don't fight the user's finger
    char tmp[300];
    strlcpy(tmp, line, sizeof tmp);
    char *sp = nullptr;
    strtok_r(tmp, ",", &sp);  // "FAN"
    char *prof = strtok_r(nullptr, ",", &sp), *cnt = strtok_r(nullptr, ",", &sp);
    if (!prof || !cnt) return;
    Fans nf;
    strlcpy(nf.profile, prof, sizeof nf.profile);
    nf.n = constrain(atoi(cnt), 0, 4);
    for (int i = 0; i < nf.n; i++) {
      char *nm = strtok_r(nullptr, ",", &sp), *rp = strtok_r(nullptr, ",", &sp), *pc = strtok_r(nullptr, ",", &sp),
           *tp = strtok_r(nullptr, ",", &sp), *mn = strtok_r(nullptr, ",", &sp);
      if (!nm || !rp || !pc || !tp || !mn) { nf.n = i; break; }
      strlcpy(nf.f[i].name, nm, sizeof nf.f[i].name);
      nf.f[i].rpm = atoi(rp); nf.f[i].pct = atoi(pc); nf.f[i].temp = atoi(tp); nf.f[i].manual = atoi(mn) != 0;
    }
    fans = nf;
    lastFanRx = millis();
    haveFan = true;
  } else return;
  dirty = true;
}

// ---------- input: touch (FT6336 @0x38, portrait raw 240x320 -> landscape 320x240) ----------
// NOT YET VERIFIED ON HARDWARE. The default below follows the 480x320 variant. If taps land
// mirrored or rotated, change TOUCH_MAP in platformio.ini build_flags:
//   0: x = ry,        y = 239 - rx   (default, matches TFT_ROTATION 1 on the other variant)
//   1: x = 319 - ry,  y = rx         (matches TFT_ROTATION 3, screen turned 180 degrees)
//   2: x = ry,        y = rx
//   3: x = 319 - ry,  y = 239 - rx
#ifndef TOUCH_MAP
#define TOUCH_MAP (TFT_ROTATION == 3 ? 1 : 0)
#endif
bool readTouch(int &x, int &y) {
  uint8_t b[7] = {0};
  Wire.beginTransmission(0x38);
  Wire.write(0x00);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(0x38, 7) != 7) return false;
  for (int i = 0; i < 7; i++) b[i] = Wire.read();
  if (!(b[2] & 0x0F)) return false;
  int rx = ((b[3] & 0x0F) << 8) | b[4], ry = ((b[5] & 0x0F) << 8) | b[6];
  switch (TOUCH_MAP) {
    case 1: x = 319 - ry; y = rx; break;
    case 2: x = ry; y = rx; break;
    case 3: x = 319 - ry; y = 239 - rx; break;
    default: x = ry; y = 239 - rx; break;
  }
  return true;
}

void sendLine(const char *line) {  // commands for the host: USB serial and, when connected, the WiFi link
  Serial.print(line);
  if (hostLink.connected()) hostLink.print(line);
}
void sendFan(int i) {
  char b[40];
  snprintf(b, sizeof b, "SET,FAN,%d,%d\n", i, fans.f[i].pct);
  sendLine(b);
}

void handleTouch() {
  static bool down = false;
  static int sx, sy, lx, ly, slider = -1;
  static uint32_t seen = 0, lastPoll = 0, lastSend = 0;
  if (millis() - lastPoll < 12) return;
  lastPoll = millis();
  int x, y;
  if (readTouch(x, y)) {
    if (!down) {
      down = true; sx = x; sy = y; slider = -1;
      if (page == 2 && haveFan)
        for (int i = 0; i < fans.n; i++)
          if (inRect(sx, sy, sliderHit(fanCard(i, fans.n)))) slider = i;
      if (page == 3 && wview == V_KEY) kbPress(x, y);
    }
    lx = x; ly = y; seen = millis();
    if (slider >= 0) {  // dragging a fan slider
      Rect c = fanCard(slider, fans.n);
      int pct = constrain((x - sliderX0(c)) * 100 / (sliderX1(c) - sliderX0(c)), 0, 100);
      FanInfo &f = fans.f[slider];
      if (pct != f.pct || !f.manual) {
        f.pct = pct; f.manual = true;
        strlcpy(fans.profile, "manual", sizeof fans.profile);
        lastUserFan = millis();
        dirty = true;
        if (millis() - lastSend > 120) { sendFan(slider); lastSend = millis(); }
      }
    }
  } else if (down && millis() - seen > 150) {
    down = false;
    if (page == 3 && wview == V_KEY) return;  // keyboard is full-screen: no swipes or tabs
    if (slider >= 0) { sendFan(slider); lastUserFan = millis(); slider = -1; return; }
    int dx = lx - sx, dy = ly - sy, newPage = page;
    bool tap = abs(dx) < 25 && abs(dy) < 25;
    if (abs(dx) > 60 && abs(dx) > 2 * abs(dy)) newPage = constrain(page + (dx < 0 ? 1 : -1), 0, NTABS - 1);  // swipe
    else if (tap && sy >= TABS_Y - 6) {                                                                        // tab bar
      for (int i = 0; i < NTABS; i++)
        if (sx >= tabX(i) - 6 && sx < tabX(i) + TAB_W + 6) newPage = i;
    } else if (tap && page == 3) {
      wifiTap(sx, sy);
    } else if (tap && page == 2) {                                                                              // profile buttons
      for (int i = 0; i < 4; i++)
        if (inRect(sx, sy, segRect(i), 4)) {
          char b[40];
          snprintf(b, sizeof b, "SET,PROFILE,%s\n", PROF[i]);
          sendLine(b);
          strlcpy(fans.profile, PROF[i], sizeof fans.profile);
          for (int k = 0; k < fans.n; k++) fans.f[k].manual = false;
          lastUserFan = millis();
          dirty = true;
        }
    }
    if (newPage != page) { page = newPage; dirty = true; }
  }
}

void setup() {
  Serial.setRxBufferSize(4096);  // a frame push takes ~100 ms; don't drop feed bytes meanwhile
  Serial.begin(115200);          // USB CDC on this board; the baud rate is ignored
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);

  pinMode(TP_RST, OUTPUT);
  digitalWrite(TP_RST, LOW); delay(20); digitalWrite(TP_RST, HIGH); delay(300);
  Wire.begin(TP_SDA, TP_SCL, 400000);

  tft.init();
  tft.setRotation(TFT_ROTATION);
  tft.fillScreen(TFT_BLACK);

  BG = tft.color565(9, 12, 18);
  CARD = tft.color565(20, 26, 37);
  EDGE = tft.color565(34, 43, 59);
  GRIDC = tft.color565(38, 47, 64);
  TEXT = tft.color565(232, 237, 246);
  DIM = tft.color565(122, 135, 158);
  GREEN = tft.color565(52, 211, 153);
  AMBER = tft.color565(251, 191, 36);
  RED = tft.color565(248, 113, 113);
  CYAN = tft.color565(56, 189, 248);
  ORANGE = tft.color565(251, 146, 60);
  VIOLET = tft.color565(167, 139, 250);
  ONACC = tft.color565(8, 14, 24);

  fb.setColorDepth(16);
  fb.setAttribute(PSRAM_ENABLE, true);
  if (!fb.createSprite(W, H)) Serial.println("sprite alloc failed");
  render();
  wifiBegin();
}

void loop() {
  static LineBuf usbBuf;
  while (Serial.available()) usbBuf.feed(Serial.read());
  netLoop();
  if (kbFlash >= 0 && millis() - kbFlashAt > 110) { kbFlash = -1; dirty = true; }
  handleTouch();
  // refresh when data changed, or periodically so LIVE/NO DATA flips on time
  if ((dirty && millis() - lastDraw > 120) || millis() - lastDraw > 1000) render();
  delay(1);
}
