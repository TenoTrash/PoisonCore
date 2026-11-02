// ============================================================
//  pc_identity.cpp  -  Ed25519 keypair / sign / ECDH
// ============================================================
#include "pc_identity.h"
#include <Arduino.h>
#include <esp_random.h>

// Lib bundle (orlp/ed25519), portada de MeshCore/lib/ed25519.
// ED25519_NO_SEED se define como build flag (-D) para toda la compilacion,
// de modo que seed.c (usa /dev/urandom) queda vacio. El seed lo genera
// generate() con esp_random().
extern "C" {
  #include <ed_25519.h>
}

void LocalIdentity::generate() {
    uint8_t seed[32];
    do {
        for (int i = 0; i < 32; i += 4) {
            uint32_t r = esp_random();
            memcpy(&seed[i], &r, 4);
        }
        ed25519_create_keypair(pub, prv, seed);
        // MeshCore rechaza pub prefijado con 0x00 o 0xFF; si cae, re-genera.
    } while (pub[0] == 0x00 || pub[0] == 0xFF);
    valid = true;
}

void LocalIdentity::sign(uint8_t* sig, const uint8_t* msg, int msg_len) const {
    ed25519_sign(sig, msg, (size_t)msg_len, pub, prv);
}

void LocalIdentity::calcSharedSecret(uint8_t* secret, const uint8_t* other_pub) const {
    ed25519_key_exchange(secret, other_pub, prv);
}
