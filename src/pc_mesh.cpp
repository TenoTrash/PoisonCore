// ============================================================
//  pc_mesh.cpp  -  Framing MeshCore + sniffer
// ============================================================
#include "pc_mesh.h"
#include "pc_crypto.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

// Mascaras de flags del app_data del advert
#define ADV_LATLON_MASK  0x10
#define ADV_NAME_MASK     0x80

SeenNode g_nodes[PC_MAX_NODES];
int      g_node_count = 0;

// ---- Reloj derivado de la malla -----------------------------------------
// Antes de sincronizar: base_epoch + millis()/1000 (mismo fallback de siempre:
// 2025-01-01 UTC). Al ver el ts de un advert se re-ancla a esa hora real.
static uint32_t s_clock_base_epoch  = 1735689600UL;  // 2025-01-01 00:00 UTC
static uint32_t s_clock_base_millis = 0;
static bool     s_clock_synced      = false;

uint32_t pc_clock_now() {
    return s_clock_base_epoch + (uint32_t)((millis() - s_clock_base_millis) / 1000UL);
}

bool pc_clock_synced() { return s_clock_synced; }

void pc_clock_observe(uint32_t seen_epoch) {
    // sanity: descarta basura (fuera de 2024-01-01 .. 2035-01-01)
    if (seen_epoch < 1704067200UL || seen_epoch > 2051222400UL) return;
    // adopta solo si adelanta el estimado actual (o si es la primera vez) ->
    // nunca retrocede, y un advert viejo/replay no arrastra el reloj atras.
    if (!s_clock_synced || seen_epoch > pc_clock_now()) {
        s_clock_base_epoch  = seen_epoch;
        s_clock_base_millis = millis();
        s_clock_synced      = true;
    }
}

// Nombre valido para advert (MeshCore rechaza estos caracteres)
static bool valid_name_char(char c) {
    return !(c == '[' || c == ']' || c == '\\' || c == ':' ||
             c == ',' || c == '?' || c == '*');
}

// --- app_data del advert (port de AdvertDataBuilder::encodeTo) ---
static int encode_advert_app_data(uint8_t adv_type, const char* name,
                                  bool has_loc, int32_t lat, int32_t lon,
                                  uint8_t* app) {
    app[0] = adv_type;
    int i = 1;
    if (has_loc) {
        app[0] |= ADV_LATLON_MASK;
        memcpy(&app[i], &lat, 4); i += 4;
        memcpy(&app[i], &lon, 4); i += 4;
    }
    if (name && *name) {
        int maxn = PC_MAX_ADVERT_DATA - i;
        int nlen = 0;
        while (name[nlen] && nlen < maxn && valid_name_char(name[nlen])) nlen++;
        if (nlen > 0) {
            app[0] |= ADV_NAME_MASK;
            memcpy(&app[i], name, nlen);
            i += nlen;
        }
    }
    return i;
}

int pc_build_advert(const LocalIdentity& id, uint8_t adv_type, const char* name,
                    bool has_loc, int32_t lat_e6, int32_t lon_e6,
                    uint32_t timestamp, uint8_t* out) {
    if (!id.valid) return 0;

    uint8_t app[PC_MAX_ADVERT_DATA];
    int app_len = encode_advert_app_data(adv_type, name, has_loc, lat_e6, lon_e6, app);

    // payload = [pub 32][ts 4][sig 64][app_data]
    uint8_t payload[PC_MAX_PACKET_PAYLOAD];
    int p = 0;
    memcpy(&payload[p], id.pub, PC_PUB_KEY_SIZE); p += PC_PUB_KEY_SIZE;
    memcpy(&payload[p], &timestamp, 4); p += 4;
    uint8_t* sig = &payload[p]; p += PC_SIGNATURE_SIZE;
    memcpy(&payload[p], app, app_len); p += app_len;

    // firma sobre pub || ts || app_data
    uint8_t msg[PC_PUB_KEY_SIZE + 4 + PC_MAX_ADVERT_DATA];
    int m = 0;
    memcpy(&msg[m], id.pub, PC_PUB_KEY_SIZE); m += PC_PUB_KEY_SIZE;
    memcpy(&msg[m], &timestamp, 4); m += 4;
    memcpy(&msg[m], app, app_len); m += app_len;
    id.sign(sig, msg, m);

    // wire = [header][path_len=0][payload]
    int w = 0;
    out[w++] = (PAYLOAD_TYPE_ADVERT << PH_TYPE_SHIFT) | ROUTE_TYPE_FLOOD;  // 0x11
    out[w++] = 0x00;  // path_len (0 saltos)
    memcpy(&out[w], payload, p); w += p;
    return w;
}

int pc_build_group_text(const GroupChannel& ch, const char* sender,
                        const char* text, uint32_t timestamp, uint8_t* out) {
    if (!ch.valid) return 0;

    // data = [ts 4][txt_type=0 1]["sender: text"]
    uint8_t data[PC_MAX_PACKET_PAYLOAD];
    int d = 0;
    memcpy(&data[d], &timestamp, 4); d += 4;
    data[d++] = 0;  // TXT_TYPE_PLAIN
    int avail = (int)sizeof(data) - d - 1;
    int pre = snprintf((char*)&data[d], avail, "%s: ", sender ? sender : "");
    if (pre < 0) pre = 0;
    if (pre > avail) pre = avail;
    d += pre;
    int tlen = text ? (int)strlen(text) : 0;
    if (tlen > (int)sizeof(data) - d) tlen = (int)sizeof(data) - d;
    memcpy(&data[d], text, tlen); d += tlen;

    // payload = [channel.hash 1][encryptThenMAC(secret, data)]
    uint8_t payload[PC_MAX_PACKET_PAYLOAD];
    int pl = 0;
    payload[pl++] = ch.hash;
    int enc = pc::encryptThenMAC(ch.secret, &payload[pl], data, d);
    pl += enc;

    int w = 0;
    out[w++] = (PAYLOAD_TYPE_GRP_TXT << PH_TYPE_SHIFT) | ROUTE_TYPE_FLOOD;  // 0x15
    out[w++] = 0x00;
    memcpy(&out[w], payload, pl); w += pl;
    return w;
}

int pc_build_dm(const LocalIdentity& self, const uint8_t* dest_pub,
                const char* text, uint32_t timestamp, uint8_t* out) {
    if (!self.valid) return 0;

    uint8_t secret[PC_PUB_KEY_SIZE];
    self.calcSharedSecret(secret, dest_pub);

    // data = [ts 4][attempt 1][text\0]
    uint8_t data[PC_MAX_PACKET_PAYLOAD];
    int d = 0;
    memcpy(&data[d], &timestamp, 4); d += 4;
    data[d++] = 0;  // attempt
    int tlen = text ? (int)strlen(text) : 0;
    if (tlen > (int)sizeof(data) - d - 1) tlen = (int)sizeof(data) - d - 1;
    memcpy(&data[d], text, tlen); d += tlen;
    data[d++] = 0;  // null terminator (parte del payload cifrado)

    // payload = [dest_hash 1][src_hash 1][encryptThenMAC(secret, data)]
    uint8_t payload[PC_MAX_PACKET_PAYLOAD];
    int pl = 0;
    payload[pl++] = dest_pub[0];     // PATH_HASH_SIZE = 1
    payload[pl++] = self.pub[0];
    int enc = pc::encryptThenMAC(secret, &payload[pl], data, d);
    pl += enc;

    int w = 0;
    out[w++] = (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT) | ROUTE_TYPE_FLOOD;  // 0x09
    out[w++] = 0x00;
    memcpy(&out[w], payload, pl); w += pl;
    return w;
}

bool pc_channel_from_psk(const char* psk_b64, const char* name, GroupChannel& out) {
    memset(out.secret, 0, sizeof(out.secret));
    int n = pc::base64_decode(psk_b64, out.secret, PC_PUB_KEY_SIZE);
    if (n != 16 && n != 32) return false;   // 128-bit o 256-bit
    uint8_t h[PC_HASH_SIZE];
    pc::sha256(h, PC_HASH_SIZE, out.secret, n);
    out.hash = h[0];                         // PATH_HASH_SIZE = 1
    strncpy(out.name, name ? name : "", sizeof(out.name) - 1);
    out.name[sizeof(out.name) - 1] = 0;
    out.valid = true;
    return true;
}

// ============================================================
//  Sniffer
// ============================================================
void pc_sniffer_reset() { g_node_count = 0; }

static SeenNode* find_or_alloc(const uint8_t* pub) {
    for (int i = 0; i < g_node_count; i++) {
        if (memcmp(g_nodes[i].pub, pub, PC_PUB_KEY_SIZE) == 0) return &g_nodes[i];
    }
    if (g_node_count < PC_MAX_NODES) return &g_nodes[g_node_count++];
    // tabla llena: reemplaza el mas viejo
    int oldest = 0;
    for (int i = 1; i < g_node_count; i++)
        if (g_nodes[i].last_ms < g_nodes[oldest].last_ms) oldest = i;
    return &g_nodes[oldest];
}

void pc_sniffer_process(const uint8_t* data, int len, int rssi) {
    if (len < 2) return;
    uint8_t header = data[0];
    uint8_t ptype  = (header >> PH_TYPE_SHIFT) & 0x0F;
    if (ptype != PAYLOAD_TYPE_ADVERT) return;   // solo adverts (por ahora)

    uint8_t path_len_byte = data[1];
    uint8_t hop_count = path_len_byte & 63;
    uint8_t hash_size = (path_len_byte >> 6) + 1;
    int path_bytes = hop_count * hash_size;

    int off = 2 + path_bytes;
    // payload = [pub 32][ts 4][sig 64][app_data]
    int need = off + PC_PUB_KEY_SIZE + 4 + PC_SIGNATURE_SIZE;
    if (len < need + 1) return;  // al menos 1 byte de app_data (flags)

    const uint8_t* pub = &data[off];
    const uint8_t* app = &data[off + PC_PUB_KEY_SIZE + 4 + PC_SIGNATURE_SIZE];
    int app_len = len - (off + PC_PUB_KEY_SIZE + 4 + PC_SIGNATURE_SIZE);
    if (app_len < 1) return;

    // El ts del advert va en texto plano (little-endian, como lo arma el builder)
    // -> engancha el reloj a la hora real de la malla (la ADV no tiene RTC).
    uint32_t adv_ts = 0;
    memcpy(&adv_ts, &data[off + PC_PUB_KEY_SIZE], 4);
    pc_clock_observe(adv_ts);

    uint8_t flags = app[0];
    uint8_t adv_type = flags & 0x0F;

    // extraer nombre (saltando latlon si esta)
    int ai = 1;
    if (flags & ADV_LATLON_MASK) ai += 8;
    char name[20] = {0};
    if ((flags & ADV_NAME_MASK) && ai < app_len) {
        int nlen = app_len - ai;
        if (nlen > (int)sizeof(name) - 1) nlen = sizeof(name) - 1;
        memcpy(name, &app[ai], nlen);
        name[nlen] = 0;
    }

    SeenNode* n = find_or_alloc(pub);
    memcpy(n->pub, pub, PC_PUB_KEY_SIZE);
    n->has_pub = true;
    n->prefix  = pub[0];
    strncpy(n->name, name, sizeof(n->name) - 1);
    n->name[sizeof(n->name) - 1] = 0;
    n->type    = adv_type;
    n->rssi    = rssi;
    n->last_ms = millis();
}
