# Phase 6: Troubleshooting

## US-024: Diagnose Offline Node

**As a** user with a mesh network
**I want to** diagnose why a node went offline
**So that** I can restore monitoring coverage

### Scenario
1. User notices missing sensor data on dashboard
2. User opens Mesh page
3. Offline node shown with last-seen time
4. User checks signal history
5. User identifies issue (range, power, interference)

### OLED Screens (on coordinator)

```
┌────────────────────┐
│ ⚠ NODE OFFLINE    │
│                    │
│ boorker-c3d4       │
│ Last seen: 5m ago  │
│ Last RSSI: -78 dBm │
│                    │
└────────────────────┘
```

### Acceptance Criteria
- [ ] Mesh page shows node status clearly
- [ ] Offline nodes have distinct visual state
- [ ] Last seen timestamp shown
- [ ] Last known signal strength shown
- [ ] Signal history graph available
- [ ] Ping/health check button
- [ ] Troubleshooting suggestions shown
- [ ] Alert triggered for node offline

### UI Pages
- `spa-mesh.html`
- `spa-terminal.html` (for ping commands)

### UI Verification Checklist
- [ ] Node cards show online/offline state
- [ ] Offline styling is distinct (greyed, warning badge)
- [ ] Last seen timestamp renders
- [ ] Signal strength indicator works
- [ ] Ping button triggers command

---

## US-025: Reconnect WiFi

**As a** user whose device lost WiFi connection
**I want to** reconnect to my network
**So that** remote monitoring is restored

### Scenario A: Device has AP fallback
1. User connects to device AP (still running)
2. User opens web UI at 192.168.4.1
3. User selects new/same network
4. Device reconnects to station WiFi

### Scenario B: Physical button
1. User presses WiFi reset button (long press)
2. Device enters setup mode
3. OLED shows AP credentials
4. User connects and reconfigures

### OLED Screens

WiFi disconnected:
```
┌────────────────────┐
│ ⚠ WiFi OFFLINE    │
│                    │
│ Station: ✗        │
│ AP Mode: Active    │
│ IP: 192.168.4.1    │
│                    │
└────────────────────┘
```

Reconnecting:
```
┌────────────────────┐
│ ◐ WiFi CONNECTING │
│                    │
│ HomeNetwork-5G     │
│ Attempt 2 of 5     │
│                    │
│ ████░░░░░░ 40%     │
└────────────────────┘
```

### Acceptance Criteria
- [ ] AP mode stays active when station fails
- [ ] OLED shows connection status
- [ ] Settings page shows WiFi status
- [ ] Rescan networks button available
- [ ] Connection progress shown
- [ ] Fallback to AP if station fails
- [ ] Long-press button enters setup mode
- [ ] mDNS hostname updates after reconnect

### UI Pages
- `spa-settings.html` (WiFi section)

---

## US-026: Debug Sensor Issue

**As a** user with a malfunctioning sensor
**I want to** diagnose sensor problems
**So that** I can fix wiring or configuration issues

### Scenario
1. User notices sensor shows error state
2. User opens Terminal page
3. User runs sensor diagnostic command
4. Output shows GPIO state, raw readings
5. User identifies issue (wiring, configuration)

### OLED Screens

Sensor error:
```
┌────────────────────┐
│ ⚠ SENSOR ERROR    │
│                    │
│ basement_temp      │
│ DHT22 • GPIO 2     │
│                    │
│ No response        │
└────────────────────┘
```

### Acceptance Criteria
- [ ] Sensor cards show error state clearly
- [ ] Error message indicates failure type
- [ ] Terminal has sensor debug commands
- [ ] Raw GPIO read available
- [ ] I2C/OneWire scan commands
- [ ] Force read button triggers immediate poll
- [ ] OLED shows sensor errors
- [ ] Error events logged to history

### UI Pages
- `spa-sensors.html`
- `spa-terminal.html`

### UI Verification Checklist
- [ ] Error state styling is distinct
- [ ] Error message displays
- [ ] Configure button still accessible
- [ ] Force Read button works
- [ ] Terminal shows sensor commands in help

