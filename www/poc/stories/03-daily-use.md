# Phase 3: Daily Use

## US-009: Glance at System Status

**As a** user passing by the device
**I want to** quickly see if everything is normal
**So that** I know the system is working without opening an app

### Scenario A: OLED Glance
1. User looks at device OLED
2. OLED shows current status summary
3. Green indicators = all good, Red = needs attention

### Scenario B: Dashboard Glance
1. User opens web dashboard
2. Stats cards show current readings
3. Alert banner shown if active alerts

### OLED Screens

Normal operation:
```
┌────────────────────┐
│ ◉ ONLINE    3:42pm │
│                    │
│ Temp: 68.4°F  OK   │
│ Humid: 52%    OK   │
│ Vibration: Idle    │
│ Mesh: 3/3 nodes ✓  │
└────────────────────┘
```

Alert active:
```
┌────────────────────┐
│ ⚠ ALERT    3:42pm │
│                    │
│ WATER DETECTED     │
│ basement_float     │
│                    │
│ [Press to ack]     │
└────────────────────┘
```

### Acceptance Criteria
- [ ] OLED shows time and connection status
- [ ] Key sensor readings displayed
- [ ] Mesh node count shown
- [ ] Alert state clearly visible (color/icon change)
- [ ] Dashboard loads quickly (<2s)
- [ ] Stats cards show live values
- [ ] Trend indicators show direction of change

### UI Pages
- `spa-dashboard.html`

### UI Verification Checklist
- [ ] Stats cards render with current values
- [ ] Trend indicators (up/down arrows) visible
- [ ] Mesh summary shows node count
- [ ] Page loads without JavaScript errors

---

## US-010: View Sensor Readings

**As a** user monitoring conditions
**I want to** see detailed sensor readings
**So that** I understand current environmental conditions

### Scenario
1. User opens Sensors page (or taps sensor on dashboard)
2. Sensor cards show live readings
3. User can see reading history count
4. Metadata shows GPIO, poll interval

### OLED Screens (scroll through sensors with button):

```
┌────────────────────┐
│ SENSOR 1/3         │
│ basement_temp      │
│                    │
│ 68.4°F   52%       │
│ DHT22 • GPIO 2     │
│ Updated: 5s ago    │
└────────────────────┘
```

### Acceptance Criteria
- [ ] All enabled sensors shown
- [ ] Live readings update automatically
- [ ] Metadata shows configuration
- [ ] Source node indicated for mesh sensors
- [ ] OLED can scroll through sensors
- [ ] Force Read button triggers immediate poll

### UI Pages
- `spa-dashboard.html` (stats cards)
- `spa-sensors.html` (detailed view)

---

## US-011: Receive Alert Notification

**As a** user away from the device
**I want to** be notified when a threshold is exceeded
**So that** I can respond to issues promptly

### Scenario
1. Sensor reading exceeds threshold
2. Device activates LED (flashing red)
3. Device activates buzzer (configured pattern)
4. OLED shows alert details
5. Dashboard shows alert banner
6. Event logged to history

### OLED Screens

```
┌────────────────────┐
│ ⚠ ALERT            │
│ ═══════════════════│
│ VIBRATION DETECTED │
│ pump_vibration     │
│                    │
│ Since: 2:45pm      │
│ [Press to ack]     │
└────────────────────┘
```

### Acceptance Criteria
- [ ] LED changes to alert pattern (red flash)
- [ ] Buzzer sounds with configured pattern
- [ ] OLED immediately shows alert
- [ ] Dashboard alert banner appears
- [ ] Alert banner shows source sensor and time
- [ ] Event logged with timestamp

### UI Pages
- `spa-dashboard.html` (alert banner)

---

## US-012: Acknowledge Alert

**As a** user who received an alert
**I want to** acknowledge it to silence notifications
**So that** I can investigate without ongoing buzzer noise

### Scenario A: Physical Ack
1. User presses button on device
2. Buzzer silences
3. LED changes to acknowledged state (amber)
4. OLED shows ack confirmation

### Scenario B: Web Ack
1. User clicks "Acknowledge" on dashboard
2. Request sent to device
3. Buzzer silences, LED changes
4. Alert banner shows acknowledged state

### OLED Screens

After acknowledgment:
```
┌────────────────────┐
│ ◉ ACKNOWLEDGED     │
│                    │
│ VIBRATION ALERT    │
│ Ack'd at 2:47pm    │
│                    │
│ [Condition active] │
└────────────────────┘
```

### Acceptance Criteria
- [ ] Physical button acknowledges alert
- [ ] Web UI acknowledge button works
- [ ] Buzzer silences immediately on ack
- [ ] LED changes to amber (ack'd but active)
- [ ] Alert banner updates to show ack'd state
- [ ] Ack event logged with timestamp
- [ ] Alert can re-trigger if condition clears and recurs

### UI Pages
- `spa-dashboard.html` (alert banner with Acknowledge button)

---

## US-013: Check Event History

**As a** user reviewing past activity
**I want to** see historical events and alerts
**So that** I can understand patterns and diagnose issues

### Scenario
1. User opens History page
2. Stats show event counts (24h)
3. Mini chart shows event distribution
4. Table shows recent events with details
5. User filters by type, node, or time

### Acceptance Criteria
- [ ] Event stats show alerts/warnings/info counts
- [ ] Mini chart visualizes event distribution
- [ ] Table shows event type, message, source, time
- [ ] Source node column shows which device
- [ ] Filter by event type (chip toggles)
- [ ] Filter by node (dropdown)
- [ ] Filter by time range
- [ ] Search box filters by text
- [ ] Pagination for large histories
- [ ] Export to CSV available

### UI Pages
- `spa-history.html`

### UI Verification Checklist
- [ ] Stats cards render correctly
- [ ] Chart bars appear with correct heights
- [ ] Filter chips toggle properly
- [ ] Node filter dropdown populated
- [ ] Table rows show all columns
- [ ] Pagination controls work
