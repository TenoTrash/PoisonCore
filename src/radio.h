#pragma once
// ============================================================
//  radio.h  -  Motor de radio (SX1262 + Cap LoRa-1262)
//  MeshCore: sync word 0x12 (RADIOLIB_SX126X_SYNC_WORD_PRIVATE)
//  Tolerante: si el modulo no esta, radio_ok()==false y el resto
//  de la UI funciona igual (para probar boot sin Cap LoRa).
// ============================================================
#include <stdint.h>
#include <stddef.h>

// Inicializa segun g_rf. Devuelve true si el SX1262 respondio.
bool  radio_init();
bool  radio_ok();
void  radio_apply_config();                 // re-aplica g_rf sin reinit

int   radio_transmit(const uint8_t* data, size_t len);   // 0 = OK, <0 = error
bool  radio_start_receive();
bool  radio_rx_available();
// Lee el paquete pendiente. Devuelve longitud (>0) o <=0 si no hay/err.
int   radio_read(uint8_t* buf, size_t maxlen, int* rssi);
