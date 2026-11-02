#pragma once
// ============================================================
//  config.h  -  Constantes globales de PoisonCore-Cardputer
//  Objetivo: MeshCore (sync word 0x12)
// ============================================================
#include <stdint.h>

// ---- Pines SPI LoRa (Cap LoRa-1262) ------------------------
#define LORA_SCK   40
#define LORA_MOSI  14
#define LORA_MISO  39
#define LORA_NSS    5
#define LORA_RST    3
#define LORA_IRQ    4
#define LORA_BUSY   6

// ---- Pines I2C ---------------------------------------------
#define I2C_SDA  8
#define I2C_SCL  9

// ---- Pantalla Cardputer ADV --------------------------------
#define LCD_WIDTH  240
#define LCD_HEIGHT 135

// ---- Parametros de UI --------------------------------------
#define UI_HEADER_H   18
#define UI_FOOTER_H   16
#define UI_CONTENT_Y  (UI_HEADER_H + 2)
#define UI_CONTENT_H  (LCD_HEIGHT - UI_HEADER_H - UI_FOOTER_H - 4)

// ---- Colores (RGB565) --------------------------------------
#define COL_BG         0x0000   // negro
#define COL_HEADER_BG  0x001F   // azul oscuro
#define COL_ACCENT     0x07FF   // cian
#define COL_TEXT       0xFFFF   // blanco
#define COL_TEXT_DIM   0x7BEF   // gris claro
#define COL_SEL_BG     0x0410   // verde oscuro (seleccion)
#define COL_WARNING    0xFBE0   // amarillo
#define COL_DANGER     0xF800   // rojo
#define COL_OK         0x07E0   // verde

// ---- Version del firmware ----------------------------------
#define FW_VERSION "0.2.0"
#define APP_NAME   "PoisonCore"

// ============================================================
//  Configuracion de radio (editable en pantalla Config RF)
// ============================================================

// Presets region -> (freq, bw, sf, cr). TODO editable igualmente.
struct RFPreset {
    const char* name;
    float   freqMHz;
    float   bwKHz;
    uint8_t sf;
    uint8_t cr;   // 4/cr  (5..8)
};

// DEFAULT = Australia, 915.8 MHz, SF10, CR 4/5 (pedido del usuario).
static const RFPreset RF_PRESETS[] = {
    { "AU 915",  915.8f,   250.0f, 10, 5 },  // <- default
    { "EU 869",  869.618f,  62.5f,  8, 5 },  // default de este arbol MeshCore
    { "US 906",  906.875f, 250.0f, 10, 5 },
    { "Custom",  915.8f,   250.0f, 10, 5 },  // punto de partida libre
};
static const uint8_t NUM_RF_PRESETS = sizeof(RF_PRESETS) / sizeof(RF_PRESETS[0]);

#define DEFAULT_PRESET_IDX 0
#define TX_POWER_MAX       22   // maximo del SX1262 (+22 dBm)
#define TX_POWER_MIN       (-9)

// Estado vivo de la config de radio
struct RFConfig {
    float   freqMHz;
    float   bwKHz;
    uint8_t sf;        // 7..12
    uint8_t cr;        // 5..8  (= 4/cr)
    int8_t  txPower;   // dBm, hasta +22
    uint8_t presetIdx; // indice en RF_PRESETS[]
};

extern RFConfig g_rf;

// ---- Tipos de advert MeshCore ------------------------------
#define ADV_TYPE_CHAT      1
#define ADV_TYPE_REPEATER  2
#define ADV_TYPE_ROOM      3

// ---- PSK del canal publico MeshCore (hardcodeada, conocida) -
#define PUBLIC_GROUP_PSK   "izOH6cXN6mrJ5e26oRXNcg=="
