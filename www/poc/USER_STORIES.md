# Boorker Watchdog User Stories

This document catalogs user stories from unboxing through a year of use. Each story follows the format:
- **As a** [user type]
- **I want to** [action]
- **So that** [benefit]

Stories are grouped by lifecycle phase and reference which UI pages support them.

## Story Index

| ID | Phase | Story | Primary UI | OLED |
|----|-------|-------|-----------|------|
| US-001 | Unboxing | Power on and see status | - | Yes |
| US-002 | Unboxing | Connect to setup AP | spa-login | Yes |
| US-003 | First Boot | Claim device with password | spa-login | - |
| US-004 | First Boot | Complete setup wizard | spa-wizard | Yes |
| US-005 | First Boot | Configure WiFi | spa-wizard | Yes |
| US-006 | First Boot | Name and locate device | spa-wizard | - |
| US-007 | First Boot | Review detected sensors | spa-wizard | Yes |
| US-008 | First Boot | Set alert preferences | spa-wizard | - |
| US-009 | Daily | Glance at system status | spa-dashboard | Yes |
| US-010 | Daily | View sensor readings | spa-dashboard, spa-sensors | Yes |
| US-011 | Daily | Receive alert notification | spa-dashboard | Yes |
| US-012 | Daily | Acknowledge alert | spa-dashboard | Yes |
| US-013 | Daily | Check event history | spa-history | - |
| US-014 | Config | Add new sensor | spa-sensors | - |
| US-015 | Config | Configure sensor thresholds | spa-sensors | - |
| US-016 | Config | Add mesh node | spa-mesh | Yes |
| US-017 | Config | Configure LoRa parameters | spa-settings | - |
| US-018 | Config | Set up Bluetooth pairing | spa-settings | Yes |
| US-019 | Config | Configure AP mode | spa-settings | - |
| US-020 | Maint | Update firmware (OTA) | spa-settings | Yes |
| US-021 | Maint | Reboot device | spa-settings | Yes |
| US-022 | Maint | View system diagnostics | spa-terminal | - |
| US-023 | Maint | Export event history | spa-history | - |
| US-024 | Trouble | Diagnose offline node | spa-mesh, spa-terminal | - |
| US-025 | Trouble | Reconnect WiFi | spa-settings | Yes |
| US-026 | Trouble | Debug sensor issue | spa-terminal | - |
| US-027 | Long-term | Change password | spa-login | - |
| US-028 | Long-term | Factory reset | spa-settings | Yes |
| US-029 | Long-term | Remove mesh node | spa-mesh | - |
| US-030 | Long-term | Review usage trends | spa-history | - |

---

## Detailed Stories

See individual story files:
- [01-unboxing.md](01-unboxing.md) - US-001 to US-002
- [02-first-boot.md](02-first-boot.md) - US-003 to US-008
- [03-daily-use.md](03-daily-use.md) - US-009 to US-013
- [04-configuration.md](04-configuration.md) - US-014 to US-019
- [05-maintenance.md](05-maintenance.md) - US-020 to US-023
- [06-troubleshooting.md](06-troubleshooting.md) - US-024 to US-026
- [07-long-term.md](07-long-term.md) - US-027 to US-030
