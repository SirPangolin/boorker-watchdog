# Boorker Platform - System Design

**Date:** 2026-02-27
**Status:** Approved
**Hardware Cost:** $65.05 (ordered)

## Overview

Boorker is a generic IoT sensor mesh platform built on Heltec WiFi LoRa 32 V3 boards. While initially designed for basement sump pump monitoring, the architecture supports arbitrary sensor configurations through a pluggable driver system.

Key capabilities:
- Pluggable sensor architecture with runtime configuration
- Dual-path mesh sync (LoRa + WiFi simultaneously)
- Configurable rules engine with alert backoff
- Unified web interface accessible from any node
- Remote access via Tailscale (MicroLink)
- OTA firmware updates
- Tiered data logging

## Node Architecture

All nodes are symmetric — same firmware, same capabilities. Any node can serve as mesh coordinator, web server, or sensor host. Configuration determines role, not code.

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           BOORKER NODE                                   │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │                    Heltec WiFi LoRa 32 V3                          │ │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                 │ │
│  │  │  ESP32-S3   │  │ SX1262 LoRa │  │ 0.96" OLED  │                 │ │
│  │  │  240MHz     │  │   915MHz    │  │   Display   │                 │ │
│  │  └─────────────┘  └─────────────┘  └─────────────┘                 │ │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                 │ │
│  │  │   WiFi      │  │  3000mAh    │  │  Built-in   │                 │ │
│  │  │  802.11n    │  │  Battery    │  │  Button     │                 │ │
│  │  └─────────────┘  └─────────────┘  └─────────────┘                 │ │
│  └────────────────────────────────────────────────────────────────────┘ │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │                         SOFTWARE STACK                              │ │
│  │                                                                     │ │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                 │ │
│  │  │  MicroLink  │  │   Sensor    │  │   Rules     │                 │ │
│  │  │  Tailscale  │  │   Drivers   │  │   Engine    │                 │ │
│  │  └─────────────┘  └─────────────┘  └─────────────┘                 │ │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                 │ │
│  │  │    Mesh     │  │     Web     │  │    Data     │                 │ │
│  │  │    Sync     │  │   Server    │  │   Logger    │                 │ │
│  │  └─────────────┘  └─────────────┘  └─────────────┘                 │ │
│  └────────────────────────────────────────────────────────────────────┘ │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │  External Sensors (via RJ11/RJ45):                                 │ │
│  │  • Any registered sensor driver (DHT22, float switch, etc.)        │ │
│  │  • Hot-pluggable with runtime detection                            │ │
│  └────────────────────────────────────────────────────────────────────┘ │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │  Output Devices:                                                    │ │
│  │  • Buzzer (GPIO 6)                                                 │ │
│  │  • OLED Display (built-in)                                         │ │
│  │  • Web Interface (port 80)                                         │ │
│  └────────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────┘
```

### Node Roles (Configuration-Based)

| Role | Description |
|------|-------------|
| Sensor Node | Reads sensors, reports to mesh |
| Display Node | Shows status, sounds alerts |
| Gateway Node | Bridges to external services (webhooks, email) |
| Any/All | Nodes can fulfill multiple roles simultaneously |

### Remote Access

MicroLink provides Tailscale VPN integration on ESP32-S3:
- Access web interface from anywhere via Tailscale IP
- No port forwarding or dynamic DNS needed
- End-to-end encrypted connection
- Works even when local WiFi unavailable (via LoRa relay)

## Sensor Plugin System

Sensors are implemented as pluggable drivers with a common interface. New sensors can be added without modifying core firmware.

### Driver Interface

```cpp
class SensorDriver {
public:
    // Metadata
    virtual const char* getId() = 0;        // "dht22", "float_switch"
    virtual const char* getName() = 0;       // "DHT22 Temp/Humidity"
    virtual SensorType getType() = 0;        // ANALOG, DIGITAL, I2C, etc.

    // Lifecycle
    virtual bool init(uint8_t pin, JsonObject& config) = 0;
    virtual void shutdown() = 0;

    // Reading
    virtual SensorReading read() = 0;
    virtual bool supportsInterrupt() = 0;
    virtual void attachInterrupt(void (*callback)()) = 0;

    // Metadata for UI/rules
    virtual const char* getUnit() = 0;       // "°F", "%", "bool"
    virtual float getMinValue() = 0;
    virtual float getMaxValue() = 0;
};
```

### Built-in Drivers

| Driver | Type | Interrupt | Unit | Description |
|--------|------|-----------|------|-------------|
| `dht22` | Digital | No | °F, % | Temperature and humidity |
| `float_switch` | Digital | Yes | bool | Water detection |
| `sw420` | Digital | Yes | bool | Vibration (pump activity) |
| `usb_power` | Internal | Yes | bool | AC power detection (ESP32-S3 VBUS) |
| `battery` | Internal | No | % | Battery level (built-in) |

### Sensor Configuration (JSON)

```json
{
  "sensors": [
    {
      "id": "basement_temp",
      "driver": "dht22",
      "pin": 2,
      "poll_interval_ms": 300000,
      "config": {
        "temp_offset": -2.0
      }
    },
    {
      "id": "water_detect",
      "driver": "float_switch",
      "pin": 4,
      "poll_interval_ms": 0,
      "config": {
        "active_low": true
      }
    },
    {
      "id": "pump_vibration",
      "driver": "sw420",
      "pin": 5,
      "poll_interval_ms": 0,
      "config": {
        "debounce_ms": 100
      }
    }
  ]
}
```

### Driver Registry

```cpp
class SensorRegistry {
public:
    void registerDriver(SensorDriver* driver);
    SensorDriver* getDriver(const char* id);
    std::vector<SensorDriver*> listDrivers();

    // Runtime sensor management
    bool addSensor(JsonObject& config);
    bool removeSensor(const char* sensorId);
    std::vector<Sensor*> getActiveSensors();
};
```

## Mesh Communication

Dual-path synchronization using both LoRa and WiFi simultaneously. LoRa provides reliability when WiFi is unavailable; WiFi provides higher bandwidth when available.

### Communication Paths

```
┌──────────────┐                              ┌──────────────┐
│   Node A     │                              │   Node B     │
│  (Basement)  │                              │ (Living Rm)  │
│              │◄────── LoRa 915MHz ─────────►│              │
│              │   (always on, low bandwidth) │              │
│              │                              │              │
│              │◄────── WiFi mDNS ───────────►│              │
│              │  (when available, fast sync) │              │
└──────────────┘                              └──────────────┘
```

### Sync Protocol

1. **LoRa Path (Primary)**
   - Always active, even without WiFi
   - Transmits every 5 minutes + on interrupt events
   - Compact binary packets (8 bytes)
   - Immediate alerts bypass normal interval

2. **WiFi Path (Secondary)**
   - Nodes discover each other via mDNS (`_boorker._tcp`)
   - REST API for bulk sync: `GET /api/v1/readings`
   - WebSocket for real-time updates: `ws://node/sync`
   - Higher bandwidth for logs, configuration, OTA

### Packet Format (LoRa)

```
Byte 0:     Packet type
            0x01 = Status update
            0x02 = Alert (immediate)
            0x03 = Heartbeat
            0x04 = Config sync request
Byte 1:     Node ID (0-255)
Byte 2:     Sequence number (rollover)
Byte 3-N:   Payload (varies by type)
Last byte:  CRC8 checksum
```

**Status Payload (Type 0x01):**
```
Byte 0-1:   Temperature (int16, tenths °F)
Byte 2:     Humidity (uint8, percent)
Byte 3:     Status flags
            Bit 0: Water detected
            Bit 1: Power OK
            Bit 2: Pump running
            Bit 3: Alert active
            Bit 4-7: Reserved
Byte 4:     Battery percent
Byte 5:     Pump run count (last hour)
```

### Conflict Resolution

When both paths deliver data:
- **Sensor readings:** Most recent timestamp wins
- **Configuration:** Higher version number wins
- **Alerts:** Union of all active alerts
- **Logs:** Merge and deduplicate by timestamp + event ID

### Discovery

```cpp
// mDNS advertisement
MDNS.begin("boorker-" + nodeId);
MDNS.addService("boorker", "tcp", 80);
MDNS.addServiceTxt("boorker", "tcp", "version", FIRMWARE_VERSION);
MDNS.addServiceTxt("boorker", "tcp", "role", nodeRole);

// Discover peers
int n = MDNS.queryService("boorker", "tcp");
for (int i = 0; i < n; i++) {
    String host = MDNS.hostname(i);
    IPAddress ip = MDNS.IP(i);
    // Add to peer list
}
```

## Rules Engine & Alerts

Configurable rules system that evaluates conditions and triggers actions. Supports compound conditions and alert backoff to prevent notification fatigue.

### Rule Structure

```json
{
  "rules": [
    {
      "id": "water_alert",
      "name": "Water Detected",
      "enabled": true,
      "conditions": {
        "type": "simple",
        "sensor": "water_detect",
        "operator": "==",
        "value": true
      },
      "actions": [
        {"type": "buzzer", "pattern": "continuous", "duration_sec": 10},
        {"type": "display", "message": "WATER DETECTED!", "priority": "critical"},
        {"type": "webhook", "url": "https://...", "method": "POST"},
        {"type": "email", "to": "user@example.com", "subject": "Basement Alert"}
      ],
      "backoff": {
        "initial_minutes": 1,
        "max_minutes": 60,
        "multiplier": 2
      }
    }
  ]
}
```

### Condition Types

**Simple Condition:**
```json
{
  "type": "simple",
  "sensor": "basement_temp",
  "operator": "<",
  "value": 40
}
```

**Compound Condition (AND):**
```json
{
  "type": "and",
  "conditions": [
    {"sensor": "humidity", "operator": ">", "value": 80},
    {"sensor": "temp", "operator": "<", "value": 50}
  ]
}
```

**Compound Condition (OR):**
```json
{
  "type": "or",
  "conditions": [
    {"sensor": "water_detect", "operator": "==", "value": true},
    {"sensor": "power_ok", "operator": "==", "value": false}
  ]
}
```

**Duration Condition:**
```json
{
  "type": "duration",
  "sensor": "pump_running",
  "operator": "==",
  "value": true,
  "for_minutes": 5
}
```

### Supported Operators

| Operator | Description |
|----------|-------------|
| `==` | Equal |
| `!=` | Not equal |
| `<` | Less than |
| `>` | Greater than |
| `<=` | Less than or equal |
| `>=` | Greater than or equal |
| `changed` | Value changed since last reading |

### Action Types

| Action | Description |
|--------|-------------|
| `buzzer` | Sound local buzzer (patterns: continuous, beep, chirp) |
| `display` | Show message on OLED and web interface |
| `webhook` | HTTP POST/GET to external URL |
| `email` | Send email via configured SMTP |
| `lora_alert` | Broadcast alert to mesh (immediate, bypasses interval) |
| `log` | Write to data log |

### Alert Backoff

Prevents notification fatigue during persistent conditions:

```
Alert triggered → Action fires
                → Wait initial_minutes (1 min)
                → Condition still true? → Action fires again
                → Wait initial * multiplier (2 min)
                → Condition still true? → Action fires again
                → Wait 4 min, 8 min, 16 min... up to max_minutes (60 min)
                → Condition clears → Reset backoff timer
```

### Built-in Alert Thresholds

| Sensor | Warning | Critical |
|--------|---------|----------|
| Water | Any detection | N/A (always critical) |
| AC Power | N/A | Power loss |
| Temperature | < 40°F or > 90°F | < 32°F or > 100°F |
| Humidity | > 80% | > 95% |
| Pump | Running > 5 min | Not run in 7 days |
| No signal | > 15 min | > 30 min |

### Acknowledgment

- Press built-in button to acknowledge alert
- Silences buzzer immediately
- Visual alert remains until condition clears
- Resets backoff timer (next alert fires immediately if condition persists)

## Display System

Two display surfaces: physical OLED on each node and a unified web interface accessible from any node.

### OLED Display (128x64)

**Normal State:**
```
┌────────────────────────┐
│ BASEMENT     12:34 PM  │
│ ──────────────────────│
│  Temp: 62°F   Hum: 45% │
│  Water: DRY   Pwr: OK  │
│  Pump: Idle            │
│ ──────────────────────│
│  Status: NORMAL   87%  │
└────────────────────────┘
```

**Alert State:**
```
┌────────────────────────┐
│ !! ALERT !!   12:34 PM │
│ ══════════════════════│
│                        │
│   WATER DETECTED!      │
│                        │
│ ══════════════════════│
│  Press button to ack   │
└────────────────────────┘
```

### Web Interface

Accessible at `http://<node-ip>/` or via Tailscale IP. Served by any node, shows mesh-wide data.

**Dashboard Features:**
- Real-time sensor values (WebSocket updates)
- Virtual OLED display rendering
- Alert history and status
- Sensor configuration UI
- Rules editor
- Data logs with graphs
- Node status (battery, signal strength, last seen)
- OTA firmware update interface

**API Endpoints:**

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/v1/readings` | GET | All current sensor readings |
| `/api/v1/readings/:id` | GET | Specific sensor reading |
| `/api/v1/alerts` | GET | Active alerts |
| `/api/v1/alerts/:id/ack` | POST | Acknowledge alert |
| `/api/v1/config` | GET/PUT | Node configuration |
| `/api/v1/rules` | GET/POST/PUT/DELETE | Rules CRUD |
| `/api/v1/logs` | GET | Historical data |
| `/api/v1/nodes` | GET | Mesh node status |
| `/ws` | WebSocket | Real-time updates |

### Virtual Display Rendering

Web interface renders a pixel-perfect OLED emulation:

```html
<canvas id="oled-display" width="128" height="64"></canvas>
```

JavaScript receives display buffer updates via WebSocket and renders to canvas, matching physical OLED appearance.

## Data Logging

Tiered storage strategy balances detail with limited flash storage.

### Storage Tiers

| Tier | Retention | Resolution | Storage |
|------|-----------|------------|---------|
| Hot | 24 hours | Every reading | ~50KB |
| Warm | 7 days | Hourly averages | ~10KB |
| Cold | 30 days | Daily averages | ~2KB |

### Rollup Process

- Runs hourly on each node
- Hot → Warm: Compute hourly min/max/avg
- Warm → Cold: Compute daily min/max/avg
- Deletes expired data

### Log Format (SPIFFS/LittleFS)

```
/data/
  hot/
    2026-02-27.jsonl    # Line-delimited JSON
  warm/
    2026-02-W09.jsonl   # Week 9 hourly data
  cold/
    2026-02.jsonl       # Monthly daily data
```

## OTA Updates

Firmware updates delivered via web interface or triggered remotely.

### Update Flow

1. Upload firmware binary to any node's web interface
2. Node validates binary (magic bytes, size, checksum)
3. Node broadcasts update availability to mesh
4. Each node downloads binary via WiFi (or LoRa chunks if WiFi unavailable)
5. Nodes update sequentially (one at a time for safety)
6. Automatic rollback if boot fails 3 times

### Version Management

```json
{
  "firmware_version": "1.2.3",
  "min_compatible_version": "1.0.0",
  "changelog": "Added webhook support"
}
```

## Hardware Pin Assignments

### Heltec V3 Built-in Components

| Component | GPIO | Notes |
|-----------|------|-------|
| OLED Display | I2C (SDA 17, SCL 18) | 0.96" 128x64, reset on GPIO 21 |
| LoRa SX1262 | SPI (NSS 8, RST 12, BUSY 13, DIO1 14) | 915MHz, 21dBm |
| User Button (PRG) | GPIO 0 | Active LOW, alert acknowledge |
| White LED | GPIO 35 | Status indicator |
| Battery ADC | GPIO 1 | Via voltage divider |
| Vext Control | GPIO 36 | External sensor power control |
| Reset Button | N/A | Hardware reset (not programmable) |

### Basement Node - External Connections

| Pin | Function | Notes |
|-----|----------|-------|
| GPIO 2 | DHT22 | 10K pullup |
| GPIO 4 | Float switch | Internal pullup, interrupt |
| GPIO 5 | SW-420 vibration | Interrupt |
| GPIO 6 | Buzzer | Active buzzer |
| JST 1.25 | Battery | 3000mAh |

### USB Power Detection (ESP32-S3 Native)

The ESP32-S3 can detect USB VBUS state directly without external circuitry:

```cpp
#include "soc/usb_serial_jtag_struct.h"

bool isUsbPowerPresent() {
    return USB_SERIAL_JTAG.conf0.usb_pad_enable &&
           (USB_SERIAL_JTAG.conf0.vrefh_sel != 0);
}

// Or using Arduino-ESP32 API (simpler)
#include "USB.h"
bool isUsbPowerPresent() {
    return USB.connected();
}
```

When USB power is lost, the driver triggers an immediate alert via the rules engine.

### Display Node - External Connections

| Pin | Function | Notes |
|-----|----------|-------|
| GPIO 2 | DHT22 | Room temp/humidity |
| GPIO 6 | Buzzer | Active buzzer |
| JST 1.25 | Battery | Backup |

### LED Behavior

| Pattern | Meaning |
|---------|---------|
| Solid | WiFi connected, normal operation |
| Slow blink (1s) | LoRa only mode (no WiFi) |
| Fast blink (200ms) | Transmitting/receiving |
| Double pulse | Alert active |
| Off | Deep sleep |

### Button Behavior (PRG/GPIO 0)

| Action | Function |
|--------|----------|
| Short press | Acknowledge active alert |
| Long press (3s) | Cycle display mode |
| Very long press (10s) | Enter config mode (AP) |

## Libraries & Dependencies

| Library | Purpose |
|---------|---------|
| RadioLib | LoRa (SX1262) |
| U8g2 | OLED display |
| ArduinoJson | Configuration, API |
| ESPAsyncWebServer | Web interface |
| AsyncTCP | WebSocket |
| MicroLink | Tailscale VPN |
| DHT sensor library | DHT22 driver |
| LittleFS | Data storage |

## Future Enhancements (Out of Scope for MVP)

- Multiple sensor zones (>2 nodes)
- Historical graphs on OLED
- Integration with Home Assistant
- LoRa mesh routing (>2 hops)
- SD card logging
- SMS alerts via Twilio
