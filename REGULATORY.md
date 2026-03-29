# Regulatory Compliance

Boorker Watchdog uses LoRa radio communication on the ISM (Industrial, Scientific, and Medical) frequency bands. These bands are shared, unlicensed spectrum governed by regional regulations.

## Hardware Certification

The Heltec WiFi LoRa 32 V3 module carries modular type approval:

- **FCC ID:** 2AXHB-HTIT-WB32LA (United States)
- **CE Marked** (European Union)

This firmware operates within the certified parameters of the module. No separate FCC/CE filing is required when using unmodified Heltec V3 hardware.

## Default Behavior

Boorker Watchdog applies **conservative defaults across all regions**:

- **Duty cycle: 10%** — the firmware enforces a 10% airtime limit (360 seconds per hour) regardless of region. This matches the strictest ISM band requirement (ETSI EN 300 220 for EU 868 MHz) and is applied as the universal default.
- **Duty cycle enforcement is blocking** — when the airtime budget is exhausted, transmissions are rejected until the rolling window resets.
- **TX power** is set per-region and cannot exceed the regional regulatory maximum.

## Regional Limits

| Region | Frequency | Max TX Power | Duty Cycle | Dwell Time | Regulation |
|--------|-----------|-------------|-----------|-----------|------------|
| US_915 | 915 MHz | +22 dBm | No federal limit* | None | FCC Part 15.247 |
| EU_868 | 869.525 MHz | +14 dBm ERP | 10% | None | ETSI EN 300 220 |
| EU_433 | 433.875 MHz | +10 dBm ERP | 10% | None | ETSI EN 300 220 |
| AU_915 | 915 MHz | +22 dBm | No federal limit* | 400ms max | AS/NZS 4268 |

*US and AU regions have no duty cycle requirement, but this firmware defaults to 10% for all regions. This default can be adjusted via build configuration up to the region's legal maximum.

## Antenna Considerations

Regulatory limits are expressed as **Effective Radiated Power (ERP)** or **EIRP**, which includes antenna gain:

```
EIRP = TX Power (dBm) + Antenna Gain (dBi)
```

| Antenna | Gain | EIRP at +22 dBm TX | Within US limit (+30 dBm)? |
|---------|------|--------------------|-----------------------------|
| 2 dBi stub (indoor) | 2 dBi | +24 dBm | Yes |
| 6 dBi whip | 6 dBi | +28 dBm | Yes |
| 10 dBi omni (outdoor) | 10 dBi | +32 dBm | Marginal — reduce TX power to +20 dBm |
| 12+ dBi directional | 12+ dBi | +34+ dBm | No — reduce TX power accordingly |

For EU regions, the ERP limit of +14 dBm means a 2 dBi antenna with +12 dBm TX power is the practical maximum without additional authorization.

## User Responsibility

This software configures chip TX power only. Antenna selection and installation are the operator's responsibility. Users must ensure their complete system (transmitter + antenna + cable losses) complies with local regulations.

Only the US_915 region has been tested on hardware. Other regions are implemented from published regulatory specifications. Use untested regions at your own risk and verify compliance with your local authority.
