// ============================================================
//  radio.cpp  -  SX1262 via RadioLib (MeshCore-compatible)
// ============================================================
#include "radio.h"
#include "config.h"
#include <RadioLib.h>
#include <SPI.h>

// Pinout del Cap LoRa-1262 (ver config.h)
static SX1262 g_radio = new Module(LORA_NSS, LORA_IRQ, LORA_RST, LORA_BUSY);

static bool          s_ok      = false;
static bool          s_rx      = false;
static volatile bool s_rxFlag  = false;

static void IRAM_ATTR _rx_isr() { s_rxFlag = true; }

bool radio_ok() { return s_ok; }

bool radio_init() {
    s_ok = false;
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);

    int8_t txp = g_rf.txPower;
    if (txp > TX_POWER_MAX) txp = TX_POWER_MAX;

    int st = g_radio.begin(
        g_rf.freqMHz,
        g_rf.bwKHz,
        g_rf.sf,
        g_rf.cr,
        RADIOLIB_SX126X_SYNC_WORD_PRIVATE,  // 0x12  (MeshCore)
        txp,
        16,        // preamble length
        1.6f,      // TCXO del Cap LoRa-1262
        false      // DC-DC regulator
    );
    if (st != RADIOLIB_ERR_NONE) {
        Serial.printf("[Radio] begin() fallo: %d (modulo ausente?)\n", st);
        return false;   // TOLERANTE: la UI sigue funcionando
    }

    g_radio.setDio2AsRfSwitch(true);   // DIO2 controla el switch de antena
    g_radio.setPreambleLength(16);
    g_radio.setCurrentLimit(140);
    g_radio.setCRC(1);                 // MeshCore usa setCRC(1)
    g_radio.setDio1Action(_rx_isr);

    s_ok = true;
    Serial.printf("[Radio] SX1262 OK @ %.3f MHz BW%.1f SF%u CR4/%u %ddBm sync=0x12\n",
                  g_rf.freqMHz, g_rf.bwKHz, g_rf.sf, g_rf.cr, txp);
    return true;
}

void radio_apply_config() {
    if (!s_ok) return;
    g_radio.setFrequency(g_rf.freqMHz);
    g_radio.setBandwidth(g_rf.bwKHz);
    g_radio.setSpreadingFactor(g_rf.sf);
    g_radio.setCodingRate(g_rf.cr);
    int8_t txp = g_rf.txPower; if (txp > TX_POWER_MAX) txp = TX_POWER_MAX;
    g_radio.setOutputPower(txp);
}

int radio_transmit(const uint8_t* data, size_t len) {
    if (!s_ok) return -100;   // sin radio: no-op
    bool wasRx = s_rx;
    int st = g_radio.transmit(const_cast<uint8_t*>(data), len);
    if (wasRx) radio_start_receive();
    return (st == RADIOLIB_ERR_NONE) ? 0 : st;
}

bool radio_start_receive() {
    if (!s_ok) return false;
    s_rxFlag = false;
    if (g_radio.startReceive() != RADIOLIB_ERR_NONE) return false;
    s_rx = true;
    return true;
}

bool radio_rx_available() { return s_ok && s_rxFlag; }

int radio_read(uint8_t* buf, size_t maxlen, int* rssi) {
    if (!s_ok || !s_rxFlag) return -1;
    s_rxFlag = false;
    size_t n = g_radio.getPacketLength();
    if (n == 0 || n > maxlen) { g_radio.startReceive(); return -1; }
    int st = g_radio.readData(buf, n);
    if (rssi) *rssi = (int)g_radio.getRSSI();
    g_radio.startReceive();
    if (st == RADIOLIB_ERR_NONE || st == RADIOLIB_ERR_CRC_MISMATCH) return (int)n;
    return -1;
}
