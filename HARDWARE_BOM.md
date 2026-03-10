# RP2040 Lickometer Hardware BOM

Source of truth: [DigiKey_BOM.xlsx](./DigiKey_BOM.xlsx)

This file is a readable mirror of the current Digi-Key cart BOM. If there is any mismatch, use the XLSX.

## Digi-Key Cart Items

| Index | Qty | Digi-Key Part Number | Manufacturer Part Number | Description | Available | Backorder | Unit Price (USD) | Extended Price (USD) |
|---:|---:|---|---|---|---:|---:|---:|---:|
| 1 | 1 | `1528-5980-ND` | `5980` | ADAFRUIT FEATHER RP2040 ADALOGGE | 1 | 0 | 14.95 | 14.95 |
| 2 | 1 | `1528-1038-ND` | `1982` | MPR121 TOUCH SENSE 12-KEY | 1 | 0 | 7.95 | 7.95 |
| 3 | 1 | `1528-4650-ND` | `4650` | FEATHERWING OLED 1.3" 128X64 | 1 | 0 | 14.95 | 14.95 |
| 4 | 1 | `1528-1620-ND` | `3028` | DS3231 PRECISION RTC FEATHERWING | 1 | 0 | 13.95 | 13.95 |
| 5 | 1 | `2830-ND` | `2830` | FEATHER STACKING HEADERS FML | 1 | 0 | 1.25 | 1.25 |
| 6 | 1 | `1528-4399-ND` | `4399` | STEMMA QWIIC JST SH CABLE 50MM | 1 | 0 | 0.95 | 0.95 |
| 7 | 1 | `1528-5035-ND` | `5035` | BATTERY LITH-ION 3.7V 10AH | 1 | 0 | 29.95 | 29.95 |
| 8 | 1 | `1528-1294-ND` | `1294` | MEMORY CARD SDHC 8GB CLASS 4 | 0 | 1 | 9.95 | 9.95 |
| 9 | 1 | `314-1190-ND` | `BU-P4969` | TEST LEAD BNC TO WIRE LEADS 7" | 1 | 0 | 12.83 | 12.83 |
| 10 | 1 | `1528-1581-ND` | `2940` | SHORT FEATHER HEADERS KIT - 12-P | 0 | 1 | 1.50 | 1.50 |
| 11 | 1 | `1528-380-ND` | `380` | CR1220 12MM DIAMETER - 3V LITHIU | 1 | 0 | 0.95 | 0.95 |
| 12 | 1 | `1528-1752-ND` | `290` | HOOK-UP SOLID 22AWG 300V BLK 25' | 1 | 0 | 2.95 | 2.95 |
| 13 | 1 | `1528-1128-ND` | `1128` | RF EMI SHIELD TAPE 49.21'X0.236" | 0 | 1 | 5.95 | 5.95 |

**Cart total (from XLSX):** `118.08 USD`

## Firmware-Critical Mappings

From this cart, firmware-critical components are:
- RP2040 Feather Adalogger (`1528-5980-ND`)
- MPR121 (`1528-1038-ND`)
- OLED 128x64 SH1107 FeatherWing (`1528-4650-ND`)
- DS3231 RTC FeatherWing (`1528-1620-ND`)
- microSD card (`1528-1294-ND`)

Other cart lines support wiring, mounting, power, and external signal connection.
