#pragma once
// ============================================================
//  pc_crypto.h  -  Capa de cifrado (port de MeshCore Utils.cpp)
//  AES-128-ECB + HMAC-SHA256 truncado a 2 bytes.
// ============================================================
#include <stdint.h>
#include <stddef.h>

#define PC_PUB_KEY_SIZE     32
#define PC_PRV_KEY_SIZE     64
#define PC_SIGNATURE_SIZE   64
#define PC_CIPHER_KEY_SIZE  16   // AES-128
#define PC_CIPHER_BLOCK     16
#define PC_CIPHER_MAC_SIZE   2
#define PC_HASH_SIZE         8   // hash de deduplicacion de paquete (MeshCore MAX_HASH_SIZE)

namespace pc {

// SHA256 truncado a hash_len bytes.
void sha256(uint8_t* hash, size_t hash_len, const uint8_t* msg, int msg_len);
void sha256(uint8_t* hash, size_t hash_len,
            const uint8_t* frag1, int frag1_len,
            const uint8_t* frag2, int frag2_len);

// AES-128-ECB, relleno con ceros en el ultimo bloque. Devuelve bytes (mult. de 16).
int encrypt(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len);
int decrypt(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len);

// dest = [MAC(2)] [ciphertext]. Devuelve 2 + enc_len.
int encryptThenMAC(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len);
// Verifica MAC (bytes iniciales de src) y descifra. 0 si MAC invalido / no alineado.
int MACThenDecrypt(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len);

// Decodifica base64 (para la PSK del canal). Devuelve bytes escritos en out, o 0.
int base64_decode(const char* b64, uint8_t* out, int out_cap);

} // namespace pc
