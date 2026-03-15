# Phase 5: Maintenance

## US-020: Update Firmware (OTA)

**As a** user keeping the system current
**I want to** update firmware over-the-air
**So that** I get new features and security fixes without physical access

### Scenario
1. User opens Settings > System
2. System shows current version and checks for updates
3. User sees update available, clicks Update
4. Progress shown on web and OLED
5. Device reboots with new firmware

### OLED Screens

Update available notification:
```
┌────────────────────┐
│ ◉ UPDATE AVAILABLE │
│                    │
│ v1.2.3 → v1.3.0    │
│                    │
│ Press button or    │
│ use web UI         │
└────────────────────┘
```

During update:
```
┌────────────────────┐
│ ⚠ UPDATING...     │
│ ═══════════════════│
│                    │
│ DO NOT POWER OFF   │
│                    │
│ ████████░░ 80%     │
└────────────────────┘
```

After update:
```
┌────────────────────┐
│ ✓ UPDATE COMPLETE  │
│                    │
│ v1.3.0 installed   │
│                    │
│ Rebooting in 3s... │
└────────────────────┘
```

### Acceptance Criteria
- [ ] Current version displayed in Settings
- [ ] Check for updates button
- [ ] Update available indicator (badge)
- [ ] Release notes shown before update
- [ ] Progress bar during download
- [ ] Progress bar during flash
- [ ] OLED shows update progress
- [ ] Automatic reboot after update
- [ ] Rollback option if update fails

### UI Pages
- `spa-settings.html` (System section)

---

## US-021: Reboot Device

**As a** user troubleshooting issues
**I want to** remotely reboot the device
**So that** I can resolve problems without physical access

### Scenario
1. User opens Settings > System
2. User clicks Reboot
3. Confirmation dialog appears
4. Device reboots, reconnects
5. User refreshes page to reconnect

### OLED Screens

```
┌────────────────────┐
│ REBOOTING...       │
│                    │
│ Requested by       │
│ web interface      │
│                    │
│ Please wait...     │
└────────────────────┘
```

### Acceptance Criteria
- [ ] Reboot button in System section
- [ ] Confirmation dialog prevents accidents
- [ ] OLED shows reboot source (web/button)
- [ ] Web UI shows disconnected state
- [ ] Automatic reconnection attempt
- [ ] Uptime counter resets after reboot

### UI Pages
- `spa-settings.html` (System section)

---

## US-022: View System Diagnostics

**As a** user debugging issues
**I want to** see detailed system information
**So that** I can diagnose problems or provide info to support

### Scenario
1. User opens Terminal page
2. Diagnostic commands available
3. User runs memory, network, sensor diagnostics
4. Results displayed in terminal
5. User can copy output for support

### Acceptance Criteria
- [ ] Terminal page accessible from nav
- [ ] Built-in diagnostic commands (help shows list)
- [ ] Memory usage display
- [ ] Network connectivity test
- [ ] Sensor health check
- [ ] LoRa radio diagnostics
- [ ] Copy output button
- [ ] Command history navigation

### UI Pages
- `spa-terminal.html`

### UI Verification Checklist
- [ ] Terminal renders with prompt
- [ ] Commands execute and show output
- [ ] Scrollback works
- [ ] Copy button functional
- [ ] Node selector works for mesh

---

## US-023: Export Event History

**As a** user analyzing patterns
**I want to** export event history to CSV
**So that** I can analyze data in spreadsheets or archive it

### Scenario
1. User opens History page
2. User sets filter criteria (optional)
3. User clicks Export CSV
4. Browser downloads file
5. File contains filtered events

### Acceptance Criteria
- [ ] Export button on History page
- [ ] Export respects current filters
- [ ] CSV includes all columns
- [ ] Timestamps in ISO format
- [ ] Filename includes date range
- [ ] Large exports show progress
- [ ] Export includes source node info

### UI Pages
- `spa-history.html`

