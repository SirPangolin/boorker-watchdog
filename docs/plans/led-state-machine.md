# LED Feedback State Machine

## States (Priority Order)

| Priority | State | RGB Color | Pattern | Meaning |
|----------|-------|-----------|---------|---------|
| 0 (highest) | ALERT_CRITICAL | Red | Fast double pulse | Critical system alert |
| 1 | FIRST_BOOT | Purple | Slow breathe | Password not changed (unclaimed) |
| 2 | WIFI_PROVISIONING | Blue | Slow blink | AP mode, awaiting WiFi config |
| 3 | WIFI_RECONNECTING | Yellow | Medium blink | Lost connection, reconnecting |
| 4 | ALERT_ACTIVE | Orange | Slow double pulse | Active alert condition |
| 5 | WIFI_CONNECTING | Cyan | Fast blink | Connecting to network |
| 6 | TAILSCALE_CONNECTING | Cyan | Fast blink | Connecting to Tailscale |
| 7 | CONNECTED | Green | Solid on | Normal operation |
| 8 (lowest) | OFF | Off | Off | LED disabled |

## Implementation: Bitmask Tracking (Approach B)

**Design:** Track multiple active states simultaneously. LED shows highest priority active state.

```c
uint16_t active_states;  // Bitmask of active states

set_state(X):   active_states |= (1 << X)
clear_state(X): active_states &= ~(1 << X)
get_display():  return lowest set bit (highest priority)
```

**Benefits:**
- Each domain manages only its own states
- Automatic fallback to next priority when state cleared
- Callers don't need to know full state machine

## State Ownership

| Domain | Owns States | Clears FIRST_BOOT? |
|--------|-------------|-------------------|
| web_auth | FIRST_BOOT | Yes (on password change) |
| wifi_manager | WIFI_PROVISIONING, WIFI_CONNECTING, WIFI_RECONNECTING, CONNECTED | No |
| tailscale_manager | TAILSCALE_CONNECTING | No |
| alert_system | ALERT_CRITICAL, ALERT_ACTIVE | No |

## FIRST_BOOT Semantics

**FIRST_BOOT = "Device not claimed by owner"**

- **Set:** On boot if password has never been changed
- **Cleared:** Only when password is changed via web UI
- **NOT cleared by:** WiFi connecting, BLE provisioning, time passing

**Behavior during FIRST_BOOT:**
- LED: Purple breathing (highest priority after alerts)
- Web UI: Forces password change before any other access
- WiFi: Can be in AP mode or connected (independent concern)
- QR/Display: Shows credentials + access URL

## Factory Reset

Factory reset triggers:
1. Clear WiFi credentials → device enters AP/provisioning mode
2. Reset password to default → FIRST_BOOT = true
3. Next boot: Purple LED, forced password change

## Example Flows

### New Device (AP + Web UI provisioning)
```
1. Boot                    → active: [FIRST_BOOT, WIFI_PROVISIONING]
                           → LED: Purple (priority 1 > 2)
2. User connects to AP     → (no state change)
3. User changes password   → active: [WIFI_PROVISIONING]
                           → LED: Blue
4. User configures WiFi    → active: [WIFI_CONNECTING]
                           → LED: Cyan
5. WiFi connects           → active: [CONNECTED]
                           → LED: Green
```

### New Device (BLE provisioning)
```
1. Boot                    → active: [FIRST_BOOT, WIFI_PROVISIONING]
                           → LED: Purple
2. BLE provision (PoP)     → active: [FIRST_BOOT, WIFI_CONNECTING]
                           → LED: Purple (still unclaimed)
3. WiFi connects           → active: [FIRST_BOOT, CONNECTED]
                           → LED: Purple (still unclaimed)
4. User changes password   → active: [CONNECTED]
                           → LED: Green
```

### Existing Device (has creds, password changed)
```
1. Boot                    → active: [WIFI_CONNECTING]
                           → LED: Cyan
2. WiFi connects           → active: [CONNECTED]
                           → LED: Green
```

### WiFi Disconnect During Operation
```
1. Running normally        → active: [CONNECTED]
                           → LED: Green
2. WiFi disconnects        → active: [WIFI_RECONNECTING]
                           → LED: Yellow
3. WiFi reconnects         → active: [CONNECTED]
                           → LED: Green
```

## Implementation Checklist

- [ ] Change `current_state` to `active_states` bitmask
- [ ] Update `set_state()` to set bit
- [ ] Update `clear_state()` to clear bit
- [ ] Add `get_highest_priority()` helper
- [ ] Update `apply_current_state()` to show highest priority
- [ ] web_auth: Clear FIRST_BOOT on password change
- [ ] wifi_manager: Only manage WiFi states (never touch FIRST_BOOT)
- [ ] main.c: Set FIRST_BOOT based on `password_changed` flag, not `is_first_boot`
