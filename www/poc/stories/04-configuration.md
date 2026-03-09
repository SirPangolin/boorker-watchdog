# Phase 4: Configuration & Expansion

## US-014: Add New Sensor

**As a** user expanding monitoring capability
**I want to** add a new sensor to the system
**So that** I can monitor additional conditions

### Scenario
1. User physically connects sensor to GPIO
2. User opens Sensors page
3. User clicks "Add Sensor"
4. Dialog shows sensor type selection
5. User configures GPIO, name, thresholds
6. Sensor appears in list

### Acceptance Criteria
- [ ] Add Sensor button visible on Sensors page
- [ ] Dialog shows supported sensor types
- [ ] GPIO pin selector available
- [ ] Name field with validation
- [ ] Threshold configuration (if applicable)
- [ ] Poll interval setting
- [ ] Test button to verify wiring
- [ ] New sensor appears immediately

### UI Pages
- `spa-sensors.html` (Add Sensor dialog)

### UI Verification Checklist
- [ ] Add Sensor button renders
- [ ] Dialog opens on click
- [ ] Sensor type dropdown populated
- [ ] GPIO selector works
- [ ] Form validation prevents invalid input
- [ ] Cancel/Save buttons functional

---

## US-015: Configure Sensor Thresholds

**As a** user fine-tuning monitoring
**I want to** adjust alert thresholds
**So that** alerts are meaningful for my environment

### Scenario
1. User clicks Configure on sensor card
2. Config dialog shows current thresholds
3. User adjusts min/max values
4. User saves configuration
5. New thresholds take effect immediately

### Acceptance Criteria
- [ ] Configure button on each sensor card
- [ ] Dialog shows current threshold values
- [ ] Min/Max fields for temperature sensors
- [ ] Debounce setting for binary sensors
- [ ] Hysteresis option to prevent flapping
- [ ] Save applies immediately
- [ ] History shows threshold change event

### UI Pages
- `spa-sensors.html` (Config dialog)

---

## US-016: Add Mesh Node

**As a** user expanding coverage
**I want to** add another device to my mesh network
**So that** I can monitor additional locations

### Scenario
1. User powers on new node
2. New node enters setup mode
3. User configures new node (same wizard)
4. New node joins mesh network
5. Coordinator shows new node in mesh view

### OLED Screens (on new node):

```
┌────────────────────┐
│ ◉ JOINING MESH     │
│                    │
│ Scanning for       │
│ coordinator...     │
│ ◐ ◓ ◑ ◒           │
└────────────────────┘
```

```
┌────────────────────┐
│ ✓ MESH JOINED      │
│                    │
│ Coordinator:       │
│ boorker-a1b2       │
│ RSSI: -42 dBm      │
│ Channel: 1         │
└────────────────────┘
```

### OLED Screens (on coordinator):

```
┌────────────────────┐
│ + NEW NODE         │
│                    │
│ boorker-c3d4       │
│ RSSI: -42 dBm      │
│                    │
│ [Accept] [Reject]  │
└────────────────────┘
```

### Acceptance Criteria
- [ ] New node discovers coordinator automatically
- [ ] Mesh channel and encryption must match
- [ ] Coordinator shows join request
- [ ] Accept/reject on coordinator (OLED or web)
- [ ] Mesh page updates with new node
- [ ] Node sensors appear in coordinator view
- [ ] Signal strength shown

### UI Pages
- `spa-mesh.html`
- `spa-wizard.html` (on new node)

---

## US-017: Configure LoRa Parameters

**As a** user optimizing mesh performance
**I want to** adjust LoRa radio settings
**So that** I can balance range vs speed for my setup

### Scenario
1. User opens Settings > LoRa Mesh Network
2. User adjusts spreading factor, bandwidth, TX power
3. Changes require all nodes to update
4. User confirms mesh-wide change

### Acceptance Criteria
- [ ] LoRa settings section in Settings page
- [ ] Frequency band selector (region-locked)
- [ ] Spreading factor SF7-SF12
- [ ] Bandwidth 125/250/500 kHz
- [ ] TX power selection
- [ ] Warning that all nodes must match
- [ ] Mesh channel number configuration
- [ ] Encryption key management

### UI Pages
- `spa-settings.html` (LoRa Mesh Network section)

### UI Verification Checklist
- [ ] LoRa section renders
- [ ] All dropdowns populated with options
- [ ] Encryption key field has show/change buttons

---

## US-018: Set Up Bluetooth Pairing

**As a** user wanting mobile app access
**I want to** pair my phone via Bluetooth
**So that** I can use the mobile app for monitoring

### Scenario
1. User opens Settings > Bluetooth
2. User enables Discoverable mode
3. User notes PIN code (or generates new)
4. User pairs phone using PIN
5. Device shows paired device

### OLED Screens

```
┌────────────────────┐
│ BLE PAIRING        │
│                    │
│ PIN: 847291        │
│                    │
│ Waiting for        │
│ connection...      │
└────────────────────┘
```

```
┌────────────────────┐
│ ✓ BLE PAIRED       │
│                    │
│ Device: iPhone 14  │
│ Paired at 3:45pm   │
│                    │
└────────────────────┘
```

### Acceptance Criteria
- [ ] Bluetooth section in Settings
- [ ] Enable/disable Bluetooth toggle
- [ ] Device name configuration
- [ ] Discoverable mode toggle
- [ ] PIN code display with generate button
- [ ] Paired devices list
- [ ] Remove pairing option
- [ ] OLED shows pairing PIN when discoverable

### UI Pages
- `spa-settings.html` (Bluetooth section)

---

## US-019: Configure AP Mode

**As a** user wanting fallback access
**I want to** configure the device's access point
**So that** I can always connect even if home WiFi fails

### Scenario
1. User opens Settings > WiFi (Access Point Mode)
2. User enables AP mode
3. User configures SSID and password
4. User sets channel and max connections
5. AP becomes available alongside station mode

### Acceptance Criteria
- [ ] AP Mode section in Settings
- [ ] Enable/disable toggle
- [ ] SSID configuration
- [ ] Password field with show toggle
- [ ] Channel selection
- [ ] Max connections setting
- [ ] Hidden SSID option
- [ ] Connected clients count shown

### UI Pages
- `spa-settings.html` (WiFi Access Point Mode section)
