# Phase 1: Unboxing & Power On

## US-001: Power On and See Status

**As a** new user who just unboxed the device
**I want to** plug it in and immediately see it's working
**So that** I have confidence the hardware is functioning before configuration

### Scenario
1. User removes device from packaging
2. User connects power via USB-C
3. Device boots (LED flashes during boot sequence)
4. OLED displays boot progress and initial status

### OLED Screens

```
┌────────────────────┐
│   BOORKER WATCHDOG │
│                    │
│   Booting...       │
│   ████████░░ 80%   │
└────────────────────┘
```

After boot, device enters setup mode with QR code:

```
┌────────────────────┐
│ ◉ SETUP MODE       │
│ ┌────────┐         │
│ │ QR     │ WiFi AP │
│ │ CODE   │ or BLE  │
│ └────────┘         │
│ Boorker-A1B2-Setup │
│ Pass: boorker123   │
└────────────────────┘
```

### Acceptance Criteria
- [ ] Device powers on within 3 seconds of connecting power
- [ ] LED indicates boot progress (pulsing cyan)
- [ ] OLED shows boot progress bar
- [ ] After boot, OLED shows QR code for quick setup
- [ ] OLED shows both AP and BLE connection options
- [ ] Credentials displayed below QR code as fallback
- [ ] Generated password matches device label

### UI Pages
- None (pre-authentication)

---

## US-002: Connect to Device (AP or Bluetooth)

**As a** new user setting up the device
**I want to** connect via WiFi AP or Bluetooth
**So that** I can choose the most convenient method for my setup

### Option A: WiFi AP Connection

1. User scans QR code (opens WiFi settings) OR manually connects
2. User connects phone/laptop to "Boorker-A1B2-Setup" network
3. Captive portal redirects to setup page (or navigate to 192.168.4.1)
4. Login page loads

### Option B: Bluetooth Connection

1. User scans QR code (opens BLE pairing) OR pairs manually
2. Mobile app connects via BLE
3. App presents setup interface
4. Configuration sent over BLE, device joins WiFi

### OLED Screens

When client connects via AP:
```
┌────────────────────┐
│ ◉ SETUP MODE       │
│                    │
│ WiFi: 1 connected  │
│ IP: 192.168.4.1    │
│                    │
│ Waiting for setup  │
└────────────────────┘
```

When client connects via BLE:
```
┌────────────────────┐
│ ◉ SETUP MODE       │
│                    │
│ BLE: Paired        │
│ Device: iPhone     │
│                    │
│ Waiting for setup  │
└────────────────────┘
```

### Acceptance Criteria
- [ ] QR code encodes both AP credentials and BLE pairing info
- [ ] AP broadcasts SSID matching device ID pattern
- [ ] AP password is unique per device
- [ ] BLE advertises with device name
- [ ] BLE uses secure pairing with PIN (shown on OLED)
- [ ] Web interface accessible at 192.168.4.1 (AP mode)
- [ ] OLED updates to show connection method and client info
- [ ] Setup works identically via either connection method

### UI Pages
- `spa-login.html` (Set Password page)

### UI Verification Checklist
- [ ] Login page loads on mobile viewport (AP mode)
- [ ] Generated password field accepts credentials from OLED/label
- [ ] Form is touch-friendly (large tap targets)
- [ ] Both connection paths lead to same first-boot experience
