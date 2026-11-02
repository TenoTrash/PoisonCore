// ============================================================
//  pc_crypto.cpp  -  port de la capa de cifrado de MeshCore
// ============================================================
#include "pc_crypto.h"
#include <string.h>
#include <AES.h>       // rweather/Crypto
#include <SHA256.h>    // rweather/Crypto

namespace pc {

void sha256(uint8_t* hash, size_t hash_len, const uint8_t* msg, int msg_len) {
    SHA256 sha;
    sha.update(msg, msg_len);
    sha.finalize(hash, hash_len);
}

void sha256(uint8_t* hash, size_t hash_len,
            const uint8_t* frag1, int frag1_len,
            const uint8_t* frag2, int frag2_len) {
    SHA256 sha;
    sha.update(frag1, frag1_len);
    sha.update(frag2, frag2_len);
    sha.finalize(hash, hash_len);
}

int encrypt(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len) {
    AES128 aes;
    uint8_t* dp = dest;
    aes.setKey(shared_secret, PC_CIPHER_KEY_SIZE);
    while (src_len >= 16) {
        aes.encryptBlock(dp, src);
        dp += 16; src += 16; src_len -= 16;
    }
    if (src_len > 0) {              // bloque parcial restante -> pad con ceros
        uint8_t tmp[16];
        memset(tmp, 0, 16);
        memcpy(tmp, src, src_len);
        aes.encryptBlock(dp, tmp);
        dp += 16;
    }
    return dp - dest;               // siempre multiplo de 16
}

int decrypt(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len) {
    AES128 aes;
    uint8_t* dp = dest;
    aes.setKey(shared_secret, PC_CIPHER_KEY_SIZE);
    while (src_len >= 16) {
        aes.decryptBlock(dp, src);
        dp += 16; src += 16; src_len -= 16;
    }
    return dp - dest;
}

int encryptThenMAC(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len) {
    int enc_len = encrypt(shared_secret, dest + PC_CIPHER_MAC_SIZE, src, src_len);

    SHA256 sha;
    sha.resetHMAC(shared_secret, PC_PUB_KEY_SIZE);
    sha.update(dest + PC_CIPHER_MAC_SIZE, enc_len);
    sha.finalizeHMAC(shared_secret, PC_PUB_KEY_SIZE, dest, PC_CIPHER_MAC_SIZE);

    return PC_CIPHER_MAC_SIZE + enc_len;
}

int MACThenDecrypt(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len) {
    if (src_len <= PC_CIPHER_MAC_SIZE) return 0;
    int enc_len = src_len - PC_CIPHER_MAC_SIZE;
    if (enc_len % PC_CIPHER_BLOCK != 0) return 0;

    uint8_t hmac[PC_CIPHER_MAC_SIZE];
    SHA256 sha;
    sha.resetHMAC(shared_secret, PC_PUB_KEY_SIZE);
    sha.update(src + PC_CIPHER_MAC_SIZE, enc_len);
    sha.finalizeHMAC(shared_secret, PC_PUB_KEY_SIZE, hmac, PC_CIPHER_MAC_SIZE);

    if (memcmp(hmac, src, PC_CIPHER_MAC_SIZE) == 0) {
        return decrypt(shared_secret, dest, src + PC_CIPHER_MAC_SIZE, enc_len);
    }
    return 0;
}

// --- base64 decode minimal (RFC 4648, con '=' padding) ---
static int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int base64_decode(const char* b64, uint8_t* out, int out_cap) {
    int acc = 0, nbits = 0, n = 0;
    for (const char* p = b64; *p; p++) {
        if (*p == '=' || *p == '\n' || *p == '\r' || *p == ' ') continue;
        int v = b64val(*p);
        if (v < 0) return 0;        // caracter invalido
        acc = (acc << 6) | v;
        nbits += 6;
        if (nbits >= 8) {
            nbits -= 8;
            if (n >= out_cap) return 0;
            out[n++] = (uint8_t)((acc >> nbits) & 0xFF);
        }
    }
    return n;
}

} // namespace pc
