# LoRaMessenger

A standalone peer-to-peer encrypted messenger running entirely on ESP32 + LoRa hardware — **no internet, no phone, no infrastructure required**.

Devices discover each other over radio or via Bluetooth proximity, exchange keys, and communicate via authenticated encrypted messages with delivery confirmation. Everything is stored locally in the device's non-volatile memory and flash filesystem.

---

## Table of Contents

- [Hardware](#hardware)
- [Wiring](#wiring)
- [Dependencies](#dependencies)
- [Setup — Arduino IDE](#setup--arduino-ide)
- [Configuration](#configuration)
- [Building & Flashing](#building--flashing)
- [Usage](#usage)
  - [First Boot](#first-boot)
  - [T9 Input](#t9-input)
  - [Navigation Map](#navigation-map)
  - [Pairing Contacts](#pairing-contacts)
  - [Chatting](#chatting)
  - [SOS Mode](#sos-mode)
  - [Settings](#settings)
  - [PIN Lock](#pin-lock)
  - [Broadcast](#broadcast)
  - [Factory Reset](#factory-reset)
- [Protocol](#protocol)
- [Encryption & Security](#encryption--security)
- [Architecture](#architecture)
- [GPIO Reference](#gpio-reference)
- [Known Limitations](#known-limitations)

---

## Hardware

**Recommended board: [Heltec WiFi LoRa 32 V2](https://heltec.org/project/wifi-lora-32/)**

The board already integrates everything needed except the keypad and optional peripherals:

| Component | Details |
|-----------|---------|
| MCU | ESP32 (dual-core, 240 MHz, BLE + WiFi built-in) |
| Radio | SX1276 LoRa (integrated on Heltec board) |
| Display | SSD1306 OLED 128×64 (integrated on Heltec board) |
| Keypad | 4×4 matrix keypad (external) |
| Buzzer | Active buzzer on GPIO 25 (optional) |
| Vibration | ERM coin motor on GPIO 12 via MOSFET (optional) |
| Battery | Monitored via ADC on GPIO 35 (onboard on Heltec) |

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
| `*` | Toggle uppercase (T9) — hold 2s to lock device or trigger SOS |
| `#` | Toggle number mode (T9) |

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

Uses Heltec-defined pins: `SDA_OLED` (GPIO 4), `SCL_OLED` (GPIO 15), `RST_OLED` (GPIO 16), `Vext` (GPIO 21). These are provided by the Heltec board package.

> **Important:** GPIO 4 is `SDA_OLED`. Do not use it for any external peripheral (LED, sensor, etc.) as it will jam the I2C bus.

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
| `LittleFS.h` | Flash filesystem for chat history |
| `mbedtls/sha256.h` | SHA-256 for encryption and key derivation |
| `mbedtls/pkcs5.h` | PBKDF2-HMAC-SHA256 for PIN hashing |
| `esp32-hal-ledc.h` | PWM for vibration motor |
| `BLEDevice.h` / `BLEServer.h` / `BLEScan.h` | BLE proximity pairing |
| `esp_random()` | Hardware RNG |

---

## Setup — Arduino IDE

1. **Install the board package** and **libraries** listed above.
2. Open `LoRaMessenger.ino` in Arduino IDE.
3. Select your board: **Tools → Board → Heltec ESP32 Arduino → WiFi LoRa 32(V2)**.
4. Select the correct port: **Tools → Port → COMx** (Windows) or `/dev/ttyUSB0` (Linux/Mac).
5. Set a unique `DEVICE_ID` for each physical device (see [Configuration](#configuration)).
6. Click **Upload**.

---

## Configuration

All configuration is done in source files. No external config files exist.

### Device ID

Each device on the network must have a unique ID (1–254). Edit `protocol.cpp` or pass it as a compile flag:

```cpp
// protocol.cpp — change the default:
#define DEVICE_ID 1
```

Or via Arduino IDE **Sketch → Upload Using Programmer** with a custom `build.extra_flags`:

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

### Runtime Settings (via Config menu)

All settings below are configurable at runtime through the device menu and persisted in NVS:

| Setting | Default | Location |
|---------|---------|---------|
| Screen timeout | 30 s | Config → Power |
| Sleep poll interval | 1 s | Config → Power |
| OLED brightness | ~78% | Config → Power |
| Notification mask (LED/Buzz/Vib/Wake) | Buzz + Wake | Config → Notifications |
| Lock on/off | Off | Config → Security |
| Lock timeout | 5 min | Config → Security |
| PIN | not set | Config → Security → Set PIN |
| Default message priority | Normal | Config → Messages |
| Chat history retention | 7 days | Config → Messages |

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

On first power-on, the device shows a boot splash then prompts for a **device name**. Use the keypad to type a name (up to 20 characters) with T9 input, then press **OK** to save it. The name is used to identify you to other devices during discovery and pairing.

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

### Navigation Map

```
Boot → PAGE_NAME (first run only)
         ↓ OK (save name)
       PAGE_CONTACTS                   ← main hub
         ↓ OK (open chat)        → PAGE_CHAT
         ↓ OK (no contacts)      → PAGE_SEARCH → invite peer → PAGE_INVITE_CODE
         ↓ ← (back)              → PAGE_CONFIG
                │
                ├── Notifications  → PAGE_SETTINGS_NOTIFY
                ├── Power          → PAGE_SETTINGS_POWER
                ├── Security       → PAGE_SETTINGS_SECURITY
                │                       └── Set PIN → PAGE_PIN_SETUP
                ├── Messages       → PAGE_SETTINGS_MESSAGES
                ├── Broadcast      → PAGE_BROADCAST (chat UI)
                ├── Contacts       → PAGE_CONTACTS
                ├── System         → PAGE_SETTINGS_SYSTEM
                │                       └── Factory Reset → PAGE_CONFIRM_RESET
                └── Pair via BT    → PAGE_BT_PAIR

Special:
  * held 2s (on contacts/chat)  → lock immediately (if PIN set)
                                   OR start SOS (if no PIN)
  * held 2s (on SOS screen)     → cancel SOS after 3s hold
```

### Status Bar

Every screen shows a 12-pixel status bar at the top with:

```
[L] DeviceName      RSSI  Bat% [~]
```

| Icon | Meaning |
|------|---------|
| `L` | Device is locked |
| `~` | Battery charging |
| RSSI | Signal strength to current peer (chat screen only) |
| `Bat%` | Battery level |

---

### Pairing Contacts

Two devices must be **paired** before they can exchange encrypted messages. There are two pairing methods:

---

#### Method 1: LoRa Pairing (Over-the-air, any distance)

Both devices communicate over the LoRa radio to establish the contact. One device acts as inviter, the other as invitee. The pairing is secured by a **6-digit code** that must be communicated through a separate channel (voice, phone call, SMS, etc.) — this code never travels over the air.

**How to use:**

1. Both devices go to the **Contacts** screen.
2. The **inviter** presses **OK** → opens **Search** → device scans for nearby LoRa peers.
3. The inviter selects the peer from the list and presses **OK**. A **6-digit code** appears on the inviter's screen.
4. The inviter communicates this code out-of-band to the invitee (voice call, etc.).
5. The **invitee** automatically sees an invite prompt. They type the 6-digit code and press **OK**.
6. Both devices confirm — the contact is saved on both sides with a derived shared key.

**Shared key derivation:**

```
key = SHA-256( sort(nameA, nameB) ‖ code6 ‖ nonce8 )
```

The `nonce8` is generated randomly by the inviter and travels in the invite packet (public). The `code6` is the only secret and **never travels over the air**.

| Aspect | Details |
|--------|---------|
| Physical proximity required | No — works at any LoRa range (hundreds of metres to kilometres) |
| Out-of-band channel required | Yes — code6 must be shared via voice, SMS, etc. |
| Key entropy | ~20 bits (10⁶ combinations for code6) |
| Attack surface | Attacker within LoRa range during pairing can capture nonce8 and brute-force code6 offline in <1 ms (GPU). Risk is low in practice due to the narrow pairing window |
| Best for | Pairing at long range where physical proximity is impossible |

---

#### Method 2: Bluetooth Pairing (Proximity, ≤10 m)

Both devices must be physically close (within typical BLE range, ~5–10 m). The key is exchanged directly over Bluetooth — **no code needs to be memorised or communicated separately**. The required physical proximity acts as the security proof.

The pairing process uses BLE GATT: one device advertises as **Host** with a freshly generated 32-byte random key; the other connects as **Join**, reads the key, and sends back its identity. Both sides save the contact.

**How to use:**

1. Go to **Config → Pair via BT** on both devices.
2. On the **first device**, select **Host (share key)** → press **OK**. The device advertises via BLE for up to 60 seconds.
3. On the **second device**, select **Join (scan)** → press **OK**. The device scans for the host.
4. Once found, the connection and key exchange happen automatically.
5. Both devices show "Done! Paired: \<name\>" and the contact is saved.

> If pairing does not complete within 60 seconds, the process times out and can be restarted.

**BLE packet layout:**

```
INFO (Host → Join):  id(1) | name(16) | key(32)  = 49 bytes
ACK  (Join → Host):  id(1) | name(16)             = 17 bytes
```

The 32-byte key is generated with `esp_random()` (hardware RNG) and has full 256-bit entropy.

| Aspect | Details |
|--------|---------|
| Physical proximity required | Yes — BLE range (~10 m) |
| Out-of-band channel required | No |
| Key entropy | 256 bits (32 bytes from hardware RNG) |
| Attack surface | An attacker must be physically present and within BLE range during the ~5-second exchange window |
| Best for | Maximum security when devices can be brought together |

---

#### Comparison: LoRa vs Bluetooth Pairing

| | LoRa Pairing | BT Pairing |
|--|--|--|
| Works at long range | Yes | No (~10 m max) |
| Requires physical proximity | No | Yes |
| Manual code exchange needed | Yes (6-digit, out-of-band) | No |
| Key strength | ~20 bits (code6) | 256 bits (hardware RNG) |
| Offline brute-force risk | Yes (1 M combinations) | No |
| After pairing | Chat via LoRa | Chat via LoRa |

After pairing via either method, all chat communication happens over LoRa normally — the pairing method only affects how the initial shared key is established.

---

### Chatting

- Select a contact → **OK** to open chat.
- Type a message with T9, press **OK** to send.
- Message status indicators (shown after the message text):
  - *(no indicator)* — queued
  - `/` — sent, waiting for ACK
  - `//` — delivered (ACK received)
  - `/x` — failed after all retries
  - `[!]` — high-priority message
  - `[S]` — SOS message
- Press **▲/▼** to scroll through chat history (loaded from flash on startup).
- Press **←** with an empty compose bar to go back to contacts.

**Message priority** (set via Config → Messages → Default priority):

| Priority | Retries | ACK Timeout |
|----------|---------|------------|
| Normal | 3 | 1200 ms |
| High | 7 | 800 ms |
| SOS | 15 | 600 ms + resent every 30 s |

Up to **4 messages** can be in-flight simultaneously (pending queue). SOS to multiple contacts is tracked independently for each recipient.

---

### SOS Mode

SOS sends an authenticated encrypted alert to all paired contacts every 30 seconds until cancelled.

**To activate SOS:**
- Hold `*` for 2 seconds from the Contacts or Chat screen.
- The SOS page shows a countdown and the number of transmissions sent.

**To cancel SOS:**
- Hold `*` for 3 seconds on the SOS page.

If the device has no contacts, SOS broadcasts an unencrypted discovery-style packet so unpaired devices can see the distress signal.

---

### Settings

Access via **Contacts → ← → Config**:

| Menu item | What it configures |
|-----------|-------------------|
| **Notifications** | Toggle LED / Buzz / Vibration / Wake screen on incoming messages |
| **Power** | Screen timeout, sleep poll interval, OLED brightness |
| **Security** | Enable/disable lock, lock timeout, set/change PIN |
| **Messages** | Default message priority, chat history retention days |
| **Broadcast** | Send one message to all contacts at once |
| **Contacts** | Return to contacts list |
| **System** | View device ID, LoRa frequency, factory reset |
| **Pair via BT** | Pair a new contact via Bluetooth proximity |

---

### PIN Lock

When enabled, the device locks after the configured timeout and displays a PIN entry screen.

- **Set PIN:** Config → Security → Set PIN → enter 4–8 digits → OK.
- **Enable lock:** Config → Security → Lock on/off → toggle.
- **Lock timeout:** Config → Security → Lock timeout (Immediate / 1 / 5 / 15 / 60 min).
- **Instant lock:** Hold `*` for 2 seconds from any screen (when PIN is set).
- **Unlock:** Enter your PIN on the lock screen → OK.

The PIN is stored as a **PBKDF2-HMAC-SHA256** hash (20,000 iterations, random 16-byte salt) in NVS. The raw PIN is never saved.

**Progressive lockout:** After 3 failed attempts, the device enforces an increasing wait before the next attempt (30 s → 2 min → 10 min).

---

### Broadcast

Send the same message to all contacts at once (each encrypted individually):

- **Contacts → ← → Config → Broadcast**
- Type your message and press **OK** to send.

Broadcast messages are sent without delivery tracking (no ACK/retry per recipient).

---

### Factory Reset

- **Contacts → ← → Config → System → Factory Reset → Yes**
- Erases device name, all contacts, settings, and chat history from NVS and LittleFS.

---

## Protocol

All communication is custom LoRa P2P — **not LoRaWAN**.

### Packet Structure

```
struct Packet {
  uint8_t  sender;      // device ID (1–254)
  uint8_t  receiver;    // device ID or 0xFF (broadcast)
  uint8_t  type;        // MsgType enum
  uint8_t  flags;       // bits 0-1: MsgPriority; bit 2: SOS marker
  uint16_t seq;         // sequence number
  uint8_t  len;         // body length in use
  char     body[160];   // payload
  uint16_t crc;         // CRC-16/CCITT over all preceding bytes
} __attribute__((packed));
```

Total size: 169 bytes. All fields packed with no padding.

### Message Types

| Value | Name | Direction | Purpose |
|-------|------|-----------|---------|
| 1 | `TYPE_DATA` | unicast | Encrypted + authenticated chat message |
| 2 | `TYPE_ACK` | unicast | Delivery acknowledgement |
| 10 | `TYPE_DISC_REQ` | broadcast | Discovery ping (includes sender name) |
| 11 | `TYPE_DISC_RSP` | unicast | Discovery reply (includes name) |
| 20 | `TYPE_INV_REQ` | unicast | LoRa pairing invite (code6 + name + nonce8) |
| 21 | `TYPE_INV_ACK` | unicast | LoRa pairing accept (code6 + name + nonce8) |

### DATA Body Layout

```
body = nonce4(4) | encrypt( plaintext(≤151 B) | hmac_tag(4) )
     = 4 + up to 155 bytes total
```

The authentication tag binds the ciphertext to the key and nonce, preventing bit-flipping attacks.

### Reliability

- **Pending queue:** up to **4 messages** can be in-flight simultaneously, each with independent retry state. Older single-slot design has been replaced.
- **Retries by priority:** Normal 3×1200 ms, High 7×800 ms, SOS 15×600 ms.
- **Duplicate suppression:** sliding window of 8 sequence numbers per sender. Reordered packets within the window are handled correctly. Duplicate `TYPE_DATA` packets trigger an idempotent ACK to stop the sender retrying.
- **ACK gating:** ACK is only sent after successful decryption and authentication. Packets from unknown senders or with invalid HMAC tags are discarded silently.
- **Discovery expiry:** entries expire after 8 seconds without a new beacon.

### Error Detection

CRC-16/CCITT (polynomial 0x1021, init 0xFFFF) is computed over all packet bytes except the 2-byte `crc` field. This provides ~65,000× better random error detection than the previous CRC-8.

> **Note:** The CRC16 change is a **breaking wire-format change**. All devices on the same network must be flashed with the same firmware version.

---

## Encryption & Security

### Message Encryption

Messages use a **stream cipher** built from SHA-256 in counter mode:

1. A 4-byte random nonce is generated per message via `esp_random()`.
2. A keystream is produced by hashing `key ‖ nonce ‖ counter` (32+4+4 bytes) with SHA-256, incrementing the counter for each 32-byte block.
3. The plaintext (+ authentication tag) is XOR-ed with the keystream.
4. The nonce is prepended to the ciphertext in the packet body.

### Message Authentication (HMAC)

Each message carries a 4-byte truncated MAC computed as:

```
authKey = SHA-256( pairKey ‖ "auth" )
tag     = SHA-256( authKey ‖ nonce4 ‖ plaintext )[0:4]
```

The tag is encrypted together with the plaintext. On receive, decryption is performed first, then the tag is verified before the message is accepted or ACK-ed. Any tampered ciphertext produces a different plaintext and fails the tag check.

### Key Derivation (LoRa Pairing)

The `derivePairKey()` function (in `crypto.cpp`) computes a 32-byte shared key from:

```
key = SHA-256( sort(nameA, nameB) ‖ code6(4B) ‖ nonce8(8B) )
```

Both devices compute the same key independently after the invite exchange. The `code6` is the only secret — it never travels over the air.

### Key Generation (BT Pairing)

The Host generates 32 bytes directly from the hardware RNG (`esp_random()`) and transmits them over BLE. Full 256-bit entropy, no derivation step.

### PIN Security

PINs are stored using **PBKDF2-HMAC-SHA256** with 20,000 iterations and a 16-byte random salt. The raw PIN is never persisted. A progressive lockout (30 s / 2 min / 10 min) protects against on-device brute-force.

### Security Considerations

| Threat | Mitigation |
|--------|-----------|
| Passive eavesdropping on LoRa | Stream cipher (XOR keystream from SHA-256 counter mode) |
| Bit-flipping / active RF tampering | 4-byte HMAC tag verified before ACK; tampered packets silently dropped |
| LoRa pairing code brute-force | ~1 M combinations; attacker must be present during narrow pairing window; BT pairing eliminates this entirely |
| On-device PIN brute-force | PBKDF2 (20k rounds) + progressive lockout |
| Physical access | PIN lock with PBKDF2 hash; no plain PIN stored |

---

## Architecture

```
LoRaMessenger.ino   — setup() / loop() entry point
│
├── protocol.cpp/h  — LoRa radio driver, packet framing (CRC-16),
│                     discovery, LoRa pairing, chat send/receive,
│                     pending queue (4 slots), ACK retry, SOS
│
├── ui.cpp/h        — OLED display pages and state machine (Page enum),
│                     status bar, boot splash, all draw functions
│
├── input.cpp/h     — Keypad polling, T9 engine, page navigation,
│                     compose buffer, long-press detection (SOS/lock),
│                     selection state for all sub-menus
│
├── storage.cpp/h   — NVS (Preferences): device name, contacts (up to 10)
│
├── crypto.cpp/h    — SHA-256 wrapper, key derivation, keystream XOR,
│                     HMAC sign/verify (4-byte truncated tag)
│
├── settings.cpp/h  — AppSettings struct, NVS persistence (namespace "cfg")
│
├── power.cpp/h     — Screen timeout, battery ADC (GPIO 35),
│                     charging detection, powerActivity() reset
│
├── lock.cpp/h      — PIN entry, PBKDF2-HMAC-SHA256 hashing,
│                     auto-lock timer, progressive lockout
│
├── notify.cpp/h    — Notification engine: coordinates LED, buzz, vib
│                     patterns per notifyMask setting
│
├── history.cpp/h   — LittleFS chat history: per-contact encrypted records,
│                     configurable retention (days), load on boot
│
├── btpair.cpp/h    — BLE proximity pairing: GATT server (Host) and
│                     client (Join), 32-byte key exchange, 60s timeout
│
├── buzz.cpp/h      — Non-blocking buzzer patterns (active or passive)
│
├── vib.cpp/h       — Non-blocking vibration motor via LEDC PWM
│
└── bitmaps.h       — PROGMEM icon data
```

**Loop execution order:**

```
powerTick()           ← screen timeout / wake management
protocolPoll()        ← receive LoRa packets, dispatch handlers
protocolPendingTick() ← non-blocking retry timer for outgoing messages
protocolSOSTick()     ← SOS resend every 30 s
inputPoll()           ← read keypad, drive navigation and compose
lockTick()            ← auto-lock timer
buzzTick()            ← advance buzzer pattern (non-blocking)
vibTick()             ← advance vibration pattern (non-blocking)
notifyTick()          ← LED pattern stepper
notifyHeartbeatTick() ← heartbeat LED pulse every 5 s
uiTick()              ← blink cursor / refresh animations
btpairTick()          ← BLE pairing state machine
```

**Global state:** The `page` variable (declared in `ui.cpp`, shared via `ui.h`) is the central state machine. All modules read and write it to trigger UI transitions.

---

## GPIO Reference

| GPIO | Used by | Notes |
|------|---------|-------|
| 4 | SDA_OLED (I2C data) | Do not use externally |
| 15 | SCL_OLED (I2C clock) | Do not use externally |
| 16 | RST_OLED | Do not use externally |
| 5 | LoRa SCK | SPI |
| 19 | LoRa MISO | SPI |
| 27 | LoRa MOSI | SPI |
| 18 | LoRa CS | SPI |
| 14 | LoRa RST | |
| 26 | LoRa DIO0 | IRQ |
| 23 | Keypad Row 1 | |
| 22 | Keypad Row 2 | |
| 21 | Keypad Row 3 | also `Vext` on some Heltec variants |
| 17 | Keypad Row 4 | |
| 13 | Keypad Col 1 | |
| 2 | Keypad Col 2 | onboard LED on some boards |
| 32 | Keypad Col 3 | |
| 33 | Keypad Col 4 | |
| 25 | Buzzer | active or passive |
| 12 | Vibration motor | via MOSFET |
| 35 | Battery ADC | input only |

**Free GPIOs** (not used by any built-in function): `0` (boot mode sensitive), `34` (input only), `36`, `39`.

---

## Known Limitations

| Issue | Details |
|-------|---------|
| **Manual DEVICE_ID** | Each device needs a unique ID compiled in. No automatic assignment. |
| **Up to 10 contacts** | Hard-coded limit in `storage.cpp`. |
| **Up to 64 chat messages in RAM** | Ring buffer in `protocol.cpp`; oldest messages are dropped first (persistent history is on flash). |
| **LoRa pairing code strength** | 6-digit code = ~20 bits of entropy; BT pairing is strongly preferred when physical proximity is possible. |
| **No key rotation** | The shared key from pairing is permanent. Re-pair to rotate. |
| **BLE and LoRa simultaneous** | BLE uses the ESP32 radio; LoRa uses SPI to an external SX1276. Both can run simultaneously, but BLE scanning during LoRa receive may add latency to packet processing. BLE is automatically stopped after pairing. |
| **CRC-16 breaking change** | All devices in the same network must run the same firmware version. Mixing CRC-8 and CRC-16 firmware causes all packets to be rejected. |
| **Discovery may double-fire** | `protocolPoll()` and `protocolSearchTick()` both schedule discovery at ~1 Hz on `PAGE_SEARCH`. Harmless but redundant. |
