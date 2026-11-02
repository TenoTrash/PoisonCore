#pragma once
// ============================================================
//  pc_mesh.h  -  Framing de paquetes MeshCore + sniffer
//  Ver DOCUMENTACION_TECNICA_MESHCORE.md (secciones 2, 4, 6, 7).
// ============================================================
#include <stdint.h>
#include "pc_identity.h"

// ---- Header: 0bVVPPPPRR ------------------------------------
#define PH_ROUTE_MASK     0x03
#define PH_TYPE_SHIFT     2

#define ROUTE_TYPE_FLOOD          0x01
#define ROUTE_TYPE_DIRECT         0x02

#define PAYLOAD_TYPE_TXT_MSG      0x02
#define PAYLOAD_TYPE_ADVERT       0x04
#define PAYLOAD_TYPE_GRP_TXT      0x05

#define PC_MAX_PACKET_PAYLOAD     184
#define PC_MAX_ADVERT_DATA        32
#define PC_PATH_HASH_SIZE         1    // el "hash" de nodo = 1er byte de la pub_key
#define PC_MAX_MTU                256  // buffer de radio holgado

// Canal de grupo
struct GroupChannel {
    uint8_t secret[PC_PUB_KEY_SIZE];   // 32 (16 utiles + 16 cero para 128-bit)
    uint8_t hash;                      // 1er byte de SHA256(secret)
    char    name[16];
    bool    valid = false;
};

// Nodo visto por el sniffer
struct SeenNode {
    uint8_t  pub[PC_PUB_KEY_SIZE];
    bool     has_pub;
    uint8_t  prefix;     // pub[0]
    char     name[20];
    uint8_t  type;       // ADV_TYPE_*
    int      rssi;
    uint32_t last_ms;
};

#define PC_MAX_NODES 32
extern SeenNode g_nodes[PC_MAX_NODES];
extern int      g_node_count;

// ---- Builders (devuelven longitud en el aire escrita en 'out', o 0) ----

// Advert firmado (ROUTE_TYPE_FLOOD, 0 saltos).
int pc_build_advert(const LocalIdentity& id, uint8_t adv_type, const char* name,
                    bool has_loc, int32_t lat_e6, int32_t lon_e6,
                    uint32_t timestamp, uint8_t* out);

// Mensaje de grupo "sender: text" cifrado con la PSK del canal.
int pc_build_group_text(const GroupChannel& ch, const char* sender,
                        const char* text, uint32_t timestamp, uint8_t* out);

// DM cifrado con ECDH(self, dest_pub).
int pc_build_dm(const LocalIdentity& self, const uint8_t* dest_pub,
                const char* text, uint32_t timestamp, uint8_t* out);

// Carga un canal desde PSK base64 (deriva secret + hash).
bool pc_channel_from_psk(const char* psk_b64, const char* name, GroupChannel& out);

// ---- Sniffer ----
void pc_sniffer_reset();
// Procesa un paquete recibido (wire). Si es un advert valido, lo agrega al node table.
void pc_sniffer_process(const uint8_t* data, int len, int rssi);

// ---- Reloj derivado de la malla (la Cardputer ADV no tiene RTC) ----
// Arranca en un fallback y se engancha a la hora real cuando el sniffer ve el
// timestamp (texto plano) de un advert. Monotonico: nunca retrocede.
uint32_t pc_clock_now();              // epoch estimado (Unix, segundos)
void     pc_clock_observe(uint32_t seen_epoch);   // lo alimenta el sniffer
bool     pc_clock_synced();           // true si ya se engancho a un advert
