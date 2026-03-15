# Phase 7: Long-term Use

## US-027: Change Password

**As a** security-conscious user
**I want to** change my password periodically
**So that** my device remains secure over time

### Scenario
1. User opens Login page
2. User clicks "Change Password" link
3. User enters current password
4. User enters and confirms new password
5. Session continues with new credentials

### Acceptance Criteria
- [ ] Change Password accessible from login page
- [ ] Current password required
- [ ] New password minimum 8 characters
- [ ] Confirm password field
- [ ] Clear validation errors
- [ ] Success message shown
- [ ] Session maintained after change
- [ ] Other sessions optionally invalidated

### UI Pages
- `spa-login.html` (Change Password tab)

### UI Verification Checklist
- [ ] Change Password tab renders
- [ ] All fields present and labeled
- [ ] Show/hide password toggle works
- [ ] Validation errors display
- [ ] Submit button disabled until valid

---

## US-028: Factory Reset

**As a** user decommissioning or reselling device
**I want to** perform a factory reset
**So that** all my data and configuration is removed

### Scenario
1. User opens Settings > System
2. User clicks Factory Reset
3. Multiple confirmation required (prevent accidents)
4. Device wipes all settings
5. Device reboots into first-boot state

### OLED Screens

Confirmation:
```
┌────────────────────┐
│ ⚠ FACTORY RESET   │
│ ═══════════════════│
│ All data will be   │
│ permanently erased │
│                    │
│ Hold BTN 5s to     │
│ confirm            │
└────────────────────┘
```

During reset:
```
┌────────────────────┐
│ FACTORY RESET      │
│                    │
│ Erasing...         │
│ ████████░░ 80%     │
│                    │
│ DO NOT POWER OFF   │
└────────────────────┘
```

### Acceptance Criteria
- [ ] Factory Reset in System section
- [ ] Warning dialog explains consequences
- [ ] Requires typing confirmation phrase
- [ ] Progress shown during erase
- [ ] OLED shows reset progress
- [ ] Device reboots to first-boot state
- [ ] All credentials removed
- [ ] Mesh associations cleared
- [ ] Event history cleared

### UI Pages
- `spa-settings.html` (System section)

---

## US-029: Remove Mesh Node

**As a** user reorganizing their mesh network
**I want to** remove a node from the network
**So that** I can relocate or decommission it

### Scenario
1. User opens Mesh page
2. User selects node to remove
3. User clicks Remove Node
4. Confirmation dialog appears
5. Node removed from coordinator
6. Removed node returns to standalone mode

### OLED Screens (on removed node)

```
┌────────────────────┐
│ MESH REMOVED       │
│                    │
│ Coordinator        │
│ removed this node  │
│                    │
│ Entering standalone│
└────────────────────┘
```

### Acceptance Criteria
- [ ] Remove option on node card
- [ ] Confirmation dialog
- [ ] Node immediately removed from mesh view
- [ ] Coordinator stops routing to node
- [ ] Removed node enters standalone mode
- [ ] OLED on removed node shows status
- [ ] Event logged on both devices
- [ ] Node can rejoin mesh later

### UI Pages
- `spa-mesh.html`

---

## US-030: Review Usage Trends

**As a** user optimizing their setup
**I want to** review long-term sensor trends
**So that** I can identify patterns and adjust thresholds

### Scenario
1. User opens History page
2. User selects longer time range (week/month)
3. Mini chart shows trend visualization
4. User identifies patterns (daily cycles, etc.)
5. User adjusts sensor thresholds based on insights

### Acceptance Criteria
- [ ] Time range selector (24h, 7d, 30d, custom)
- [ ] Mini chart shows event distribution over time
- [ ] Trend data for temperature/humidity sensors
- [ ] Peak/low value indicators
- [ ] Pattern detection hints (e.g., "spikes at 6pm daily")
- [ ] Link to sensor config from trend view
- [ ] Export includes trend data

### UI Pages
- `spa-history.html`

### UI Verification Checklist
- [ ] Time range selector works
- [ ] Chart updates with range changes
- [ ] Stats cards update with range
- [ ] Trend indicators meaningful
- [ ] Export button respects time range

