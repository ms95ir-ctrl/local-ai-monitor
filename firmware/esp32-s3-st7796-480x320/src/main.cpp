#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include "fonts.h"

constexpr int PIN_BL = 5, TP_RST = 7, TP_SDA = 8, TP_SCL = 9;
constexpr int W = 480, H = 320;
constexpr int HIST = 444;  // 1px per sample = 444px; graph point every 500 ms -> ~3.7 min

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
void fmtInt(char *out, size_t n, long v) {  // 1234567 -> "1,234,567"
  char t[24];
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
  fb.fillSmoothRoundRect(x, y, w, h, 12, e, BG);
  fb.fillSmoothRoundRect(x + 1, y + 1, w - 2, h - 2, 11, CARD, e);
}
void pill(int xr, int y, const char *label, uint16_t fg, uint16_t bgc) {
  int w = twidth(label, F_LABEL) + 26;
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
  constexpr int R = 40, IR = 32;
  fb.drawSmoothArc(cx, cy, R, IR, 30, 330, GRIDC, CARD, true);
  int p = constrain(pct, 0, 100);
  if (p > 0) {
    uint32_t end = 30 + (300 * p) / 100;
    if (end < 36) end = 36;
    fb.drawSmoothArc(cx, cy, R, IR, 30, end, col, CARD, true);
  }
  text(val, cx, cy - 3, F_VAL, TEXT, MC_DATUM);
  text(unit, cx, cy + 17, F_LABEL, DIM, MC_DATUM);
}

// ---------- chrome ----------
int page = 0;  // 0 = GPU, 1 = LLM, 2 = FANS
constexpr int NTABS = 3;
constexpr int TABS_Y = 286, TAB_W = 100, TAB_H = 28;
int tabX(int i) { return (W - (NTABS * TAB_W + (NTABS - 1) * 8)) / 2 + i * (TAB_W + 8); }

void tabs() {
  const char *names[NTABS] = {"GPU", "LLM", "FANS"};
  for (int i = 0; i < NTABS; i++) {
    bool on = page == i;
    fb.fillSmoothRoundRect(tabX(i), TABS_Y, TAB_W, TAB_H, 14, on ? CYAN : CARD, BG);
    text(names[i], tabX(i) + TAB_W / 2, TABS_Y + TAB_H / 2, F_BODYB, on ? ONACC : DIM, MC_DATUM);
  }
}

// ---------- pages ----------
void pageGpu() {
  bool live = haveGpu && millis() - lastGpuRx < 3500;
  text("Tesla P100", 12, 18, F_TITLE, TEXT, ML_DATUM);
  text("16 GB  \xC2\xB7  PCIe", 12 + twidth("Tesla P100", F_TITLE) + 10, 21, F_BODY, DIM, ML_DATUM);
  if (live) pill(W - 10, 7, "LIVE", GREEN, mix(BG, GREEN, 40));
  else pill(W - 10, 7, "NO DATA", RED, mix(BG, RED, 40));

  struct GaugeSpec { const char *label; char val[12]; const char *unit; int pct; uint16_t col; };
  GaugeSpec g[4];
  auto set = [&](int i, const char *label, const char *unit, int pct, uint16_t col) {
    g[i].label = label; g[i].unit = unit; g[i].pct = pct; g[i].col = col;
  };
  char lbl2[20], lbl3[20];
  int mp = gpu.memTotalMiB ? gpu.memUsedMiB * 100 / gpu.memTotalMiB : 0;
  int pp = gpu.powerLimit > 0 ? (int)(gpu.power * 100 / gpu.powerLimit) : 0;
  snprintf(lbl2, sizeof lbl2, "VRAM / %d GB", (gpu.memTotalMiB + 512) / 1024);
  snprintf(lbl3, sizeof lbl3, "POWER / %d W", (int)gpu.powerLimit);
  set(0, "UTILIZATION", "%", gpu.util, level(gpu.util, 70, 95));
  snprintf(g[0].val, 12, "%d", gpu.util);
  set(1, "TEMPERATURE", "\xC2\xB0" "C", gpu.temp, level(gpu.temp, 65, 80));
  snprintf(g[1].val, 12, "%d", gpu.temp);
  set(2, lbl2, "GB", mp, level(mp, 80, 95));
  snprintf(g[2].val, 12, "%.1f", gpu.memUsedMiB / 1024.0f);
  set(3, lbl3, "W", pp, level(pp, 70, 90));
  snprintf(g[3].val, 12, "%d", (int)(gpu.power + 0.5f));

  for (int i = 0; i < 4; i++) {
    int x = 9 + i * 117, y = 42;
    card(x, y, 111, 118);
    gauge(x + 55, y + 52, g[i].pct, g[i].col, g[i].val, g[i].unit);
    text(g[i].label, x + 55, y + 104, F_LABEL, DIM, MC_DATUM);
  }

  // history card
  const int cx = 9, cy = 166, cw = 462, ch = 114;
  card(cx, cy, cw, ch);
  fb.fillSmoothCircle(cx + 18, cy + 17, 4, CYAN, CARD);
  text("GPU util", cx + 28, cy + 17, F_LABEL, DIM, ML_DATUM);
  fb.fillSmoothCircle(cx + 92, cy + 17, 4, ORANGE, CARD);
  text("Temp \xC2\xB0""C", cx + 102, cy + 17, F_LABEL, DIM, ML_DATUM);
  char clk[40];
  snprintf(clk, sizeof clk, "SM %d MHz  \xC2\xB7  MEM %d MHz", gpu.smClk, gpu.memClk);
  text(clk, cx + cw - 14, cy + 17, F_LABEL, DIM, MR_DATUM);

  const int px = cx + 9, pt = cy + 34, pb = cy + ch - 10, ph = pb - pt;
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
  card(x, y, w, 106, active ? mix(EDGE, col, 160) : 0);
  text(label, x + 16, y + 16, F_LABEL, DIM, ML_DATUM);
  char s[16];
  snprintf(s, sizeof s, v >= 100 ? "%.0f" : "%.1f", v);
  uint16_t c = active ? col : (v > 0 ? TEXT : DIM);
  text(s, x + 14, y + 30, F_BIG, c);
  text("tok/s", x + 14 + twidth(s, F_BIG) + 8, y + 66, F_BODYB, DIM, BL_DATUM);
  text(sub, x + 16, y + 90, F_BODY, DIM, ML_DATUM);
}

void pageLlm() {
  bool live = haveLlm && millis() - lastLlmRx < 3500 && llm.state > 0;
  text(llm.model, 12, 18, F_TITLE, TEXT, ML_DATUM);
  if (llm.quant[0]) chip(12 + twidth(llm.model, F_TITLE) + 10, 8, llm.quant, VIOLET, mix(BG, VIOLET, 50));
  if (!live) pill(W - 10, 7, "OFFLINE", RED, mix(BG, RED, 40));
  else if (llm.state == 3) pill(W - 10, 7, "GENERATING", GREEN, mix(BG, GREEN, 40));
  else if (llm.state == 2) pill(W - 10, 7, "READING PROMPT", AMBER, mix(BG, AMBER, 40));
  else pill(W - 10, 7, "IDLE", DIM, mix(BG, DIM, 40));

  char sub[40];
  snprintf(sub, sizeof sub, "avg %.1f tok/s since uptime", llm.avgGen);
  bigStat(9, 42, 227, "GENERATION", llm.gen, sub, GREEN, live && llm.state == 3);
  snprintf(sub, sizeof sub, "avg %.1f tok/s since uptime", llm.avgPrompt);
  bigStat(246, 42, 227, "PROMPT PROCESSING", llm.prompt, sub, AMBER, live && llm.state == 2);

  // context
  card(9, 154, 464, 58);
  text("CONTEXT", 25, 170, F_LABEL, DIM, ML_DATUM);
  int pct = llm.ctxTotal ? (int)(100L * llm.ctxUsed / llm.ctxTotal) : 0;
  char a[16], b[16], line[64];
  fmtInt(a, sizeof a, llm.ctxUsed);
  fmtInt(b, sizeof b, llm.ctxTotal);
  snprintf(line, sizeof line, "%s / %s tokens  \xC2\xB7  %d%%", a, b, pct);
  text(line, 457, 170, F_BODYB, TEXT, MR_DATUM);
  bar(25, 189, 432, 10, pct, level(pct, 70, 90), 0);

  // totals
  char v[24];
  const char *lab[3] = {"TOKENS GENERATED", "PROMPT TOKENS", "SERVER UPTIME"};
  fmtInt(v, sizeof v, llm.predTotal);
  char v1[24];
  fmtInt(v1, sizeof v1, llm.promptTotal);
  char v2[24];
  long u = llm.uptime;
  if (u >= 86400) snprintf(v2, sizeof v2, "%ldd %ldh", u / 86400, (u % 86400) / 3600);
  else if (u >= 3600) snprintf(v2, sizeof v2, "%ldh %ldm", u / 3600, (u % 3600) / 60);
  else snprintf(v2, sizeof v2, "%ldm %lds", u / 60, u % 60);
  const char *vals[3] = {v, v1, v2};
  uint16_t cols[3] = {GREEN, AMBER, CYAN};
  for (int i = 0; i < 3; i++) {
    int x = 9 + i * 158;
    card(x, 218, 148, 62);
    fb.fillSmoothRoundRect(x + 14, 232, 4, 4, 2, cols[i], CARD);
    text(lab[i], x + 24, 234, F_LABEL, DIM, ML_DATUM);
    text(vals[i], x + 14, 268, F_TITLE, TEXT, ML_DATUM);
  }
}


// ---- fans page ----
struct Rect { int x, y, w, h; };
const char *PROF[4] = {"idle", "normal", "performance", "auto"};
const char *PROF_LABEL[4] = {"IDLE", "NORMAL", "PERFORMANCE", "AUTO"};
uint16_t profColor(int i) { return i == 0 ? CYAN : i == 1 ? GREEN : i == 2 ? ORANGE : VIOLET; }
Rect segRect(int i) { return {9 + i * 117, 42, 111, 34}; }
Rect fanCard(int i, int n) {
  int cols = n <= 1 ? 1 : 2, rows = (n + cols - 1) / cols;
  int cw = cols == 1 ? 464 : 227, ch = (196 - (rows - 1) * 8) / rows;
  return {9 + (i % cols) * (cw + 10), 84 + (i / cols) * (ch + 8), cw, ch};
}
int sliderX0(const Rect &c) { return c.x + 24; }
int sliderX1(const Rect &c) { return c.x + c.w - 24; }
int sliderY(const Rect &c) { return c.y + c.h - 22; }
bool inRect(int x, int y, const Rect &r, int pad = 0) {
  return x >= r.x - pad && x < r.x + r.w + pad && y >= r.y - pad && y < r.y + r.h + pad;
}
Rect sliderHit(const Rect &c) { return {c.x + 4, c.y + c.h - 44, c.w - 8, 42}; }

void pageFans() {
  bool live = haveFan && millis() - lastFanRx < 3500 && strcmp(fans.profile, "none") != 0;
  text("Fans", 12, 18, F_TITLE, TEXT, ML_DATUM);
  if (!live) pill(W - 10, 7, "NO CONTROLLER", RED, mix(BG, RED, 40));
  else if (!strcmp(fans.profile, "manual")) pill(W - 10, 7, "MANUAL", AMBER, mix(BG, AMBER, 40));
  else pill(W - 10, 7, "CURVES", GREEN, mix(BG, GREEN, 40));

  int active = -1;
  for (int i = 0; i < 4; i++) if (live && !strcmp(fans.profile, PROF[i])) active = i;
  for (int i = 0; i < 4; i++) {
    Rect r = segRect(i);
    bool on = i == active;
    fb.fillSmoothRoundRect(r.x, r.y, r.w, r.h, 12, on ? profColor(i) : CARD, BG);
    if (!on) fb.drawSmoothRoundRect(r.x, r.y, 12, 11, r.w, r.h, EDGE, CARD);
    text(PROF_LABEL[i], r.x + r.w / 2, r.y + r.h / 2, F_LABEL, on ? ONACC : DIM, MC_DATUM);
  }

  if (!live || fans.n == 0) {
    card(9, 84, 464, 196);
    text(live ? "No controllable fans found" : "Fan controller not running", 241, 170, F_BODYB, DIM, MC_DATUM);
    text(live ? "" : "start the fanctl service on the PC", 241, 192, F_BODY, DIM, MC_DATUM);
    return;
  }
  for (int i = 0; i < fans.n; i++) {
    Rect c = fanCard(i, fans.n);
    const FanInfo &f = fans.f[i];
    uint16_t acc = f.manual ? AMBER : (active >= 0 ? profColor(active) : CYAN);
    card(c.x, c.y, c.w, c.h);
    text(f.name, c.x + 16, c.y + 17, F_BODYB, TEXT, ML_DATUM);
    char t[24];
    snprintf(t, sizeof t, "%d\xC2\xB0""C", f.temp);
    text(t, c.x + c.w - 16, c.y + 17, F_LABEL, DIM, MR_DATUM);
    int ry = c.y + (c.h > 120 ? 58 : 46);
    char r[16];
    if (f.rpm < 0) {
      text("PWM only, no tach", c.x + 16, ry, F_BODY, DIM, ML_DATUM);
    } else {
      snprintf(r, sizeof r, "%d", f.rpm);
      text(r, c.x + 16, ry, F_VAL, TEXT, ML_DATUM);
      text("rpm", c.x + 16 + twidth(r, F_VAL) + 6, ry + 5, F_LABEL, DIM, ML_DATUM);
    }
    snprintf(r, sizeof r, "%d%%", f.pct);
    text(r, c.x + c.w - 16, ry, F_VAL, acc, MR_DATUM);
    // slider
    int x0 = sliderX0(c), x1 = sliderX1(c), sy = sliderY(c);
    int kx = x0 + constrain(f.pct, 0, 100) * (x1 - x0) / 100;
    fb.fillSmoothRoundRect(x0, sy - 3, x1 - x0, 6, 3, GRIDC, CARD);
    if (kx > x0) fb.fillSmoothRoundRect(x0, sy - 3, kx - x0, 6, 3, acc, GRIDC);
    fb.fillSmoothCircle(kx, sy, 10, TEXT, CARD);
    fb.fillSmoothCircle(kx, sy, 5, acc, TEXT);
  }
}

bool dirty = true;
uint32_t lastDraw = 0;

void render() {
  curFont = -1;  // fonts are unloaded implicitly per frame start
  fb.fillSprite(BG);
  if (page == 0) pageGpu(); else if (page == 1) pageLlm(); else pageFans();
  tabs();
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

// ---------- input: touch (FT6336 @0x38, portrait raw -> landscape) ----------
bool readTouch(int &x, int &y) {
  uint8_t b[7] = {0};
  Wire.beginTransmission(0x38);
  Wire.write(0x00);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(0x38, 7) != 7) return false;
  for (int i = 0; i < 7; i++) b[i] = Wire.read();
  if (!(b[2] & 0x0F)) return false;
  int rx = ((b[3] & 0x0F) << 8) | b[4], ry = ((b[5] & 0x0F) << 8) | b[6];
  x = ry;
  y = 319 - rx;
  return true;
}

void sendFan(int i) { Serial.printf("SET,FAN,%d,%d\n", i, fans.f[i].pct); }

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
    if (slider >= 0) { sendFan(slider); lastUserFan = millis(); slider = -1; return; }
    int dx = lx - sx, dy = ly - sy, newPage = page;
    bool tap = abs(dx) < 25 && abs(dy) < 25;
    if (abs(dx) > 70 && abs(dx) > 2 * abs(dy)) newPage = constrain(page + (dx < 0 ? 1 : -1), 0, NTABS - 1);  // swipe
    else if (tap && sy >= TABS_Y - 6) {                                                                        // tab bar
      for (int i = 0; i < NTABS; i++)
        if (sx >= tabX(i) - 6 && sx < tabX(i) + TAB_W + 6) newPage = i;
    } else if (tap && page == 2) {                                                                              // profile buttons
      for (int i = 0; i < 4; i++)
        if (inRect(sx, sy, segRect(i), 4)) {
          Serial.printf("SET,PROFILE,%s\n", PROF[i]);
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
  Serial.begin(115200);
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);

  pinMode(TP_RST, OUTPUT);
  digitalWrite(TP_RST, LOW); delay(20); digitalWrite(TP_RST, HIGH); delay(300);
  Wire.begin(TP_SDA, TP_SCL, 400000);

  tft.init();
  tft.setRotation(1);
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
}

void loop() {
  static char buf[320];
  static int n = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') { buf[n] = 0; if (n) onLine(buf); n = 0; }
    else if (c != '\r' && n < (int)sizeof buf - 1) buf[n++] = c;
  }
  handleTouch();
  // refresh when data changed, or periodically so LIVE/NO DATA flips on time
  if ((dirty && millis() - lastDraw > 120) || millis() - lastDraw > 1000) render();
  delay(1);
}
