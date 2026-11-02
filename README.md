# PoisonCore - firmware de pentesting MeshCore (Cardputer ADV)

Firmware para **M5Stack Cardputer ADV** (ESP32-S3) + **Cap LoRa-1262** (SX1262), orientado a **MeshCore** (sync word `0x12`). Hermano de PoisonMesh (que targetea Meshtastic). 
**Uso autorizado únicamente**: redes propias o con permiso explícito. Afecta espectro RF compartido.

## Compilar / flashear

```bash
cd PoisonCore
pio run                 # compila
pio run -t upload       # flashea (puerto en platformio.ini: /dev/ttyACM0)
pio device monitor      # consola serie 115200
```

El binario queda en `.pio/build/cardputer-adv/firmware.bin`.

**Bootea sin el Cap LoRa**: el init del SX1262 es tolerante. Si el módulo no está, se muestra `RF N/A` y toda la UI/menús funcionan igual (para validar boot y navegación). El TX/RX real requiere el Cap LoRa.

## Flujo

1. **Splash** "PoisonCore" → pulsar una tecla.
2. **[A] Config RF**: Preset / Freq / BW / SF / CR / TxPwr. Default **Australia
   915.8 MHz, SF10, CR 4/5, +22 dBm**. Todo editable (`Fn+;`/`Fn+.` mover,
   `Fn+,`/`Fn+/` cambiar, `Enter` OK).
3. **[B] Identidad**: nombre (teclado) + tipo (CHAT/REPEATER/ROOM); `Enter`
   genera el par Ed25519.
4. **[C] Menú principal**:
   - **Announce Flood** — modo `Refresh` (tu identidad, timestamp++) o `Sybil`
     (identidad nueva por advert). `Enter` start/stop.
   - **Mensajes** — `Canal Public` (sender = tu nombre, spoofeable) o `DM` a un
     nodo del sniffer. Escribir y `Enter` para enviar.
   - **Regenerar identidad** — par + nombre nuevos (reset).
   - **Recon / Sniffer** — RX pasivo; lista adverts vistos.
   - **Config RF** — vuelve a [A].

Teclas: `Fn+;./,//` = flechas · `Enter` = confirmar · `` ` `` = volver/stop ·
`Backspace` = borrar · botón G0 = apagar/encender pantalla.

## Estructura

- `src/config.h` — pines, colores, presets RF, PSK del canal público.
- `src/pc_crypto.*` — AES-128-ECB + HMAC-SHA256 (port de MeshCore `Utils.cpp`).
- `src/pc_identity.*` — Ed25519 keygen/sign/ECDH (lib `lib/ed25519`).
- `src/pc_mesh.*` — framing de adverts / mensajes de canal / DM + sniffer.
- `src/radio.*` — SX1262 vía RadioLib (sync `0x12`), tolerante.
- `src/keyboard.*`, `src/screensaver.h` — portados de PoisonMesh.
- `src/main.cpp` — splash + máquina de estados + pantallas.
- `lib/ed25519/` — lib Ed25519 (orlp), portada de MeshCore.


