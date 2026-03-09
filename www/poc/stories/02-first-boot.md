# Phase 2: First Boot & Initial Setup

## US-003: Claim Device with Password

**As a** new user completing initial setup
**I want to** change the generated password to my own
**So that** I secure the device and can remember my credentials

### Scenario
1. User navigates to device web UI (via AP or after BLE setup)
2. Device detects FIRST_BOOT state, redirects to Change Password
3. User enters generated password (from OLED/label) as current password
4. User enters and confirms new password
5. Device saves credentials, exits FIRST_BOOT state

### OLED Screens

During password setup:
```
┌────────────────────┐
│ ◉ SETUP MODE       │
│                    │
│ Password change    │
│ in progress...     │
│                    │
│ Do not power off   │
└────────────────────┘
```

After password set:
```
┌────────────────────┐
│ ✓ PASSWORD SET     │
│                    │
│ Launching setup    │
│ wizard...          │
│                    │
└────────────────────┘
```

### Acceptance Criteria
- [ ] FIRST_BOOT state detected, redirects to password setup
- [ ] Generated password required (proves physical access)
- [ ] New password minimum 8 characters enforced
- [ ] Confirm password field prevents typos
- [ ] OLED shows progress during credential save
- [ ] Redirects to setup wizard on success

### UI Pages
- `spa-login.html` (Set Password tab)

### UI Verification Checklist
- [ ] "Set Password" page renders with all fields
- [ ] Username field shows "admin" (readonly)
- [ ] Generated password field accepts OLED value
- [ ] Validation errors display clearly
- [ ] Loading state shown during submission

---

## US-004: Complete Setup Wizard

**As a** new user who just set their password
**I want to** be guided through essential configuration
**So that** my device is properly set up without reading manuals

### Scenario
1. Wizard starts automatically after password set
2. User progresses through 5 steps
3. Can skip non-essential steps
4. Final screen summarizes configuration

### Wizard Steps
1. Welcome - explains what device does
2. Device Identity - name and role
3. WiFi - connect to home network
4. Sensors - review and enable detected sensors
5. Alerts - LED and buzzer preferences

### OLED Screens

During wizard (synced with web progress):
```
┌────────────────────┐
│ SETUP: Step 2/5    │
│ ════════░░░░░░░░░░ │
│                    │
│ Configuring...     │
│ Device Identity    │
└────────────────────┘
```

### Acceptance Criteria
- [ ] Wizard auto-launches after password setup
- [ ] Progress bar shows current step
- [ ] Back/Next navigation works
- [ ] Skip option available for WiFi (if already on AP)
- [ ] Summary shows all configured values
- [ ] OLED syncs with wizard progress

### UI Pages
- `spa-wizard.html`

---

## US-005: Configure WiFi

**As a** user in the setup wizard
**I want to** connect my device to my home WiFi
**So that** I can access it from my normal network

### Scenario
1. Wizard shows WiFi step
2. Device scans for networks
3. User selects network, enters password
4. Device connects and gets IP
5. User may need to reconnect to home network

### OLED Screens

Scanning:
```
┌────────────────────┐
│ SETUP: WiFi        │
│                    │
│ Scanning networks  │
│ ◐ ◓ ◑ ◒           │
│                    │
└────────────────────┘
```

Connecting:
```
┌────────────────────┐
│ SETUP: WiFi        │
│                    │
│ Connecting to:     │
│ HomeNetwork-5G     │
│ ████████░░ 80%     │
└────────────────────┘
```

Connected:
```
┌────────────────────┐
│ ✓ WiFi CONNECTED   │
│                    │
│ HomeNetwork-5G     │
│ IP: 192.168.1.42   │
│ boorker-a1b2.local │
└────────────────────┘
```

### Acceptance Criteria
- [ ] Network scan shows available SSIDs
- [ ] Signal strength indicated for each network
- [ ] Hidden network manual entry option
- [ ] Password field with show/hide toggle
- [ ] Connection progress shown
- [ ] New IP address displayed on success
- [ ] mDNS hostname shown for easy access
- [ ] OLED shows connection progress and final IP

### UI Pages
- `spa-wizard.html` (Step 3)

---

## US-006: Name and Locate Device

**As a** user setting up the device
**I want to** give it a meaningful name and location
**So that** I can identify it among multiple nodes

### Scenario
1. Wizard shows Device Identity step
2. User enters friendly name (e.g., "Basement Monitor")
3. User enters location description
4. User selects role (Coordinator or Node)

### Acceptance Criteria
- [ ] Device name field with placeholder examples
- [ ] Location field for descriptive text
- [ ] Role selector (Coordinator/Node)
- [ ] Tutorial explains coordinator vs node
- [ ] Name appears in header after setup

### UI Pages
- `spa-wizard.html` (Step 2)

---

## US-007: Review Detected Sensors

**As a** user completing setup
**I want to** see which sensors the device detected
**So that** I know what's working and can enable monitoring

### Scenario
1. Wizard shows Sensors step
2. Auto-detected sensors listed with status
3. User enables/disables each sensor
4. Live readings shown for detected sensors

### OLED Screens

```
┌────────────────────┐
│ SETUP: Sensors     │
│                    │
│ ✓ DHT22  22.3°C   │
│ ✓ SW-420 Idle     │
│ ✗ Float  No signal│
└────────────────────┘
```

### Acceptance Criteria
- [ ] Auto-detection runs on GPIO pins
- [ ] Each sensor shows type, GPIO, status
- [ ] Live readings for working sensors
- [ ] Enable/disable toggle for each
- [ ] Not-detected sensors show troubleshooting hint
- [ ] OLED shows sensor detection summary

### UI Pages
- `spa-wizard.html` (Step 4)

---

## US-008: Set Alert Preferences

**As a** user completing setup
**I want to** configure how the device alerts me
**So that** notifications match my preferences

### Scenario
1. Wizard shows Alerts step
2. User enables/disables LED indicator
3. User enables/disables buzzer
4. User selects buzzer sound pattern
5. Tutorial explains alert behavior

### Acceptance Criteria
- [ ] LED enable toggle with preview
- [ ] Buzzer enable toggle
- [ ] Sound pattern selector (Continuous, Double Beep, etc.)
- [ ] Test button to preview buzzer
- [ ] Tutorial explains when alerts trigger

### UI Pages
- `spa-wizard.html` (Step 5)
