# LoRaMessenger

A standalone peer-to-peer encrypted messenger running entirely on ESP32 + LoRa hardware — **no internet, no phone, no infrastructure required**.

Devices discover each other over radio, exchange a 6-digit pairing code, and communicate via encrypted messages with delivery confirmation. Everything is stored locally in the device's non-volatile memory.

---

## Table of Contents

- [Hardware](#hardware)
- [Wiring](#wiring)
- [Dependencies](#dependencies)
- [Setup — Arduino IDE](#setup--arduino-ide)
- [Configuration](#configuration)
- [Building & Flashing](#building--flashing)
- [Usage](#usage)
- [Protocol](#protocol)
- [Encryption](#encryption)
- [Architecture](#architecture)
- [Known Limitations](#known-limitations)

---

## Hardware

**Recommended board: [Heltec WiFi LoRa 32 V2](https://heltec.org/project/wifi-lora-32/)**

The board already integrates everything needed except the keypad and optional peripherals:

| Component | Details |
|-----------|---------|
| MCU | ESP32 (dual-core, 240 MHz) |
| Radio | SX1276 LoRa (integrated on Heltec board) |
| Display | SSD1306 OLED 128×64 (integrated on Heltec board) |
| Keypad | 4×4 matrix keypad (external, e.g. [membrane 4×4](https://www.amazon.com/s?k=4x4+matrix+keypad)) |
| Buzzer | Active buzzer on GPIO 25 (optional) |
| Vibration | ERM coin motor on GPIO 12 via MOSFET (optional) |

> **Alternative:** Heltec Wireless Stick V3 is also supported. Define `WIRELESS_STICK_V3` to switch the display driver to 64×32 geometry.

---

## Wiring

### Keypad (4×4 matrix)

| Keypad pin | ESP32 GPIO |
|-----------|-----------|
| Row 1 | 23 |
| Row 2 | 22 |
| Row 3 | 21 |
| Row 4 | 17 |
| Col 1 | 13 |
| Col 2 | 2 |
| Col 3 | 32 |
| Col 4 | 33 |

**Key layout:**

```
[ 1 ][ 2 ][ 3 ][ ▲ ]
[ 4 ][ 5 ][ 6 ][ ▼ ]
[ 7 ][ 8 ][ 9 ][ OK ]
[ * ][ 0 ][ # ][ ← ]
```

| Key | Function |
|-----|---------|
| `▲` (U) | Navigate up / scroll up in chat |
| `▼` (D) | Navigate down / scroll down in chat |
| `OK` (E) | Enter / confirm / send |
| `←` (X) | Backspace / cancel / back |
| `*` | Toggle uppercase in T9 |
| `#` | Toggle number mode in T9 |

### LoRa Radio (SX1276 — already wired on Heltec board)

| Signal | GPIO |
|--------|------|
| SCK | 5 |
| MISO | 19 |
| MOSI | 27 |
| CS (SS) | 18 |
| RST | 14 |
| DIO0 | 26 |

> These pins are already connected internally on the Heltec WiFi LoRa 32 V2. No additional wiring needed for the radio.

### OLED Display (already wired on Heltec board)

Uses Heltec-defined pins: `SDA_OLED`, `SCL_OLED`, `RST_OLED`, `Vext`. These are provided by the Heltec board package and require no manual definition.

### Buzzer (optional)

Connect an **active buzzer** between **GPIO 25** and **GND**. No resistor needed for active buzzers.

### Vibration Motor (optional, disabled by default)

- Connect a **3V ERM coin motor** with a **MOSFET driver circuit** on **GPIO 12**.
- Recommended circuit: GPIO 12 → 100 Ω → MOSFET gate; 100 kΩ gate-to-GND pull-down; flyback diode across motor; 100 µF cap on the motor power rail.
- To enable, uncomment `// vibInit();` in `LoRaMessenger.ino`.

---

## Dependencies

### Board Package

Install the **Heltec ESP32** board package in Arduino IDE.

1. Open **File → Preferences**
2. Add the following URL to *Additional boards manager URLs*:

   ```
   https://resource.heltec.cn/download/package_heltec_esp32_index.json
   ```

3. Open **Tools → Board → Boards Manager**, search for `Heltec ESP32`, install **version 0.0.9** or the latest compatible with the Heltec library below.

### Arduino Libraries

Install the following libraries via **Sketch → Include Library → Manage Libraries** or by copying the folders into your Arduino `libraries/` directory.

| Library | Version | Source | Notes |
|---------|---------|--------|-------|
| **Heltec ESP32 Dev-Boards** | 2.1.4 | [GitHub](https://github.com/HelTecAutomation/Heltec_ESP32) | Provides `HT_SSD1306Wire.h`, `lora/LoRa.h`, board pin macros |
| **Keypad** | 3.1.1 | Library Manager (`Keypad` by Mark Stanley) | Matrix keypad input |
| **Adafruit GFX Library** | latest | Library Manager | Required by Heltec library |
| **Adafruit BusIO** | latest | Library Manager | Required by Heltec library |

> **Note:** The `Heltec ESP32 Dev-Boards` library includes its own bundled LoRa driver (`lora/LoRa.h`). The code automatically selects it over the standalone `LoRa` library if present, via `#if __has_include("lora/LoRa.h")`.

### Built-in (no installation needed)

These are provided by the ESP32 Arduino core and require no separate installation:

| Header | Provides |
|--------|---------|
| `SPI.h` | SPI bus for LoRa |
| `Wire.h` | I2C bus for OLED |
| `Preferences.h` | NVS (non-volatile storage) |
| `mbedtls/sha256.h` | SHA-256 for encryption |
| `esp32-hal-ledc.h` | PWM for vibration motor |
| `esp_random()` | Hardware RNG |

---

## Setup — Arduino IDE

1. **Install the board package** and **libraries** listed above.
2. Open `LoRaMessenger.ino` in Arduino IDE.
3. Select your board: **Tools → Board → Heltec ESP32 Arduino → WiFi LoRa 32(V2)**.
4. Select the correct port: **Tools → Port → COMx** (Windows) or `/dev/ttyUSB0` (Linux/Mac).
5. Set the `DEVICE_ID` for each physical device (see [Configuration](#configuration)).
6. Click **Upload**.

---

## Configuration

All configuration is done in source files. No external config files exist.

### Device ID

Each device on the network must have a unique ID (1–254). Edit `protocol.cpp` or pass it as a compile flag:

```cpp
// protocol.cpp line 26 — change the default:
#define DEVICE_ID 1
```

Or, in Arduino IDE via **Sketch → Upload Using Programmer** with a custom `build.extra_flags`:

```
-DDEVICE_ID=2
```

### LoRa Radio Parameters

Defined in `protocol.cpp`:

| Parameter | Value | Constant |
|-----------|-------|---------|
| Frequency | 915 MHz | `LORA_BAND 915E6` |
| TX Power | 14 dBm | `LORA_POWER_DBM 14` |
| Spreading Factor | 7 | `LoRa.setSpreadingFactor(7)` |
| Bandwidth | 125 kHz | `LoRa.setSignalBandwidth(125E3)` |
| Coding Rate | 4/5 | `LoRa.setCodingRate4(5)` |
| Sync Word | 0x12 | `LoRa.setSyncWord(0x12)` |

> **Regional note:** 915 MHz is the ISM band for the Americas. Change `LORA_BAND` to the appropriate frequency for your region (e.g. `868E6` for Europe). Always verify local regulations.

### Buzzer Type

Defined in `buzz.cpp`:

```cpp
#define USE_PASSIVE_BUZZER false  // true = passive (tone), false = active (on/off)
```

### Vibration Motor

Enabled by uncommenting a line in `LoRaMessenger.ino`:

```cpp
// vibInit();  ← remove the leading // to enable
```

---

## Building & Flashing

### Arduino IDE (recommended)

1. Open `LoRaMessenger.ino`.
2. Set the correct board, port, and `DEVICE_ID`.
3. Press **Ctrl+U** (Upload).

### PlatformIO (advanced, manual setup)

No `platformio.ini` is included, but an equivalent configuration would be:

```ini
[env:heltec_wifi_lora_32_V2]
platform = espressif32
board = heltec_wifi_lora_32_V2
framework = arduino
lib_deps =
    heltec-cn/Heltec ESP32 Dev-Boards @ ^2.1.4
    Chris--A/Keypad @ ^3.1.1
    adafruit/Adafruit GFX Library
    adafruit/Adafruit BusIO
build_flags =
    -DDEVICE_ID=1
```

---

## Usage

### First Boot

On first power-on, the device prompts for a **device name** (shown on the name entry screen). Use the keypad to type a name with T9 input, then press **OK** to save it.

### T9 Input

Type text using multi-tap, phone-style input:

| Key | Letters |
|-----|---------|
| `1` | `. , ? !` |
| `2` | `a b c` |
| `3` | `d e f` |
| `4` | `g h i` |
| `5` | `j k l` |
| `6` | `m n o` |
| `7` | `p q r s` |
| `8` | `t u v` |
| `9` | `w x y z` |
| `0` | ` ` (space) |
| `*` | Toggle uppercase (A) |
| `#` | Toggle number mode (#) |

Tap the same digit repeatedly within 800 ms to cycle through its letters. Wait 800 ms or tap a different digit to commit the current character.

### Navigation

```
Boot → PAGE_NAME (first run only)
         ↓ OK (save name)
       PAGE_CONTACTS
         ↓ OK (open chat)          → PAGE_CHAT
         ↓ OK (no contacts)        → PAGE_SEARCH
         ↓ ← (ESC)                 → PAGE_CONFIG
                                        ↓ Broadcast → PAGE_BROADCAST
                                        ↓ Contact List → PAGE_CONTACTS
                                        ↓ Factory Reset → PAGE_CONFIRM_RESET

PAGE_SEARCH → select peer → OK    → PAGE_INVITE_CODE  (inviter shows 6-digit code)
           ← (ESC)                → PAGE_CONTACTS

PAGE_INVITE_PROMPT                (invitee receives invite, types the code)
  → OK (correct code)             → PAGE_CONTACTS (contact added)
```

### Adding a Contact (Pairing)

1. Both devices go to **Contacts** screen and press **OK** — this opens **Search nearby**.
2. The inviter selects a peer and presses **OK**. A 6-digit code appears on the inviter's screen.
3. The invitee receives a prompt automatically and types the 6-digit code shown by the inviter.
4. Both devices confirm — the contact is saved on both sides.

### Chatting

- Select a contact → **OK** to open chat.
- Type a message with T9, press **OK** to send.
- Message status indicators:
  - *(no indicator)* — queued
  - *(sent, waiting ACK)* — sent
  - `//` — delivered (ACK received)
  - `/x` — failed after 3 retries
- Press **▲/▼** to scroll through chat history.
- Press **← (backspace)** with an empty compose bar to go back to contacts.

### Broadcast

Send the same message to all contacts at once:
- **Contacts → ← (ESC) → Config → Broadcast**
- Type your message and press **OK** to send.

### Factory Reset

- **Contacts → ← (ESC) → Config → Factory Reset → Yes**
- Erases device name and all contacts from NVS.

---

## Protocol

All communication is custom LoRa P2P — **not LoRaWAN**.

### Packet Structure

```
struct Packet {
  uint8_t  sender;      // device ID (1–254)
  uint8_t  receiver;    // device ID or 0xFF (broadcast)
  uint8_t  type;        // MsgType enum
  uint16_t seq;         // sequence number
  uint8_t  len;         // body length in use
  char     body[160];   // payload
  uint8_t  crc;         // CRC-8 over all preceding bytes
} __attribute__((packed));
```

Total size: 166 bytes.

### Message Types

| Value | Name | Direction | Purpose |
|-------|------|-----------|---------|
| 1 | `TYPE_DATA` | unicast | Encrypted chat message |
| 2 | `TYPE_ACK` | unicast | Delivery acknowledgement |
| 10 | `TYPE_DISC_REQ` | broadcast | Discovery ping (includes sender's name) |
| 11 | `TYPE_DISC_RSP` | unicast | Discovery reply (includes name) |
| 20 | `TYPE_INV_REQ` | unicast | Pairing invite (4-byte code + 20-byte name) |
| 21 | `TYPE_INV_ACK` | unicast | Pairing accept (echoes code + name) |

### Reliability

- Sender retries up to **3 times** with a **1200 ms ACK timeout** per attempt, using a **non-blocking** pending message queue (`protocolPendingTick()`).
- Duplicate suppression: one sequence number slot per sender ID (last seen). Duplicate `TYPE_DATA` packets are re-ACKed immediately to stop the sender retrying.
- Discovery entries expire after **8 seconds** without a new beacon.

---

## Encryption

Messages use a **custom stream cipher** built from SHA-256:

1. A 4-byte random nonce is generated per message via `esp_random()`.
2. A keystream is produced by hashing `key ‖ nonce ‖ counter` (32+4+4 bytes) with SHA-256, incrementing the counter for each 32-byte block.
3. The plaintext is XOR-ed with the keystream.
4. The nonce is prepended to the ciphertext in the packet body.

The key derivation function `derivePairKey()` (in `crypto.cpp`) computes a 32-byte shared key from:

```
SHA-256( sort(nameA, nameB) ‖ code6 ‖ nonce8 )
```

> **Current status:** `derivePairKey()` is implemented but **not yet called** at pairing time. Contact keys are currently stored as all-zeros, which means the cipher runs but provides no cryptographic security. This is a known gap for a future update — connect `derivePairKey()` in the pairing flow to enable real end-to-end encryption.

---

## Architecture

```
LoRaMessenger.ino   — setup() / loop() entry point
│
├── protocol.cpp/h  — LoRa radio driver, packet framing, discovery,
│                     pairing, chat send/receive, non-blocking ACK retry
│
├── ui.cpp/h        — OLED display pages and state machine (Page enum)
│
├── input.cpp/h     — Keypad polling, T9 engine, page navigation,
│                     compose buffer, selection state
│
├── storage.cpp/h   — NVS (Preferences): device name, contacts (up to 10)
│
├── crypto.cpp/h    — SHA-256 wrapper, key derivation, keystream XOR
│
├── buzz.cpp/h      — Non-blocking buzzer patterns (active or passive)
│
├── vib.cpp/h       — Non-blocking vibration motor via LEDC PWM
│
└── bitmaps.h       — PROGMEM icon data
```

**Loop execution order:**

```
protocolPoll()        ← receive LoRa packets, dispatch handlers
protocolPendingTick() ← non-blocking retry timer for outgoing messages
inputPoll()           ← read keypad, drive navigation and compose
buzzTick()            ← advance buzzer pattern (non-blocking)
vibTick()             ← advance vibration pattern (non-blocking)
uiTick()              ← blink cursor / refresh animations
```

**Global state:** The `page` variable (declared in `ui.cpp`, shared via `ui.h`) is the central state machine. Both `protocol.cpp` and `input.cpp` read and write it to trigger UI transitions on incoming packets or key presses.

---

## Known Limitations

| Issue | Details |
|-------|---------|
| **Key derivation not integrated** | `derivePairKey()` exists but is not called during pairing. Contact keys are zero — encryption is structural only. |
| **Manual DEVICE_ID** | Each device needs a unique ID compiled in. No automatic assignment. |
| **Single sequence dedup** | Only the last sequence number per sender is tracked; a reboot resets it, allowing potential duplicates. |
| **No persistent chat history** | Chat messages live in RAM only and are lost on reboot. |
| **Up to 10 contacts** | Hard-coded limit in `storage.cpp`. |
| **Up to 64 chat messages** | Ring buffer in `protocol.cpp`; oldest messages are dropped first. |
| **Discovery may double-fire** | `protocolPoll()` and `protocolSearchTick()` both schedule discovery at ~1 Hz on `PAGE_SEARCH`. Harmless but redundant. |
