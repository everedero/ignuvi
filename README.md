# Ignuvi

Zephyr support for a reverse-engineered pilot-wire heater gateway.
The board is built around an STM32F4 SoC.

## Project status

* Blinky runs
* Console at 2 Mbaud on PB6 (TX testpoint)
* Console also confirms the 26 MHz HSE main clock
* Pilot wire simulation (simulated zero-cross, not tested on mains yet)
* Bluetooth LE works (the PC pairs with the peripheral sample, bonds survive a reboot)
* BLE settings persist on the SPI flash
* AT25SF641 flash passes the test from `samples/drivers/spi_flash`
* Zigbee chip firmware not working

## Firmware replacement shortcomings

**The board ground is tied to the mains (connector P3, probably the phase).**
The supply is non-isolated. Never connect a debugger, USB, serial adapter or scope ground
while the board is plugged into the mains.

This firmware replacement require you to erase the stock firmware, with no possible recovery.

You will need:
* A probe for reflashing the STM32 (ST-Link, FTDI2232, BlackMagicProbe...)
* A soldering iron and wires
* A (safe) 5V or 3V3 supply

## Hardware description

### Main component list
| Part | Function |
| --- | --- |
| STM32F412RG (LQFP64) | Main MCU |
| AP6212 (BCM4343A0, not the AP6212A) | Wi-Fi + Bluetooth 4.1 SiP |
| ATSAMR21E18A | 2.4 GHz 802.15.4 radio MCU |
| AT25SF641 | 64 Mbit SPI NOR flash |
| Apple MFi337S3959 (marked "629 3959 1638", 8-pin) | MFi authentication coprocessor 2.0C, I²C1 at 0x10. HomeKit / WAC accessory identity (see MFi coprocessor) |
| VIPER013LS | Non-isolated offline converter (half-wave rectified mains) |
| NCP1117 | 3.3 V LDO |
| MOC3063M | Zero-cross optotriac (triac driver) |
| T435-600B | 4 A triac, pilot-wire output |
| Crystal "NSK B 26TAG" | STM32 HSE, 26 MHz |
| Crystal "NSK B 16TAH" | ATSAMR21 clock, assumed 16 MHz |
| Crystal "NSK J 26TAG" | AP6212 clock, assumed 26 MHz |
| 32.768 kHz crystal | STM32 LSE (PC14/PC15) |

See the reconstructed schematics in the Kicad folder.

### Mains connector

| Pin | Role |
| --- | --- |
| P1 | Neutral (inferred) |
| P2 | Pilot wire output |
| P3 | Phase (inferred) = board GND |

### Bottom test pads
<!--
o  TP2

o  TP1

o  TP3



  | o (1)           USB
  |
  | o (2)          +5.5V | o
P |                      |
R | o (BOOT0)     USB_DM | o
O |                      |
G |               USB_DP | o
P |                      |
R |                   0V | o
O |    | o  SWDIO
C |    |
  |    | o  SWCK    Z    | o (GND)
  |    |            G    |
  |    | o  SWOV    B    | o (SWDIO)
  | o (TX)               |             USART ZBG
  |                 J    | o (SWCK)   ________________
  | o (3V3)         T    |
  |                 A    | o (VDDA)    RX  o   o  TX
  | o (G)           G
-->
![Kroki generated Ditaa](https://kroki.io/ditaa/svg/eNqNUEEOgyAQvO8r9ihp0lSNDxBJSGtbjFJqvPgEP-DjC1SEIE06CWzYZWYYFkTZFQCLqflWS9BAXHHBLCfo8Rqo6btZEcxO1blSpg2d3pNYof_yqBDyQpzizB6WJ37z-GFked0_fkdcVOhnXoTDm10FNFvH5vOjpjWHyXUy_mTEXwnAU2yhzIHubGtF3BfKkcSvjnLWvcSJcsBEllug2txbLTVHcD6lKr2RDH2SwvUurBirLbMf0QQyS45OlpM4vtaDD6w3cik=)


* The BOOT0 testpoint has a 10k resistor to the BOOT0 pin. Since BOOT0 has a 100 kΩ pull-down,
Setting this pad to 3V3 forces BOOT0 to 3V, which is a level high.
* Prog proc 1 is undetermined, probably 5V. Prog proc 2 is undetermined.
* Prog proc (STM32) TP TX is on PB6.
* RX is not provided.
* TX baud rate was 2000000 on the original version, we kept the same value
* ZBG (Atmel) JTAG SWDIO is PA31 and SWCLK Atmel PA30.
* ZBG (Atmel) RX is PA25 and TX is PA24.
* SW0V is GND.
* 5.5V is the silkscreen marking, should be 5V.

## How to flash
### SWD access

While debugging, wire the BOOT0 test pad to the 3V3 pad.

The reason for this is that there is no easy access to nRST, which has a 280 kOhms pull-up,
but no testpoint. The way to reflash the board is to use BOOT0 instead.

This way, every reset then starts the ST ROM bootloader, where SWD always works.

Once the firmware is all done, remove the BOOT0 wire to boot the image standalone.

### Flash probe

Two options implemented:
* Using an FTDI2232 with the "resistor hack" and openocd (default west config)
* Using an STLink probe and stm32cubeprogrammer (add "--runner stm32cubeprogrammer" to
  west flash)

### First time flashing: remove readout protection and flash

The stock firmware ships with RDP level 1, a protection against reading.
Going back to level 0 mass-erases the flash.

It means the stock firmware cannot be read out or backed up first.

1. Bench 3.3 V supply, ST-LINK on GND / SWDIO / SWCLK (+ VDD\_TARGET on 3V3).
2. Wire the BOOT0 pad to 3V3 and power-cycle.
3. Check the protection level (RDP byte = bits 15:8 of `FLASH_OPTCR`; `0x55` = level 1,
   `0xAA` = level 0):
   ```bash
   openocd -f boards/vendor/ignuvi/support/openocd.cfg \
       -c 'init' -c 'halt' -c 'mdw 0x40023C14' -c 'shutdown'
   ```
4. Set RDP level 0 (mass erase), then power-cycle:
   ```bash
   openocd -f boards/vendor/ignuvi/support/openocd.cfg \
       -c 'init' -c 'halt' -c 'stm32f2x unlock 0' -c 'shutdown'
   ```
   `mdw 0x40023C14` should now read `0x0FFFAAED`.
5. Flash (from `zephyr_workspace`, after the build below):
   ```bash
   ZEPHYR_BASE=$PWD/zephyr west flash
   ```

Once this is done, reflashing only takes a "west flash".

### Build and flash

```bash
cd zephyr_workspace
source ~/zephyrproject/.venv/bin/activate
west config manifest.path ignuvi && west update
west build -p -b ignuvi apps/pilot_wire
west flash
```

## Pilot wire over Bluetooth

`apps/pilot_wire` drives the pilot-wire output from BLE.

Home Assistant gets a small custom integration for the heater modes.
(`homeassistant/custom_components/ignuvi_pilot_wire`).

It then shows the board as a climate entity.

### Pairing
* Hold the user middle button for 3 seconds to start pairing.
  The blue LED blinks, and the board advertises as limited discoverable for 180 seconds.
  If there is no device already bonded, for instance on a new boot, the pairing modestarts at boot.

* Press the hidden button with a pin to delete all bonds. All peers will be disconnected.
  The red LED lights for 1 second, then pairing mode starts.

Do not forget to make the host forget the pairing too, before pairing again.
For instance, run `bluetoothctl remove <addr>`.

## Home Assistant integration

Copy `custom_components/ignuvi_pilot_wire/` to `/config/custom_components/` on
Home Assistant machine, then restart it.

Put the board in pairing mode (middle button for 3 seconds). The blue LED blinks.
The module will appear on Home Assistant.


### Thermostat feature
However, if what you want is to create a software thermostat with an external thermal sensor
(which is a good idea since internal heaters thermal sensing can be very off...), you will
need to add it to your Home Assistant configuration.yaml, at the root of the Home Assistant
installation folder.

Add this (replace the names for your module and your thermal sensor):
```bash
template:
  - switch:
      - name: Ignuvi heater
        unique_id: ignuvi_1f_ac_heater
        state: "{{ is_state('climate.ignuvi_1f_ac', 'heat') and is_state_attr('climate.ignuvi_1f_ac', 'preset_mode', 'comfort') }}"
        turn_on:
          - action: climate.set_preset_mode
            target: { entity_id: climate.ignuvi_1f_ac }
            data: { preset_mode: comfort }
        turn_off:
          - action: climate.turn_off
            target: { entity_id: climate.ignuvi_1f_ac }

climate:
  - platform: generic_thermostat
    name: Ignuvi 1F AC thermostat
    unique_id: ignuvi_1f_ac_thermostat
    heater: switch.ignuvi_1f_ac_heater
    target_sensor: sensor.coffeecaller_73e9_temperature
    target_temp: 20
    cold_tolerance: 0.3
    hot_tolerance: 0.3
    min_cycle_duration: { minutes: 5 }
```

For Pi, for instance, install the SSH server add-on module.

```
ssh root@homeassistant.local 'mkdir -p /config/custom_components'
scp -r ~/homeassistant/custom_components/ignuvi_pilot_wire root@homeassistant.local:/config/custom_components/
```

If you want to create a schedule.
Go to Settings -> Automations -> Create automation -> ⋮ -> Edit in YAML and paste:

```
alias: Ignuvi 1F AC off during the day
triggers:
  - trigger: time
    at: "08:00:00"
    id: "off"
  - trigger: time
    at: "22:00:00"
    id: heat
actions:
  - action: climate.set_hvac_mode
    target:
      entity_id: climate.ignuvi_1f_ac_thermostat
    data:
      hvac_mode: "{{ trigger.id }}"
mode: single
```

### Troubleshooting
* If Hors-gel and Arrêt come out swapped the another installation,
  build with `-DCONFIG_PILOT_WIRE_INVERT_POLARITY=y`.
* If the module does not appear on Home Assistant, try deleting bonds,
  and try power cycling the board.
