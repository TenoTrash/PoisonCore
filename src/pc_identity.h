#pragma once
// ============================================================
//  pc_identity.h  -  Identidad Ed25519 (port de MeshCore Identity)
//  La identidad de un nodo MeshCore ES su clave publica Ed25519.
// ============================================================
#include <stdint.h>
#include "pc_crypto.h"

struct LocalIdentity {
    uint8_t pub[PC_PUB_KEY_SIZE];   // 32
    uint8_t prv[PC_PRV_KEY_SIZE];   // 64
    bool    valid = false;

    // Genera un par nuevo con seed de esp_random(). Rechaza pub[0] en {0x00,0xFF}.
    void generate();

    // Firma Ed25519 sobre 'msg'. sig debe ser PC_SIGNATURE_SIZE (64).
    void sign(uint8_t* sig, const uint8_t* msg, int msg_len) const;

    // ECDH (Ed25519 -> Curve25519). secret debe ser PC_PUB_KEY_SIZE (32).
    void calcSharedSecret(uint8_t* secret, const uint8_t* other_pub) const;
};
