# ATSAMR21E18A bring-up

Status (2026-11-01): **the SAMR21 never answers on SWD.** The software (board `ignuvi_radio`,
app `apps/radio_scan`) builds but has never run. The stock firmware does not use the chip either.
The chip is likely dead or not a real SAMR21. Pin map: see the README.

## Probe: FT2232 with the resistor hack

OpenOCD 0.12.0 config: `boards/vendor/ignuvi_radio/support/openocd.cfg`
(`interface/ftdi/swd-resistor-hack.cfg`, target `at91samdXX.cfg`, 400 kHz, `reset_config none`).

| FT2232 channel A | SAMR21 |
| --- | --- |
| ADBUS0 (TCK) | SWCLK, PA30, TP3 |
| ADBUS1 (TDI) | 470 Ω → SWDIO, PA31, TP2 |
| ADBUS2 (TDO) | SWDIO, direct |
| GND | debug header pad 1 |
| VIO | DUT 3V3 (the FT2232 I/Os are unpowered otherwise) |

RESETn is driven by STM32 PB0, not by the probe. The `ignuvi` DTS hogs PB0 high. BOOT0 is
strapped high, so after a power cycle the STM32 sits in its ROM bootloader with PB0 floating
(the SAMR21 internal pull-up then keeps it out of reset). To drive PB0 by hand, halt the STM32
over the ST-LINK and write GPIOB:

```
mmw 0x40023830 0x2 0          ;# RCC_AHB1ENR: GPIOB clock
mww 0x40020418 0x00010000     ;# BSRR: PB0 low  (0x00000001 = high)
mmw 0x40020400 0x1 0x3        ;# MODER: PB0 output
```

Channel B (BDBUS0 TX / BDBUS1 RX, `/dev/ttyUSB1`) is meant for the SAMR21 console on PA24/PA25.
OpenOCD detaches `ftdi_sio` from channel A, so `/dev/ttyUSB0` only comes back after a replug.

## What was checked

Verified good:
- FT2232 I/O (TDO follows TDI), VIO on the DUT 3V3, probe pinout continuity
- VDDANA 3.3 V, VDDCORE 1.2 V (the internal regulator runs)
- RESETn follows PB0 (0 V when driven low)
- SWCLK and SWDIO reach QFN32 pins 31/32 (both 0 V at the chip when driven low)

Every attempt read `ACK = 111`, DPIDR `0xffffffff`: the target never drives SWDIO.
- OpenOCD at 400, 100 and 20 kHz (`Error connecting DP: cannot read IDR`)
- TDI tristated during reads (`SWDIO_OE`), so the 470 Ω cannot mask the target
- Reset extension: SWCLK held low while RESETn is released
- Attach with RESETn held low
- Bit-banged SWD (line reset, JTAG-to-SWD, DPIDR read) with SWCLK/SWDIO as wired and swapped,
  RESETn low and high
- Cold plug: power-on with SWCLK and SWDIO held low

A read-protected SAMD/SAMR still answers at the DP level, so this is not a lock.

## Next if anyone picks it up

1. Scope SWDIO during an attach: dips during the ACK phase mean a weak or damaged output, a flat
   line means a dead chip.
2. Or replace the chip with a known-good ATSAMR21E18A.
3. Once SWD answers: read DSU DID (`mdw 0x41002018`, expect the 0x1001001C family) and
   STATUSB (`mdb 0x41002101`). **Ask before `at91samd chip-erase` if it is locked.** Dump the
   flash, then flash hello_world, then `radio_scan` (`ieee802154 scan active all 300`).
4. Check the doubtful wiring (PA06–09, PA18) by toggling STM32 pins and reading PORT IN
   (`mdw 0x41004420`), and identify the "6114 0600" part.
