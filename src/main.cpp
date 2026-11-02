// ============================================================
//  main.cpp  -  PoisonCore-Cardputer  (objetivo: MeshCore)
//  Splash + maquina de estados [A]Config RF -> [B]Identidad -> [C]Menu
//  Vectores: Announce Flood, Mensajes (canal/DM), Regenerar, Recon.
//  Boot tolerante: funciona sin el Cap LoRa (radio_ok()==false).
//  UI con canvas de doble buffer (pushSprite) para evitar flickering.
// ============================================================
#include <Arduino.h>
#include <M5Unified.h>
#include <M5Cardputer.h>
#include <Wire.h>
#include <SPI.h>
#include <esp_random.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "config.h"
#include "keyboard.h"
#include "screensaver.h"
#include "radio.h"
#include "pc_identity.h"
#include "pc_mesh.h"

// ---- Estado global -----------------------------------------
RFConfig      g_rf = { 915.8f, 250.0f, 10, 5, TX_POWER_MAX, DEFAULT_PRESET_IDX };  // Australia
LocalIdentity g_identity;
uint8_t       g_adv_type = ADV_TYPE_CHAT;
char          g_node_name[20] = "PoisonNode";
GroupChannel  g_pubchan;

// Canvas de doble buffer: todo el dibujo de menus va aca y se vuelca
// de una sola vez con pushSprite() -> sin flickering.
static M5Canvas canvas(&M5.Display);

// Hora de los paquetes: la toma el reloj derivado de la malla (pc_mesh), que
// arranca en un fallback y se engancha al ts de los adverts sniffeados.
static uint32_t time_now() { return pc_clock_now(); }

// Formatea el reloj actual (UTC) para la UI: "MM-DD HH:MM Z sync|~".
static void fmt_clock(char* b, size_t cap) {
    time_t t = (time_t)pc_clock_now();
    struct tm tmv; gmtime_r(&t, &tmv);
    snprintf(b, cap, "%02d-%02d %02d:%02dZ %s",
             tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min,
             pc_clock_synced() ? "sync" : "~fallback");
}

static void rnd_name(char* dst, size_t cap) {
    snprintf(dst, cap, "Node-%02X%02X",
             (unsigned)(esp_random() & 0xFF), (unsigned)(esp_random() & 0xFF));
}

static const char* adv_type_name(uint8_t t) {
    switch (t) { case ADV_TYPE_REPEATER: return "REPEATER";
                 case ADV_TYPE_ROOM:     return "ROOM";
                 default:                return "CHAT"; }
}

// ============================================================
//  Helpers de dibujo (sobre el canvas; el push lo hace cada pantalla)
// ============================================================
static void hdr(const char* title) {
    canvas.fillRect(0, 0, LCD_WIDTH, UI_HEADER_H, COL_HEADER_BG);
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    canvas.setTextColor(COL_ACCENT);
    canvas.setTextDatum(ML_DATUM);
    canvas.drawString("PC|", 3, UI_HEADER_H / 2);
    canvas.setTextColor(COL_TEXT);
    canvas.setTextDatum(MC_DATUM);
    canvas.drawString(title, LCD_WIDTH / 2, UI_HEADER_H / 2);
    canvas.drawFastHLine(0, UI_HEADER_H - 1, LCD_WIDTH, COL_ACCENT);
}

static void ftr(const char* txt, uint16_t col = COL_OK) {
    int y = LCD_HEIGHT - UI_FOOTER_H;
    canvas.fillRect(0, y, LCD_WIDTH, UI_FOOTER_H, COL_HEADER_BG);
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    canvas.setTextColor(col);
    canvas.setTextDatum(ML_DATUM);
    canvas.drawString(txt, 2, y + UI_FOOTER_H / 2);
}

static void radio_badge() {
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    canvas.setTextDatum(MR_DATUM);
    if (radio_ok()) { canvas.setTextColor(COL_OK);      canvas.drawString("RF OK",  LCD_WIDTH - 2, UI_HEADER_H / 2); }
    else            { canvas.setTextColor(COL_WARNING); canvas.drawString("RF N/A", LCD_WIDTH - 2, UI_HEADER_H / 2); }
}

static inline void push() { canvas.pushSprite(0, 0); }

// Overlay de mensaje centrado (una linea) con push inmediato.
static void overlay(const char* title, const char* msg, uint16_t col) {
    canvas.fillSprite(COL_BG);
    hdr(title);
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextDatum(MC_DATUM);
    canvas.setTextColor(col);
    canvas.drawString(msg, LCD_WIDTH / 2, LCD_HEIGHT / 2);
    push();
}

// ============================================================
//  SPLASH  -  estilo PoisonMesh, texto "PoIsOnC0rE"
//  (se dibuja una sola vez, directo sobre M5.Display)
// ============================================================
static void splash() {
    M5.Display.fillScreen(0x0000);
    delay(150);

    // Fondo con gradiente rojo oscuro
    for (int y = 0; y < LCD_HEIGHT; y++) {
        uint16_t c = M5.Display.color565((y * 18) / LCD_HEIGHT, 0, 0);
        M5.Display.drawFastHLine(0, y, LCD_WIDTH, c);
    }

    static const char* L1[] = { "P", "o", "I", "s", "O", "n" };  // linea 1 (verde veneno)
    static const char* L2[] = { "C", "0", "r", "E" };            // linea 2

    // "PoIsOn" verde veneno, grande
    M5.Display.setTextSize(2);
    M5.Display.setFont(&fonts::FreeSans9pt7b);
    int x1 = (LCD_WIDTH - 6 * 21) / 3, y1 = 8;
    for (int i = 0; i < 6; i++) {
        M5.Display.setTextColor(M5.Display.color565(0, (i % 2) ? 255 : 220, 0));
        M5.Display.setCursor(x1 + i * 21, y1);
        M5.Display.print(L1[i]);
    }
    // "C0rE": la C en verde lima chillon, el resto gris
    const uint16_t LIME = M5.Display.color565(170, 255, 0);
    int x2 = (LCD_WIDTH - 4 * 20) / 2, y2 = 40;
    for (int i = 0; i < 4; i++) {
        if (i == 0) M5.Display.setTextColor(LIME);
        else        M5.Display.setTextColor(M5.Display.color565((i % 2) ? 140 : 110,
                                                                (i % 2) ? 140 : 110,
                                                                (i % 2) ? 140 : 110));
        M5.Display.setCursor(x2 + i * 20 - (i == 0 ? 4 : 0), y2);  // C corrida 4px a la izquierda
        M5.Display.print(L2[i]);
    }

    M5.Display.setTextSize(1);
    M5.Display.setFont(&fonts::FreeSans9pt7b);
    M5.Display.setTextColor(M5.Display.color565(90, 90, 90));
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString("by Teno  -  v" FW_VERSION, LCD_WIDTH / 2, 80);
    M5.Display.drawFastHLine(20, 92, LCD_WIDTH - 40, M5.Display.color565(80, 0, 0));

    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextColor(M5.Display.color565(60, 60, 60));
    M5.Display.drawString("MeshCore Security Research Tool", LCD_WIDTH / 2, 104);
    M5.Display.drawString("Authorized use only", LCD_WIDTH / 2, 116);

    M5.Display.setTextColor(M5.Display.color565(0, 180, 0));
    M5.Display.drawString("[ pulse una tecla para continuar ]", LCD_WIDTH / 2, LCD_HEIGHT - 8);

    keyboard_wait();
}

// ============================================================
//  [A]  CONFIG RF
// ============================================================
static const char* CFG_LABELS[] = { "Preset", "Freq", "BW", "SF", "CR", "TxPwr" };
static const uint8_t CFG_ROWS = 6;
static const float BW_SET[] = { 62.5f, 125.0f, 250.0f, 500.0f };

static void load_preset(uint8_t idx) {
    const RFPreset& p = RF_PRESETS[idx];
    g_rf.freqMHz = p.freqMHz;
    g_rf.bwKHz   = p.bwKHz;
    g_rf.sf      = p.sf;
    g_rf.cr      = p.cr;
    g_rf.presetIdx = idx;
}

static void draw_config(uint8_t sel) {
    canvas.fillSprite(COL_BG);
    hdr("Config RF");
    int rowH = (UI_CONTENT_H - 2) / CFG_ROWS;
    int startY = UI_CONTENT_Y + 1;
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    for (uint8_t i = 0; i < CFG_ROWS; i++) {
        int y = startY + i * rowH;
        bool s = (i == sel);
        if (s) canvas.fillRect(0, y, LCD_WIDTH, rowH - 1, COL_SEL_BG);
        canvas.setTextColor(s ? COL_ACCENT : COL_TEXT_DIM);
        canvas.setTextDatum(ML_DATUM);
        canvas.drawString(CFG_LABELS[i], 6, y + rowH / 2);
        char v[28];
        switch (i) {
            case 0: snprintf(v, sizeof(v), "< %s >", RF_PRESETS[g_rf.presetIdx].name); break;
            case 1: snprintf(v, sizeof(v), "< %.3f MHz >", g_rf.freqMHz); break;
            case 2: snprintf(v, sizeof(v), "< %.1f kHz >", g_rf.bwKHz); break;
            case 3: snprintf(v, sizeof(v), "< SF%u >", g_rf.sf); break;
            case 4: snprintf(v, sizeof(v), "< 4/%u >", g_rf.cr); break;
            default: snprintf(v, sizeof(v), "< %d dBm >", g_rf.txPower); break;
        }
        canvas.setTextColor(COL_TEXT);
        canvas.setTextDatum(MR_DATUM);
        canvas.drawString(v, LCD_WIDTH - 6, y + rowH / 2);
    }
    ftr("Fn+;. mover  Fn+,/ cambiar  Enter=OK");
    push();
}

static void bw_step(int dir) {
    int n = sizeof(BW_SET) / sizeof(BW_SET[0]);
    int cur = 0;
    for (int i = 0; i < n; i++) if (BW_SET[i] == g_rf.bwKHz) cur = i;
    cur = (cur + dir + n) % n;
    g_rf.bwKHz = BW_SET[cur];
}

static void screen_config_rf() {
    uint8_t sel = 0;
    bool need = true;
    while (true) {
        if (need) { draw_config(sel); need = false; }
        screen_toggle_tick();
        KeyEvent ev = keyboard_read();
        if (ev.type == Key::None) { delay(15); continue; }
        need = true;
        if (ev.type == Key::Up)        { if (sel > 0) sel--; }
        else if (ev.type == Key::Down) { if (sel < CFG_ROWS - 1) sel++; }
        else if (ev.type == Key::Left || ev.type == Key::Right) {
            int d = (ev.type == Key::Right) ? 1 : -1;
            switch (sel) {
                case 0: g_rf.presetIdx = (g_rf.presetIdx + d + NUM_RF_PRESETS) % NUM_RF_PRESETS;
                        load_preset(g_rf.presetIdx); break;
                case 1: g_rf.freqMHz += d * 0.1f;
                        if (g_rf.freqMHz < 150) g_rf.freqMHz = 150;
                        if (g_rf.freqMHz > 960) g_rf.freqMHz = 960; break;
                case 2: bw_step(d); break;
                case 3: g_rf.sf = constrain(g_rf.sf + d, 7, 12); break;
                case 4: g_rf.cr = constrain(g_rf.cr + d, 5, 8); break;
                case 5: g_rf.txPower = constrain(g_rf.txPower + d, TX_POWER_MIN, TX_POWER_MAX); break;
            }
        }
        else if (ev.type == Key::Enter) { radio_apply_config(); return; }
    }
}

// ============================================================
//  [B]  IDENTIDAD
// ============================================================
static void draw_identity(uint8_t sel, bool editing) {
    canvas.fillSprite(COL_BG);
    hdr("Identidad"); radio_badge();
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextSize(1);
    int y0 = UI_CONTENT_Y + 10;

    canvas.setTextDatum(ML_DATUM);
    canvas.setTextColor(sel == 0 ? COL_ACCENT : COL_TEXT_DIM);
    canvas.drawString("Nombre:", 6, y0);
    canvas.setTextColor(COL_TEXT);
    char nb[24]; snprintf(nb, sizeof(nb), "%s%s", g_node_name, (sel == 0 && editing) ? "_" : "");
    canvas.drawString(nb, 80, y0);

    canvas.setTextColor(sel == 1 ? COL_ACCENT : COL_TEXT_DIM);
    canvas.drawString("Tipo:", 6, y0 + 22);
    canvas.setTextColor(COL_TEXT);
    char tb[24]; snprintf(tb, sizeof(tb), "< %s >", adv_type_name(g_adv_type));
    canvas.drawString(tb, 80, y0 + 22);

    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(g_identity.valid ? COL_OK : COL_TEXT_DIM);
    char pb[40];
    if (g_identity.valid)
        snprintf(pb, sizeof(pb), "pub: %02X %02X %02X ...  (prefijo %02X)",
                 g_identity.pub[0], g_identity.pub[1], g_identity.pub[2], g_identity.pub[0]);
    else
        snprintf(pb, sizeof(pb), "pub: (sin generar)");
    canvas.drawString(pb, 6, y0 + 46);

    ftr("Fn+;. fila  texto=editar  Fn+,/ tipo  Enter=generar");
    push();
}

static void screen_identity() {
    uint8_t sel = 0;
    uint8_t clen = strlen(g_node_name);
    bool need = true;
    while (true) {
        if (need) { draw_identity(sel, sel == 0); need = false; }
        screen_toggle_tick();
        KeyEvent ev = keyboard_read();
        if (ev.type == Key::None) { delay(15); continue; }
        need = true;
        if (ev.type == Key::Up || ev.type == Key::Down) { sel ^= 1; }
        else if (ev.type == Key::Left || ev.type == Key::Right) {
            if (sel == 1) {
                int d = (ev.type == Key::Right) ? 1 : -1;
                g_adv_type = (uint8_t)constrain((int)g_adv_type + d, ADV_TYPE_CHAT, ADV_TYPE_ROOM);
            }
        }
        else if (ev.type == Key::Backspace) {
            if (sel == 0 && clen > 0) { g_node_name[--clen] = 0; }
        }
        else if (ev.type == Key::Char) {
            if (sel == 0 && clen < sizeof(g_node_name) - 1) {
                g_node_name[clen++] = ev.ch; g_node_name[clen] = 0;
            }
        }
        else if (ev.type == Key::Enter) {
            overlay("Identidad", "Generando par Ed25519...", COL_WARNING);
            g_identity.generate();
            return;
        }
    }
}

// ============================================================
//  [C]  MENU PRINCIPAL
// ============================================================
static const char* MENU[] = {
    "1. Announce Flood",
    "2. Mensajes",
    "3. CoreImpersonator",
    "4. GhostSwarm (auto)",
    "5. DarkMirror (auto)",
    "6. Regenerar identidad",
    "7. Recon / Sniffer",
    "8. Config RF",
};
static const uint8_t MENU_N = sizeof(MENU) / sizeof(MENU[0]);

static void pump_rx() {
    if (radio_rx_available()) {
        uint8_t b[PC_MAX_MTU]; int r = 0;
        int n = radio_read(b, sizeof(b), &r);
        if (n > 0) pc_sniffer_process(b, n, r);
    }
}

#define MENU_VIS 6   // filas visibles a la vez (el resto por scroll)

static void draw_menu(uint8_t sel, uint8_t top) {
    canvas.fillSprite(COL_BG);
    hdr("Menu Principal");
    canvas.setFont(&fonts::Font0); canvas.setTextSize(1); canvas.setTextDatum(MR_DATUM);
    char nb[16];
    if (g_node_count > 0) { snprintf(nb, sizeof(nb), "%d nodos", g_node_count); canvas.setTextColor(COL_OK); }
    else                  { snprintf(nb, sizeof(nb), "%s", radio_ok() ? "RX" : "RF N/A"); canvas.setTextColor(COL_TEXT_DIM); }
    canvas.drawString(nb, LCD_WIDTH - 2, UI_HEADER_H / 2);

    const int rowH = 15, startY = UI_CONTENT_Y;
    canvas.setFont(&fonts::FreeSans9pt7b);
    for (int i = 0; i < MENU_VIS; i++) {
        int idx = top + i;
        if (idx >= MENU_N) break;
        int y = startY + i * rowH;
        bool s = (idx == sel);
        if (s) { canvas.fillRect(0, y, LCD_WIDTH, rowH - 1, COL_SEL_BG); canvas.setTextColor(COL_ACCENT); }
        else   { canvas.setTextColor(idx == MENU_N - 1 ? COL_TEXT_DIM : COL_TEXT); }
        canvas.setTextDatum(ML_DATUM);
        if (s) canvas.drawString(">", 2, y + rowH / 2);
        canvas.drawString(MENU[idx], 14, y + rowH / 2);
    }
    // indicadores de scroll
    canvas.setFont(&fonts::Font0); canvas.setTextDatum(MR_DATUM); canvas.setTextColor(COL_ACCENT);
    if (top > 0)                 canvas.drawString("^", LCD_WIDTH - 3, startY + 4);
    if (top + MENU_VIS < MENU_N) canvas.drawString("v", LCD_WIDTH - 3, startY + MENU_VIS * rowH - 6);

    char fb[40]; snprintf(fb, sizeof(fb), "%.3f MHz  %s", g_rf.freqMHz, g_node_name);
    ftr(fb);
    push();
}

static uint8_t screen_main_menu() {
    uint8_t sel = 0, top = 0;
    uint32_t last = 0; int last_count = -1;
    while (true) {
        pump_rx();
        if ((int)g_node_count != last_count || millis() - last > 600) {
            draw_menu(sel, top); last = millis(); last_count = g_node_count;
        }
        screen_toggle_tick();
        KeyEvent ev = keyboard_read();
        if (ev.type == Key::None) { delay(15); continue; }
        last_count = -1;  // forzar redraw
        if (ev.type == Key::Up)        { if (sel > 0) sel--; }
        else if (ev.type == Key::Down) { if (sel < MENU_N - 1) sel++; }
        else if (ev.type == Key::Enter) return sel;
        // ventana de scroll
        if (sel < top) top = sel;
        if (sel >= top + MENU_VIS) top = sel - MENU_VIS + 1;
    }
}

// ============================================================
//  [C.1]  ANNOUNCE FLOOD
// ============================================================
static void draw_flood(uint8_t mode, bool running, uint32_t sent) {
    canvas.fillSprite(COL_BG);
    hdr("Announce Flood"); radio_badge();
    canvas.setFont(&fonts::FreeSans9pt7b); canvas.setTextDatum(MC_DATUM);
    canvas.setTextColor(COL_TEXT);
    char b[40];
    snprintf(b, sizeof(b), "Modo: < %s >", mode == 0 ? "Refresh" : "Sybil");
    canvas.drawString(b, LCD_WIDTH / 2, UI_CONTENT_Y + 14);

    canvas.setTextColor(running ? COL_DANGER : COL_TEXT_DIM);
    canvas.drawString(running ? "[ FLOOD ACTIVO ]" : "detenido", LCD_WIDTH / 2, UI_CONTENT_Y + 36);

    canvas.setTextColor(COL_OK);
    snprintf(b, sizeof(b), "Enviados: %lu", (unsigned long)sent);
    canvas.drawString(b, LCD_WIDTH / 2, UI_CONTENT_Y + 58);

    if (!radio_ok()) ftr("SIN Cap LoRa: simulado. Enter=start/stop  `=salir", COL_WARNING);
    else             ftr("Fn+,/ modo  Enter=start/stop  `=salir");
    push();
}

static void screen_flood() {
    uint8_t mode = 0; bool running = false; uint32_t sent = 0;
    uint32_t last_tx = 0; const uint32_t RATE = 500;
    uint32_t ts = time_now();
    LocalIdentity sybil;
    bool need = true;
    while (true) {
        if (need) { draw_flood(mode, running, sent); need = false; }
        if (running && millis() - last_tx >= RATE) {
            last_tx = millis();
            uint8_t buf[PC_MAX_MTU]; int n = 0;
            if (mode == 0) {
                n = pc_build_advert(g_identity, g_adv_type, g_node_name, false, 0, 0, ts++, buf);
            } else {
                sybil.generate();
                char nm[16]; rnd_name(nm, sizeof(nm));
                n = pc_build_advert(sybil, ADV_TYPE_CHAT, nm, false, 0, 0, time_now(), buf);
            }
            if (n > 0) { if (radio_ok()) radio_transmit(buf, n); sent++; }
            need = true;
        }
        screen_toggle_tick();
        KeyEvent ev = keyboard_read();
        if (ev.type == Key::Escape) { if (running) { running = false; need = true; } else return; }
        else if (ev.type == Key::Enter) { running = !running; last_tx = 0; need = true; }
        else if ((ev.type == Key::Left || ev.type == Key::Right) && !running) { mode ^= 1; need = true; }
        delay(8);
    }
}

// ============================================================
//  [C.2]  MENSAJES  (canal publico / DM)
// ============================================================
static void draw_msg_select(uint8_t dest, int victim) {
    canvas.fillSprite(COL_BG);
    hdr("Mensajes"); radio_badge();
    canvas.setFont(&fonts::FreeSans9pt7b); canvas.setTextDatum(MC_DATUM);
    char b[48];
    snprintf(b, sizeof(b), "Destino: < %s >", dest == 0 ? "Canal Public" : "DM");
    canvas.setTextColor(COL_TEXT);
    canvas.drawString(b, LCD_WIDTH / 2, UI_CONTENT_Y + 14);

    if (dest == 0) {
        snprintf(b, sizeof(b), "sender = %s", g_node_name);
        canvas.setTextColor(COL_TEXT_DIM);
        canvas.drawString(b, LCD_WIDTH / 2, UI_CONTENT_Y + 38);
    } else {
        if (g_node_count == 0) {
            canvas.setTextColor(COL_WARNING);
            canvas.drawString("sin objetivos (usa Recon)", LCD_WIDTH / 2, UI_CONTENT_Y + 38);
        } else {
            snprintf(b, sizeof(b), "< %02X %s >", g_nodes[victim].prefix, g_nodes[victim].name);
            canvas.setTextColor(COL_TEXT);
            canvas.drawString(b, LCD_WIDTH / 2, UI_CONTENT_Y + 38);
        }
    }
    ftr("Fn+;. destino  Fn+,/ objetivo  Enter=escribir  `=salir");
    push();
}

// Dibuja 'msg' partido en varias lineas, cortando por ancho real (sin wordwrap),
// con cursor '_' al final. Hasta 'maxlines' renglones; lo que no entra se corta.
static void draw_msg_body(const char* msg, int x, int y0, int lineH, int maxlines, int wrapW) {
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextDatum(ML_DATUM);
    canvas.setTextColor(COL_ACCENT);
    char buf[160]; snprintf(buf, sizeof(buf), "%s_", msg ? msg : "");
    int len = (int)strlen(buf);
    int start = 0, line = 0;
    char tmp[160];
    while (start < len && line < maxlines) {
        int n = 0;
        while (start + n < len) {                 // extiende hasta que no entre
            int m = n + 1;
            memcpy(tmp, buf + start, m); tmp[m] = 0;
            if (canvas.textWidth(tmp) > wrapW) break;
            n = m;
        }
        if (n == 0) n = 1;                         // al menos un char: evita colgarse
        memcpy(tmp, buf + start, n); tmp[n] = 0;
        canvas.drawString(tmp, x, y0 + line * lineH);
        start += n; line++;
    }
}

// Editor de texto generico (multilinea). Retorna 1=Enter, -1=Escape.
static int edit_text(char* buf, size_t cap, const char* title,
                     const char* header, const char* foot) {
    uint8_t clen = strlen(buf);
    bool need = true;
    while (true) {
        if (need) {
            canvas.fillSprite(COL_BG);
            hdr(title);
            canvas.setFont(&fonts::Font0); canvas.setTextDatum(ML_DATUM);
            canvas.setTextColor(COL_TEXT_DIM);
            if (header && header[0]) canvas.drawString(header, 4, UI_CONTENT_Y + 6);
            draw_msg_body(buf, 4, UI_CONTENT_Y + 26, 18, 3, LCD_WIDTH - 8);
            ftr(foot);
            push();
            need = false;
        }
        screen_toggle_tick();
        KeyEvent k = keyboard_read();
        if (k.type == Key::None) { delay(10); continue; }
        need = true;
        if (k.type == Key::Escape)         return -1;
        else if (k.type == Key::Enter)     return 1;
        else if (k.type == Key::Backspace) { if (clen > 0) buf[--clen] = 0; }
        else if (k.type == Key::Char)      { if (clen < cap - 1) { buf[clen++] = k.ch; buf[clen] = 0; } }
    }
}

static void screen_messages() {
    uint8_t dest = 0; int victim = 0; bool need = true;
    while (true) {
        if (need) { draw_msg_select(dest, victim); need = false; }
        screen_toggle_tick();
        KeyEvent ev = keyboard_read();
        if (ev.type == Key::None) { delay(15); continue; }
        if (ev.type == Key::Escape) return;
        else if (ev.type == Key::Up || ev.type == Key::Down) { dest ^= 1; need = true; }
        else if (ev.type == Key::Left)  { if (dest == 1 && g_node_count > 0) { victim = (victim + g_node_count - 1) % g_node_count; need = true; } }
        else if (ev.type == Key::Right) { if (dest == 1 && g_node_count > 0) { victim = (victim + 1) % g_node_count; need = true; } }
        else if (ev.type == Key::Enter) {
            if (dest == 1 && g_node_count == 0) { continue; }
            char msg[100] = "";
            char hl[48];
            if (dest == 0) snprintf(hl, sizeof(hl), "-> Canal Public (as %s)", g_node_name);
            else           snprintf(hl, sizeof(hl), "-> DM %02X %s", g_nodes[victim].prefix, g_nodes[victim].name);
            if (edit_text(msg, sizeof(msg), "Mensaje", hl, "Enter=enviar  `=cancelar") == 1) {
                uint8_t buf[PC_MAX_MTU]; int n = 0; uint32_t ts = time_now();
                if (dest == 0) n = pc_build_group_text(g_pubchan, g_node_name, msg, ts, buf);
                else           n = pc_build_dm(g_identity, g_nodes[victim].pub, msg, ts, buf);
                bool sent = false;
                if (n > 0 && radio_ok()) sent = (radio_transmit(buf, n) == 0);
                if (n <= 0)           overlay("Mensaje", "error al construir", COL_DANGER);
                else if (!radio_ok()) overlay("Mensaje", "construido (sin Cap LoRa)", COL_WARNING);
                else if (sent)        overlay("Mensaje", "enviado", COL_OK);
                else                  overlay("Mensaje", "fallo TX", COL_DANGER);
                delay(1000);
            }
            need = true;
        }
    }
}

// ============================================================
//  [C.2b]  COREIMPERSONATOR  -  spoof de remitente en el canal publico
//  Elegis un nombre (de un nodo del sniffer, o escrito a mano) y mandas un
//  mensaje al canal publico como si fuera esa identidad. El nombre de canal es
//  texto plano no ligado a clave (doc 6 / 7.3): no requiere privada ajena.
// ============================================================
static void screen_impersonator() {
    if (radio_ok()) radio_start_receive();

    // --- Fase 1: elegir el nombre a suplantar ---
    char sender[20] = "";
    int sel = 0, top = 0;
    int rowH = 12, maxrows = UI_CONTENT_H / rowH;
    bool need = true; int last_count = -1;

    while (true) {
        int total = g_node_count + 1;              // nodos del sniffer + "a mano"
        if (sel > total - 1) sel = total - 1;
        if (need || (int)g_node_count != last_count) {
            canvas.fillSprite(COL_BG);
            hdr("CoreImpersonator"); radio_badge();
            canvas.setFont(&fonts::Font0); canvas.setTextSize(1); canvas.setTextDatum(TL_DATUM);
            int startY = UI_CONTENT_Y;
            if (g_node_count == 0) {
                canvas.setTextColor(COL_WARNING);
                canvas.drawString("Sin nodos en sniffer.", 4, startY + 4);
                canvas.setTextColor(COL_TEXT_DIM);
                canvas.drawString("Corre Recon o escribi un nombre:", 4, startY + 16);
                startY += 34;
            }
            for (int i = 0; i < maxrows; i++) {
                int idx = top + i;
                if (idx >= total) break;
                int y = startY + i * rowH;
                bool s = (idx == sel);
                if (s) { canvas.fillRect(0, y, LCD_WIDTH, rowH, COL_SEL_BG); canvas.setTextColor(COL_ACCENT); }
                else   canvas.setTextColor(idx < g_node_count ? COL_TEXT : COL_OK);
                char b[48];
                if (idx < g_node_count)
                    snprintf(b, sizeof(b), "%s%02X %-12s %ddBm", s ? ">" : " ",
                             g_nodes[idx].prefix, g_nodes[idx].name, g_nodes[idx].rssi);
                else
                    snprintf(b, sizeof(b), "%s[ escribir nombre a mano ]", s ? ">" : " ");
                canvas.drawString(b, 2, y + 2);
            }
            ftr("Fn+;. elegir  Enter=usar  `=salir");
            push();
            need = false; last_count = g_node_count;
        }
        pump_rx();
        screen_toggle_tick();
        KeyEvent ev = keyboard_read();
        if (ev.type == Key::None) { delay(15); continue; }
        need = true;
        if (ev.type == Key::Escape) return;
        else if (ev.type == Key::Up)   { if (sel > 0) sel--; }
        else if (ev.type == Key::Down) { if (sel < total - 1) sel++; }
        else if (ev.type == Key::Enter) {
            if (sel < g_node_count) {
                strncpy(sender, g_nodes[sel].name, sizeof(sender));
                sender[sizeof(sender) - 1] = 0;
            } else {
                if (edit_text(sender, sizeof(sender), "CoreImpersonator",
                              "Nombre a suplantar:", "Enter=ok  `=cancelar") != 1) return;
            }
            if (sender[0] == 0) { continue; }      // nombre vacio: volver a la lista
            break;
        }
        if (sel < top) top = sel;
        if (sel >= top + maxrows) top = sel - maxrows + 1;
    }

    // --- Fase 2: componer el mensaje ---
    char msg[100] = "";
    char hl[48]; snprintf(hl, sizeof(hl), "Canal Public como \"%s\"", sender);
    if (edit_text(msg, sizeof(msg), "Mensaje", hl, "Enter=enviar  `=cancelar") != 1) return;

    // --- Fase 3: enviar al canal publico con el remitente elegido ---
    uint8_t buf[PC_MAX_MTU]; uint32_t ts = time_now();
    int n = pc_build_group_text(g_pubchan, sender, msg, ts, buf);
    bool sent = false;
    if (n > 0 && radio_ok()) sent = (radio_transmit(buf, n) == 0);
    if (n <= 0)           overlay("CoreImpersonator", "error al construir", COL_DANGER);
    else if (!radio_ok()) overlay("CoreImpersonator", "construido (sin Cap LoRa)", COL_WARNING);
    else if (sent)        overlay("CoreImpersonator", "enviado", COL_OK);
    else                  overlay("CoreImpersonator", "fallo TX", COL_DANGER);
    delay(1200);
}

// ============================================================
//  [C.3]  GHOSTSWARM  -  churn automatizado de identidades efimeras
//  Ciclo (toggle on/off): nace un nodo nuevo (keypair + nombre al azar)
//  -> se anuncia por flood un par de veces -> manda un par de mensajes
//  al canal publico (espaciados 5-20 s al azar) -> se regenera y repite.
// ============================================================
static const char* GS_MSGS[] = {
    "hola", "alguien por aca?", "probando", "buenas",
    "test", "qrv?", "ping", "aca andamos",
};
static const uint8_t GS_MSG_POOL = sizeof(GS_MSGS) / sizeof(GS_MSGS[0]);

#define GS_ADV_COUNT   2       // adverts por ciclo (burst)
#define GS_ADV_GAP_MS  1500    // separacion entre adverts del burst
#define GS_MSG_COUNT   2       // mensajes por ciclo
#define GS_MSG_MIN_MS  5000    // delay aleatorio entre envios de mensaje
#define GS_MSG_MAX_MS  20000

static uint32_t gs_rand_delay() {
    return GS_MSG_MIN_MS + (esp_random() % (GS_MSG_MAX_MS - GS_MSG_MIN_MS + 1));
}

static void draw_ghostswarm(bool running, const char* name, uint8_t prefix, uint8_t phase,
                            uint32_t cycles, uint32_t adv, uint32_t msg, int32_t next_in_s) {
    canvas.fillSprite(COL_BG);
    hdr("GhostSwarm"); radio_badge();
    canvas.setFont(&fonts::Font0); canvas.setTextSize(1); canvas.setTextDatum(ML_DATUM);

    canvas.setTextColor(running ? COL_DANGER : COL_TEXT_DIM);
    canvas.drawString(running ? "[ CHURN ACTIVO ]" : "detenido", 6, UI_CONTENT_Y + 6);

    char b[48];
    canvas.setTextColor(COL_TEXT);
    snprintf(b, sizeof(b), "nodo: %s (%02X)", name, prefix);
    canvas.drawString(b, 6, UI_CONTENT_Y + 22);

    canvas.setTextColor(COL_ACCENT);
    snprintf(b, sizeof(b), "fase: %s", phase == 0 ? "announce" : "mensajes");
    canvas.drawString(b, 6, UI_CONTENT_Y + 36);
    if (running && next_in_s >= 0) {
        canvas.setTextColor(COL_WARNING);
        snprintf(b, sizeof(b), "proximo envio en %lds", (long)next_in_s);
        canvas.drawString(b, 6, UI_CONTENT_Y + 50);
    }

    canvas.setTextColor(COL_OK);
    snprintf(b, sizeof(b), "ciclos:%lu adv:%lu msg:%lu",
             (unsigned long)cycles, (unsigned long)adv, (unsigned long)msg);
    canvas.drawString(b, 6, UI_CONTENT_Y + 64);

    if (!radio_ok()) ftr("SIN Cap LoRa: simulado. Enter=on/off  `=salir", COL_WARNING);
    else             ftr("Enter=on/off  `=salir");
    push();
}

static void screen_ghostswarm() {
    bool running = false;
    LocalIdentity id;
    char name[16] = "-";
    uint8_t phase = 0;        // 0=announce, 1=mensajes
    uint8_t idx = 0;
    uint32_t next_ms = 0;
    uint32_t cycles = 0, adv_sent = 0, msg_sent = 0;
    bool need = true;
    uint32_t last_draw = 0;

    while (true) {
        // --- motor del ciclo ---
        if (running && (int32_t)(millis() - next_ms) >= 0) {
            uint8_t buf[PC_MAX_MTU]; int n = 0; uint32_t ts = time_now();
            if (phase == 0) {                                   // ANNOUNCE burst
                n = pc_build_advert(id, ADV_TYPE_CHAT, name, false, 0, 0, ts, buf);
                if (n > 0) { if (radio_ok()) radio_transmit(buf, n); adv_sent++; }
                idx++;
                if (idx < GS_ADV_COUNT) next_ms = millis() + GS_ADV_GAP_MS;
                else { phase = 1; idx = 0; next_ms = millis() + gs_rand_delay(); }
            } else {                                            // MENSAJES
                const char* m = GS_MSGS[esp_random() % GS_MSG_POOL];
                n = pc_build_group_text(g_pubchan, name, m, ts, buf);
                if (n > 0) { if (radio_ok()) radio_transmit(buf, n); msg_sent++; }
                idx++;
                if (idx < GS_MSG_COUNT) {
                    next_ms = millis() + gs_rand_delay();
                } else {                                        // fin de ciclo -> regenera
                    cycles++;
                    id.generate(); rnd_name(name, sizeof(name));
                    phase = 0; idx = 0; next_ms = millis();     // nuevo nodo anuncia ya
                }
            }
            need = true;
        }

        // --- dibujo (con cuenta regresiva) ---
        if (need || millis() - last_draw > 500) {
            int32_t diff = (int32_t)(next_ms - millis());
            int32_t next_in = running ? (diff > 0 ? diff / 1000 : 0) : -1;
            draw_ghostswarm(running, name, running ? id.pub[0] : 0,
                            phase, cycles, adv_sent, msg_sent, next_in);
            need = false; last_draw = millis();
        }

        screen_toggle_tick();
        KeyEvent ev = keyboard_read();
        if (ev.type == Key::Escape) { if (running) { running = false; need = true; } else return; }
        else if (ev.type == Key::Enter) {
            running = !running;
            if (running) {                                      // arranca: primer nodo
                cycles = adv_sent = msg_sent = 0;
                id.generate(); rnd_name(name, sizeof(name));
                phase = 0; idx = 0; next_ms = millis();
            } else {
                strncpy(name, "-", sizeof(name));
            }
            need = true;
        }
        delay(10);
    }
}

// ============================================================
//  [C.3c]  DARKMIRROR  -  espejo del canal publico con timestamp vivo
//  Toggle on/off. Fase ESCUCHA (30s): sniffea GRP_TXT del canal publico, los
//  descifra con la PSK conocida y guarda (sender, texto) en RAM (ring, max
//  DM_CAP). Fase REPLAY (30s): reconstruye uno al azar con time_now() cada
//  DM_GAP_MS -> timestamp ACTUAL + ciphertext nuevo = se muestra como mensaje
//  vivo del emisor real y esquiva el dedup de MeshCore. Luego reinicia.
//  Solo canal publico: DMs y canales sin PSK no se pueden descifrar.
// ============================================================
#define DM_CAP        200
#define DM_LISTEN_MS  30000
#define DM_REPLAY_MS  30000
#define DM_GAP_MS     5000

struct DMMsg { char sender[20]; char text[120]; };
static DMMsg g_dm[DM_CAP];
static int   g_dm_count = 0;   // mensajes validos (hasta DM_CAP)
static int   g_dm_head  = 0;   // proxima posicion de escritura (ring)

static void dm_store(const char* sender, const char* text) {
    DMMsg* m = &g_dm[g_dm_head];
    strncpy(m->sender, sender, sizeof(m->sender)); m->sender[sizeof(m->sender) - 1] = 0;
    strncpy(m->text,   text,   sizeof(m->text));   m->text[sizeof(m->text) - 1]     = 0;
    g_dm_head = (g_dm_head + 1) % DM_CAP;
    if (g_dm_count < DM_CAP) g_dm_count++;
}

// Intenta capturar un GRP_TXT del canal publico: descifra y separa "sender: texto".
static bool dm_capture(const uint8_t* data, int len) {
    if (len < 2) return false;
    if (((data[0] >> PH_TYPE_SHIFT) & 0x0F) != PAYLOAD_TYPE_GRP_TXT) return false;
    uint8_t plb = data[1];
    int off = 2 + (plb & 63) * ((plb >> 6) + 1);          // salta el path
    if (len < off + 1 + PC_CIPHER_MAC_SIZE) return false;
    if (data[off] != g_pubchan.hash) return false;        // no es nuestro canal
    uint8_t plain[192];
    int plen = pc::MACThenDecrypt(g_pubchan.secret, plain, &data[off + 1], len - (off + 1));
    if (plen <= 5 || plain[4] != 0) return false;         // MAC malo / no es texto plano
    // El mensaje descifrado trae su propio ts real ([ts 4] al inicio) -> sincroniza
    // el reloj con la hora de la malla SIN depender de adverts. Asi el replay sale
    // con timestamp vivo aunque en la escucha no haya entrado ningun advert.
    uint32_t msg_ts = 0;
    memcpy(&msg_ts, plain, 4);
    pc_clock_observe(msg_ts);
    if (plen >= (int)sizeof(plain)) plen = sizeof(plain) - 1;
    plain[plen] = 0;
    char* body = (char*)&plain[5];                         // "sender: texto" (+ padding cero)
    char sender[20] = "", text[120] = "";
    char* sep = strstr(body, ": ");
    if (sep) {
        int sn = sep - body; if (sn > (int)sizeof(sender) - 1) sn = sizeof(sender) - 1;
        memcpy(sender, body, sn); sender[sn] = 0;
        strncpy(text, sep + 2, sizeof(text) - 1); text[sizeof(text) - 1] = 0;
    } else {
        strncpy(text, body, sizeof(text) - 1); text[sizeof(text) - 1] = 0;
    }
    if (sender[0] == 0 && text[0] == 0) return false;
    dm_store(sender, text);
    return true;
}

static void draw_darkmirror(bool running, uint8_t phase, int32_t secs_left,
                            uint32_t replays, int last_idx) {
    canvas.fillSprite(COL_BG);
    hdr("DarkMirror"); radio_badge();
    canvas.setFont(&fonts::Font0); canvas.setTextSize(1); canvas.setTextDatum(ML_DATUM);

    canvas.setTextColor(running ? COL_DANGER : COL_TEXT_DIM);
    canvas.drawString(running ? "[ ESPEJO ACTIVO ]" : "detenido", 6, UI_CONTENT_Y + 6);

    char b[48];
    canvas.setTextColor(COL_ACCENT);
    snprintf(b, sizeof(b), "fase: %s", phase == 0 ? "escuchando" : "replay (ts vivo)");
    canvas.drawString(b, 6, UI_CONTENT_Y + 20);

    if (running && secs_left >= 0) {
        canvas.setTextColor(COL_WARNING);
        snprintf(b, sizeof(b), "resta %lds", (long)secs_left);
        canvas.drawString(b, 6, UI_CONTENT_Y + 34);
    }

    canvas.setTextColor(COL_OK);
    snprintf(b, sizeof(b), "buffer:%d/%d  replays:%lu", g_dm_count, DM_CAP, (unsigned long)replays);
    canvas.drawString(b, 6, UI_CONTENT_Y + 48);

    canvas.setTextColor(COL_TEXT_DIM);
    if (last_idx >= 0 && last_idx < g_dm_count)
        snprintf(b, sizeof(b), "ult: %s: %.20s", g_dm[last_idx].sender, g_dm[last_idx].text);
    else
        snprintf(b, sizeof(b), "solo canal publico");
    canvas.drawString(b, 6, UI_CONTENT_Y + 62);

    if (!radio_ok()) ftr("SIN Cap LoRa: simulado. Enter=on/off  `=salir", COL_WARNING);
    else             ftr("Enter=on/off  `=salir");
    push();
}

static void screen_darkmirror() {
    if (radio_ok()) radio_start_receive();
    bool running = false;
    uint8_t phase = 0;                 // 0=escucha, 1=replay
    uint32_t phase_end = 0, next_tx = 0, replays = 0;
    int last_idx = -1;
    bool need = true; uint32_t last_draw = 0;

    while (true) {
        uint32_t now = millis();

        // --- captura (solo en fase escucha) ---
        if (running && phase == 0 && radio_rx_available()) {
            uint8_t rb[PC_MAX_MTU]; int r = 0;
            int n = radio_read(rb, sizeof(rb), &r);
            if (n >= 2) {
                pc_sniffer_process(rb, n, r);          // mantiene node table + reloj (ts vivo)
                if (dm_capture(rb, n)) need = true;
            }
        }

        // --- maquina de fases ---
        if (running) {
            if (now >= phase_end) {
                if (phase == 0) { phase = 1; phase_end = now + DM_REPLAY_MS; next_tx = now; }
                else            { phase = 0; phase_end = now + DM_LISTEN_MS; if (radio_ok()) radio_start_receive(); }
                need = true;
            }
            if (phase == 1 && now >= next_tx) {
                if (g_dm_count > 0) {
                    int idx = (int)(esp_random() % (uint32_t)g_dm_count);
                    uint8_t buf[PC_MAX_MTU];
                    int n = pc_build_group_text(g_pubchan, g_dm[idx].sender, g_dm[idx].text, time_now(), buf);
                    if (n > 0 && radio_ok()) radio_transmit(buf, n);
                    if (radio_ok()) radio_start_receive();
                    last_idx = idx; replays++;
                }
                next_tx = now + DM_GAP_MS;
                need = true;
            }
        }

        // --- dibujo ---
        if (need || now - last_draw > 500) {
            int32_t secs = running ? (int32_t)((phase_end > now ? phase_end - now : 0) / 1000) : -1;
            draw_darkmirror(running, phase, secs, replays, last_idx);
            need = false; last_draw = now;
        }

        screen_toggle_tick();
        KeyEvent ev = keyboard_read();
        if (ev.type == Key::Escape) { if (running) { running = false; need = true; } else return; }
        else if (ev.type == Key::Enter) {
            running = !running;
            if (running) {
                phase = 0; phase_end = millis() + DM_LISTEN_MS; next_tx = 0;
                replays = 0; last_idx = -1;
                if (radio_ok()) radio_start_receive();
            }
            need = true;
        }
        delay(5);
    }
}

// ============================================================
//  [C.4]  REGENERAR
// ============================================================
static void draw_regen(const char* name, const LocalIdentity& id) {
    canvas.fillSprite(COL_BG);
    hdr("Regenerar"); radio_badge();
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextSize(1);
    int y0 = UI_CONTENT_Y + 10;

    canvas.setTextDatum(ML_DATUM);
    canvas.setTextColor(COL_ACCENT);
    canvas.drawString("Nombre:", 6, y0);
    canvas.setTextColor(COL_TEXT);
    char nb[28]; snprintf(nb, sizeof(nb), "%s_", name);   // cursor siempre visible
    canvas.drawString(nb, 80, y0);

    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(id.valid ? COL_OK : COL_TEXT_DIM);
    char pb[44];
    snprintf(pb, sizeof(pb), "pub nueva: %02X %02X %02X ... (prefijo %02X)",
             id.pub[0], id.pub[1], id.pub[2], id.pub[0]);
    canvas.drawString(pb, 6, y0 + 40);

    canvas.setTextColor(COL_WARNING);
    canvas.drawString("Enter descarta la identidad actual.", 6, y0 + 56);

    ftr("texto=editar nombre  Enter=confirmar  `=cancelar");
    push();
}

static void screen_regen() {
    // Genera un par Ed25519 nuevo e invita a (re)escribir el nombre. Todo en
    // copias locales: solo se compromete al confirmar (Enter). Escape no toca
    // ni la identidad ni el nombre actuales.
    LocalIdentity id; id.generate();
    char name[sizeof(g_node_name)];
    rnd_name(name, sizeof(name));                    // sugerencia, editable
    uint8_t clen = strlen(name);
    bool need = true;

    while (true) {
        if (need) { draw_regen(name, id); need = false; }
        screen_toggle_tick();
        KeyEvent ev = keyboard_read();
        if (ev.type == Key::None) { delay(15); continue; }
        need = true;
        if (ev.type == Key::Escape) return;          // cancela sin cambios
        else if (ev.type == Key::Backspace) {
            if (clen > 0) { name[--clen] = 0; }
        }
        else if (ev.type == Key::Char) {
            if (clen < sizeof(name) - 1) { name[clen++] = ev.ch; name[clen] = 0; }
        }
        else if (ev.type == Key::Enter) {
            if (clen == 0) { rnd_name(name, sizeof(name)); }   // nombre no vacio
            strncpy(g_node_name, name, sizeof(g_node_name));
            g_node_name[sizeof(g_node_name) - 1] = 0;
            g_identity = id;                          // recien aca se compromete
            char b[48]; snprintf(b, sizeof(b), "%s  (%02X)", g_node_name, g_identity.pub[0]);
            overlay("Regenerar", b, COL_OK);
            delay(1200);
            return;
        }
    }
}

// ============================================================
//  [C.5]  RECON / SNIFFER
// ============================================================
static void draw_recon() {
    canvas.fillSprite(COL_BG);
    hdr("Recon / Sniffer"); radio_badge();
    canvas.setFont(&fonts::Font0); canvas.setTextSize(1); canvas.setTextDatum(ML_DATUM);
    if (!radio_ok()) {
        canvas.setTextColor(COL_WARNING);
        canvas.drawString("RADIO N/A: conecta el Cap LoRa", 6, LCD_HEIGHT / 2);
        ftr("`=salir"); push(); return;
    }
    if (g_node_count == 0) {
        canvas.setTextColor(COL_TEXT_DIM);
        canvas.drawString("escuchando adverts...", 6, LCD_HEIGHT / 2);
    } else {
        int rowH = 12, startY = UI_CONTENT_Y;
        int maxrows = UI_CONTENT_H / rowH;
        for (int i = 0; i < g_node_count && i < maxrows; i++) {
            int y = startY + i * rowH;
            char b[48];
            snprintf(b, sizeof(b), "%02X %-12s %s %ddBm",
                     g_nodes[i].prefix, g_nodes[i].name, adv_type_name(g_nodes[i].type), g_nodes[i].rssi);
            canvas.setTextColor(COL_TEXT);
            canvas.drawString(b, 4, y + rowH / 2);
        }
    }
    char cb[48], fb[64];
    fmt_clock(cb, sizeof(cb));
    snprintf(fb, sizeof(fb), "%s  `=salir", cb);
    ftr(fb, pc_clock_synced() ? COL_OK : COL_WARNING);
    push();
}

static void screen_recon() {
    if (radio_ok()) radio_start_receive();
    uint32_t last = 0; int last_count = -1;
    while (true) {
        pump_rx();
        if ((int)g_node_count != last_count || millis() - last > 800) {
            draw_recon(); last = millis(); last_count = g_node_count;
        }
        screen_toggle_tick();
        KeyEvent ev = keyboard_read();
        if (ev.type == Key::Escape) return;
        delay(20);
    }
}

// ============================================================
//  SETUP / LOOP
// ============================================================
void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    Serial.begin(115200);
    delay(200);
    Wire.begin(I2C_SDA, I2C_SCL);
    screen_toggle_init();

    M5.Display.setRotation(1);
    M5.Display.setBrightness(160);

    // Sprite de doble buffer para toda la UI de menus
    canvas.createSprite(LCD_WIDTH, LCD_HEIGHT);
    canvas.setTextDatum(MC_DATUM);

    Serial.println("[PoisonCore] boot");

    splash();

    pc_channel_from_psk(PUBLIC_GROUP_PSK, "Public", g_pubchan);

    screen_config_rf();
    radio_init();          // tolerante: si no hay Cap LoRa, sigue
    screen_identity();
}

void loop() {
    uint8_t sel = screen_main_menu();
    switch (sel) {
        case 0: screen_flood();        break;
        case 1: screen_messages();     break;
        case 2: screen_impersonator(); break;
        case 3: screen_ghostswarm();   break;
        case 4: screen_darkmirror();   break;
        case 5: screen_regen();        break;
        case 6: screen_recon();        break;
        case 7: screen_config_rf(); radio_apply_config(); break;
    }
}
