# Regulatory Compliance

Boorker Watchdog uses LoRa radio communication on the ISM (Industrial, Scientific, and Medical) frequency bands. These bands are shared, unlicensed spectrum governed by regional regulations.

## Why This Matters

ISM bands are a shared resource. Every device that transmits — your weather station, your neighbor's garage door opener, the farmer's irrigation sensor five miles away — shares the same sliver of spectrum. When one device monopolizes airtime, everyone else's packets get lost.

The regulations exist because cooperation is the only thing that makes shared spectrum work. In the EU, ETSI EN 300 220 mandates a 10% duty cycle on the 868 MHz band — you can transmit for at most 6 minutes per hour. In the US, the FCC takes a lighter touch at 915 MHz, trusting operators to self-regulate. Both approaches aim for the same goal: make sure the spectrum stays usable for everyone.

## Our Approach

Boorker Watchdog defaults to the **strictest standard across all supported regions**, regardless of which region you're in. A US device enforces the same 10% duty cycle as a EU device. We believe the conservative default is the right default — not because US law requires it, but because shared spectrum deserves shared respect.

If a US operator has a legitimate need for higher duty cycles (high-frequency sensor polling, real-time monitoring), the limit can be adjusted at build time. The firmware caps the override at the region's legal maximum — you cannot exceed your jurisdiction's laws even if you try.

This is a deliberate design choice. We'd rather a developer spend 30 seconds changing a build setting than have a device silently interfere with its neighbors because the defaults were permissive.

## Hardware Certification

The Heltec WiFi LoRa 32 V3 module carries modular type approval:

- **FCC ID:** 2AXHB-HTIT-WB32LA (United States)
- **CE Marked** (European Union)

This firmware operates within the certified parameters of the module. No separate FCC/CE filing is required when using unmodified Heltec V3 hardware.

## Default Behavior

- **Duty cycle: 10%** — the firmware enforces a 10% airtime limit (360 seconds per hour) regardless of region. Enforcement is blocking — when the budget is exhausted, transmissions are rejected until the rolling window resets.
- **TX power** is set per-region and cannot exceed the regional regulatory maximum.
- **Dwell time** limits are enforced where applicable (AU_915: 400ms max per transmission).

## Regional Limits

| Region | Frequency | Max TX Power | Duty Cycle | Dwell Time | Regulation |
|--------|-----------|-------------|-----------|-----------|------------|
| US_915 | 915 MHz | +22 dBm | No federal limit* | None | FCC Part 15.247 |
| EU_868 | 869.525 MHz | +14 dBm ERP | 10% | None | ETSI EN 300 220 |
| EU_433 | 433.875 MHz | +10 dBm ERP | 10% | None | ETSI EN 300 220 |
| AU_915 | 915 MHz | +22 dBm | No federal limit* | 400ms max | AS/NZS 4268 |

*This firmware defaults to 10% duty cycle for all regions. The default can be adjusted via build configuration up to the region's legal maximum.

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
