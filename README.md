# Ignuvi

Zephyr support for a pilot-wire heater gateway, a
board built around an STM32F412RG (LQFP64).

## Project status:

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

## Hardware

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

### STM32 pin map

| Pin (LQFP64) | Function | Notes |
| --- | --- | --- |
| PB13 (34) | RGB LED red, active low | `led0`, used by blinky. TIM1_CH1N |
| PB14 (35) | RGB LED green, active low | `led1`. TIM1_CH2N |
| PB15 (36) | RGB LED blue, active low | `led2`. TIM1_CH3N |
| VDDA (13) / VDD (19) | Optotriac LED anode (via 200 Ω) | Fed from 3V3 |
| PC6 (37) | Optotriac LED cathode switch (K38 FN, SOT-23, no test point) | Assumed active high. Hogged low in DTS |
| PC7 (38) | Zero-cross detection, TP2 (EXTI7) | Large-ratio, clipped divider on neutral |
| PC3 (11) | Mains voltage / polarity, TP1, ADC1_IN13 | 3× 300 kΩ / 2 kΩ divider |
| PC0 (8) | Pilot-wire output feedback, TP3, ADC1_IN10 | 3× 16 kΩ / 2 kΩ divider |
| PA4 (20) | SPI flash CS | GPIO chip select |
| PA5 (21) | SPI1 SCK, SPI flash | |
| PA6 (22) | SPI1 MISO, SPI flash | |
| PA7 (23) | SPI1 MOSI, SPI flash | |
| PA11 (44) | USB D− (white) | |
| PA12 (45) | USB D+ (green) | |
| PB5 (57) | Middle user button | `sw0`, active high (switch to 3V3), no external pull: internal pull-down.|
| PC2 (10) | Hidden button (pairing reset) | `sw1`, active high (switch to 3V3), no external pull: internal pull-down.|
| PA15, PB3, PB4 (50, 55, 56) | Hardware version resistors (assumed) | 6 footprints, 3 fitted, exact pins to trace. With internal pulls, PA15 and PB3 read low, PB4 high |
| PB6 (58) | Bottom test pad | USART1_TX: console |
| PB8 (61) | I2C1_SCL → MFi coprocessor pin 7 | 2.1 kΩ pull-up |
| PB9 (62) | I2C1_SDA → MFi coprocessor pin 2 | 2.2 kΩ pull-up |
| PA13 (46) | SWDIO | |
| PA14 (49) | SWCLK | |
| NRST (7) | Reset | 280 kΩ pull-up. No test point, cannot wired to the probe |
| PH0 / PH1 (5, 6) | HSE crystal | |
| PC5 (25) | AP6212 BT_REG_ON (34) | `bt-reg-on-gpios`, active high |
| PA2 (16) | USART2_TX → AP6212 UART_RXD (43) | Bluetooth H4, 115200, hw flow control |
| PA3 (17) | USART2_RX ← AP6212 UART_TXD (42) | |
| PA1 (15) | USART2_RTS → AP6212 UART_CTS_N (44) | |
| PA0 (14) | USART2_CTS ← AP6212 UART_RTS_N (41) | |
| PB10 (29) | AP6212 LPO (24), via 1 kΩ | 32.768 kHz from TIM2_CH3 (96 MHz / 2929, +240 ppm). Bluetooth answers nothing but HCI_Reset without it |
| PC8–PC12, PD2 (39–42, 53, 54) | AP6212 SDIO D0–D3, CLK, CMD | Wi-Fi |
| PB12 (33) | AP6212 WL_REG_ON (12) | `wifi-reg-on-gpios`, active high. |
| — | AP6212 BT/WL host wake | Not routed: Wi-Fi uses the in-band SDIO interrupt |

The optotriac LED anode sits on 3V3: PC6 alone turns it on through the cathode switch.

Software notes to move away:
Flash: settings in first 64 Kbit
SDIO D0-D3: `st,stm32f4-sdio` driver (`drivers/sdhc`). D0–D3 and CMD are pulled up by the AP6212 |
User button: Pilot wire: 3 s press = pairing mode
Hidden button: Pilot wire: press = erase bonds

See the reconstructed schematics in the Kicad folder.


### Mains connector

| Pin | Role |
| --- | --- |
| P1 | Neutral (inferred) |
| P2 | Pilot wire output |
| P3 | Phase (inferred) = board GND |

### Pilot-wire orders

Voltage measured between P2 and neutral. The optotriac fires at the next zero crossing after its
LED turns on, and the triac turns off at the next zero crossing after it turns off. So toggling
the LED once per half-cycle (synchronised on PC7, polarity from PC3) selects half-waves.

| Order | Signal | Firmware |
| --- | --- | --- |
| Confort | 0 V | LED off |
| Éco | Full 230 V | LED on continuously |
| Hors-gel | Negative half-wave | LED on during negative half-waves only |
| Arrêt | Positive half-wave | LED on during positive half-waves only |
| Confort −1 / −2 | 230 V for 3 s / 7 s every 5 min | Timed full-wave pulses |

PC0 lets firmware check what actually came out.

### Clocks

HSE 26 MHz → PLL (M=13, N=96, P=2, Q=4) → 96 MHz SYSCLK, 48 MHz PLLQ.
The crystal frequency is inferred from its marking. If blinky runs at the wrong rate or not at
all, switch the PLL source to `&clk_hsi` (16 MHz) with `div-m = <8>` in
`boards/vendor/ignuvi/ignuvi.dts`.

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
* TX baud rate was 2000000 on the original version, we keep the value
* ZBG (Atmel) JTAG SWDIO is PA31 and SWCLK Atmel PA30.
* ZBG (Atmel) RX is PA25 and TX is PA24.
* SW0V is GND.
* 5.5V is the silkscreen marking, should be 5V.

PB11 also reads low with a pull-up: unknown, not in the version group.

## How to flash
### SWD access

**While debugging, wire the BOOT0 test pad to the 3V3 pad.**

The reason for this is that there is no easy access to nRST, which has a 280 kOhms pull-up,
but no testpoint. The way to reflash the board is to use BOOT0 instead.

This way, every reset then starts the ST ROM bootloader, where SWD always works.

`west flash` (openocd runner, the default) resets into the
bootloader, writes the flash, then starts the image from its vector table (`ignuvi_run_app` in
`support/openocd.cfg`).

Once the firmware is all done, remove the BOOT0 wire to boot the image standalone.

### Remove readout protection and flash (first time)

The stock firmware ships with RDP level 1, a protection against reading.
Going back to level 0 **mass-erases the flash**.
It means the stock firmware cannot be read out or backed up first.

1. Bench 3.3 V supply, ST-LINK on GND / SWDIO / SWCLK (+ VDD_TARGET on 3V3).
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
   Blinky starts and prints `LED state: ON/OFF` at 2 Mbaud on PB6.

Later flashes only need step 5.

### Build and flash

```bash
cd zephyr_workspace
source ~/zephyrproject/.venv/bin/activate   # Zephyr 4.5 needs Python >= 3.12
west config manifest.path ignuvi && west update
# ZEPHYR_BASE must be absolute: a relative one is resolved from the sample dir
ZEPHYR_BASE=$PWD/zephyr west build -p -b ignuvi zephyr/samples/basic/blinky \
    -- -DPython3_EXECUTABLE=$(which python3)
# console over USB instead of the PB6 UART: add -S cdc-acm-console
west flash   # openocd over SWD (default), or --runner stm32cubeprogrammer
```

## Tests

`tests/board` checks the board devices. The `board.devices` scenario covers the RGB LED (set
and read back each colour), the buttons (released, interrupt set up) and the SPI flash (size,
JEDEC id, erase/write/read of the sector at 0x700000, which the factory image leaves erased,
then erased again). `board.bluetooth` adds the AP6212: patchram download, public identity,
advertising start and stop (needs the untracked HCD, see Bluetooth).

```bash
# on the board (bench 3.3 V, never on mains). west flash starts the app after the flash
ZEPHYR_BASE=$PWD/zephyr west twister -T ignuvi/tests/board -p ignuvi --device-testing \
    --device-serial /dev/ttyACM0 --device-serial-baud 2000000 --retry-failed 2 -j 1
# without the board
ZEPHYR_BASE=$PWD/zephyr west twister -T ignuvi/tests/board -p native_sim/native/ignuvi
```

**CI:** `scripts/ci.sh [commit range]` (default `HEAD~1..HEAD`) runs the Zephyr compliance
checks that apply here, then twister on `ignuvi` (build only) and `native_sim/native/ignuvi`
(built and run). It needs the west workspace and its Python env. `HW=1` also runs the tests on the
board, and results go to `../ci-out`. Checkpatch, clang-format and ruff use Zephyr's settings
(`.checkpatch.conf`, `.clang-format`, `.ruff.toml`). The excluded checks, and why, are listed in
the script. Without the untracked HCD, the Bluetooth scenario (tag `hcd`) is skipped.

**Release:** pushing a `v*` tag (`git tag v1.0 && git push origin v1.0`) runs
`.github/workflows/release.yml`. It fetches the HCD, builds `apps/pilot_wire` for `ignuvi`, and
attaches `pilot_wire-<tag>.bin` and `.elf` to a GitHub release.

`native_sim/native/ignuvi` (`boards/native/ignuvi`) extends `native_sim` with the board's
devicetree labels and aliases: LEDs `led0`–`led2` (active low) and buttons `sw0`/`sw1` on the
emulated GPIO, the pilot-wire GPIOs, and `spi_flash` as an 8 MiB flash simulator holding the
settings partition. There the button test presses the buttons through the GPIO emulator, and
the JEDEC test is skipped. Apps with board-only options keep them in `boards/ignuvi.conf`
(AIROC in `apps/pilot_wire`).

## Settings

`storage_partition` (the `zephyr,settings-partition`) is the first 64 KiB of the AT25SF641:
16 NVS sectors of 4 KiB. Any app with `CONFIG_SETTINGS=y` gets the NVS backend by default
(`Kconfig.defconfig`). Test: `zephyr/samples/subsys/settings`, flashed twice, starts the second
run from the values the first one saved.

## Bluetooth

The AP6212 Bluetooth core is a BCM4343A0 (LMP subversion 0x2122), driven by the Zephyr AIROC
H4 driver. It needs the AP6212 (non-A) patchram: the CYW43438 / BCM43430A1 patches (AP6212A,
Raspberry Pi) download fine but hang the controller at launch. The patch is not committed:

```bash
curl -L -o ignuvi/apps/bluetooth/bcm43438a0.hcd \
    https://raw.githubusercontent.com/armbian/firmware/master/ap6212/bcm43438a0.hcd
ZEPHYR_BASE=$PWD/zephyr west build -p -b ignuvi zephyr/samples/bluetooth/peripheral -- \
    -DEXTRA_CONF_FILE=$PWD/ignuvi/apps/bluetooth/ignuvi.conf \
    -DPython3_EXECUTABLE=$(which python3)
west flash
bluetoothctl --timeout 15 scan on   # "Zephyr Peripheral Sample Long"
```

Pairing shows a passkey on the console. Bonds, the identity and the IRK live in the settings
partition, so the PC reconnects to the bonded `43:43:A0:12:1F:AC` after a reboot without
pairing again.

The public address is the Broadcom default `43:43:A0:12:1F:AC`, the same on every unprogrammed
chip; the sample advertises with a random static address. Give each board its own address
before deploying several.

## Pilot wire over Bluetooth

`apps/pilot_wire` drives the pilot-wire output from one custom GATT service. No SIG profile
covers heater modes, so Home Assistant gets a small custom integration
(`homeassistant/custom_components/ignuvi_pilot_wire`) that shows the board as a climate entity.

| UUID | Characteristic | Format | Access |
| --- | --- | --- | --- |
| `6f1e0001-5b2c-4d8a-9c3e-2a7b41c0d5e1` | Service, advertised | | |
| `6f1e0002-5b2c-4d8a-9c3e-2a7b41c0d5e1` | Mode | uint8: 0 Confort, 1 Éco, 2 Hors-gel, 3 Arrêt, 4 Confort −1, 5 Confort −2 | read; write and notify need an encrypted (bonded) link |

The board also has the Device Information Service. The mode is saved in the settings
(`pw/mode`), and Confort is the default. Advertising, as `Ignuvi`, restarts after every
disconnect.

**Pairing** follows the GAP modes. Just Works, which needs no console.
- **Pairing mode:** hold the middle button (PB5) for 3 s. The blue LED blinks, and the board
  advertises as limited discoverable, with the service UUID and its name, for 180 s (GAP's
  T_GAP(lim_adv_timeout)). The mode ends at the first bond or at the timeout. With no bond at
  all, for example on a new board, it starts at boot.
- **Outside pairing mode:** the board is not discoverable (no discoverable flag, no UUID), and
  only bonded peers can connect (filter accept list). Pairing requests are rejected. BlueZ
  still reports a non-discoverable device that it already knows, so a bonded host reconnects.
- **Erase:** press the hidden button (PC2). Every bond is deleted and the peers are
  disconnected. The red LED lights for 1 s, then pairing mode starts. On the host, run
  `bluetoothctl remove <addr>` before pairing again.

One connection at a time: the BCM4343A0 stops accepting connections once it has advertised
during a link (reproduced on the bench), so the board never does. Entering pairing mode
disconnects the current peer to free the slot. If a bonded host (HA) reconnects first, hold
the button again.

**Known issue: pairing mode without advertising.** Seen when moving the board to a new HA
host: the blue LED blinks, but the board keeps its normal advertising (`02 01 04`, no UUID, no
name) or goes silent, so HA reports "No Ignuvi found over Bluetooth". The erase button does
not help. Workaround: power-cycle the heater (board included) for a few seconds, then hold the
button again. Not root-caused: the LED starts blinking before advertising does, so a failing
`bt_le_adv_start()` (only logged as `advertising failed`) and a link that never went down
(`adv_restart()` waits for it) look the same from outside. Next time, capture the PB6 log
(2 Mbaud) during the press.

**Output:** in the middle of each half-wave (5 ms after the PC7 edge), the optotriac LED is set to
"is the next half-wave wanted". If there is no zero-cross for 40 ms (bench), only Éco and the
Confort −1/−2 pulses turn the LED on. The PC7 level → mains polarity mapping is not verified.
By default PC7 high is taken as the positive half-wave; `pw invert 1` (test build, below) swaps
it at runtime and saves it (`pw/inv`).

```bash
# bench, no mains: a 10 ms timer replaces the zero-cross, and the enabled half-waves
# are logged every 10 s (Éco +500 -500, Hors-gel +0 -500, Arrêt +500 -0)
ZEPHYR_BASE=$PWD/zephyr west build -p -b ignuvi ignuvi/apps/pilot_wire -- \
    -DCONFIG_PILOT_WIRE_SIM_ZC=y -DPython3_EXECUTABLE=$(which python3)
west flash
# order logic unit test
ZEPHYR_BASE=$PWD/zephyr west twister -T ignuvi/apps/pilot_wire/tests -p native_sim
```

### Mains test over Bluetooth

On mains the board GND is the phase, so the probe and USART1 must stay unplugged.
`overlay-test.conf` + `test.overlay` put the Zephyr shell and the logs on the Nordic UART
Service, and add the `pw` command:

| Command | |
| --- | --- |
| `pw status` | mode, invert, zero-cross (edges, Hz, glitches, half-wave min..max, PC7 high %), half-waves enabled per PC7 level, then TP1 (PC3) and TP3 (PC0) at mid half-wave per PC7 level and LED state, and a polarity verdict. Resets the counters. |
| `pw mode <0-5>` | as the GATT write: set, save, notify |
| `pw invert <0\|1>` | PC7 high = negative half-wave when 1; saved |

`mode` and `invert` need an encrypted link: the first try asks for encryption, retry once.
The verdict reads TP1: neutral is above phase (TP1 high) in the PC7 level that is the
**negative** half on P2. TP3 about 0 while the LED is on means the triac conducts; TP3 only
means something with the heater connected. P1/P3 swapped at the wall can't be seen by the ADC
(every order then acts as Confort): check it on the heater.

```bash
# 1. bench (3.3 V, no mains): simulated zero-cross, check the shell
ZEPHYR_BASE=$PWD/zephyr west build -p -b ignuvi ignuvi/apps/pilot_wire -- \
    -DEXTRA_CONF_FILE=overlay-test.conf -DEXTRA_DTC_OVERLAY_FILE=test.overlay \
    -DCONFIG_PILOT_WIRE_SIM_ZC=y -DPython3_EXECUTABLE=$(which python3)
west flash
# pair: hold PB5 3 s, then on the PC
bluetoothctl --timeout 30 scan on; bluetoothctl pair <addr>; bluetoothctl trust <addr>
pip install bleak
python3 ignuvi/apps/pilot_wire/sim/nus.py <addr>   # pw status, pw mode 3, pw invert 1, Ctrl-D
# 2. rebuild without -DCONFIG_PILOT_WIRE_SIM_ZC=y, flash, then UNPLUG USB AND THE PROBE
```

3. Stop the Home Assistant integration: one link at a time.
4. On mains with the heater: `pw mode 0`, `pw status` (baseline, wait 10 s first), then each of
   modes 1–5 followed by `pw status` after a few seconds. Arrêt should read `+N -0`,
   Hors-gel `+0 -N`. Follow the verdict line with `pw invert`.
5. Cross-check on the heater: Arrêt stops it, Hors-gel holds about 7 °C, Éco lowers the set
   point. If Arrêt and Hors-gel look swapped although the verdict says ok, the verdict is
   wrong (or P1/P3 are swapped): trust the heater.

**On native_sim**, without the board: `native_sim/native/ignuvi` (below) runs the whole app,
with a [bumble](https://github.com/google/bumble) virtual controller in place of the AP6212.
`sim/run.sh` starts two linked bumble controllers, the app on one (`--bt-dev=127.0.0.1:9000`),
and `sim/central.py` on the other. The central pairs in pairing mode (AD flags 0x05), writes a
mode, then reconnects with the bond through the non-discoverable advertising (flags 0x04).

```bash
python3 -m venv ~/bumble && ~/bumble/bin/pip install bumble
ZEPHYR_BASE=$PWD/zephyr west build -p -b native_sim/native/ignuvi ignuvi/apps/pilot_wire -- \
    -DCONFIG_PILOT_WIRE_SIM_ZC=y -DPython3_EXECUTABLE=$(which python3)
BUMBLE_PY=~/bumble/bin/python ignuvi/apps/pilot_wire/sim/run.sh build/zephyr/zephyr.exe   # PASS
# shell over NUS: build with the two test overlays (above), then
BUMBLE_PY=~/bumble/bin/python ignuvi/apps/pilot_wire/sim/run.sh build/zephyr/zephyr.exe shell_test.py
```

### Home Assistant (docker)

The integration pairs by itself (Just Works) while the board is in pairing mode. On a desktop
host this can fail: the desktop agent rejects the Just Works confirmation (SMP error 0x0c).
Then pair once from the host, in pairing mode, with bluetoothctl's own agent. BlueZ keeps the bond:

```bash
bluetoothctl
  agent NoInputNoOutput
  default-agent
  scan le                     # wait for Ignuvi, then: scan off
  pair 43:43:A0:12:1F:AC
  trust 43:43:A0:12:1F:AC
```

A peer that lost its bond can pair again in pairing mode: the board then replaces the old bond
(`CONFIG_BT_SMP_ALLOW_UNAUTH_OVERWRITE`).

Install the integration. `/home/eve` is ecryptfs, and the docker daemon sees the directory under
the mount, not the one in `~/homeassistant`, so copy through the container:

```bash
cd ignuvi/homeassistant/custom_components
docker exec homeassistant mkdir -p /config/custom_components
tar cf - ignuvi_pilot_wire | docker exec -i homeassistant tar xf - -C /config/custom_components
docker exec homeassistant python3 /config/custom_components/ignuvi_pilot_wire/selftest.py
docker restart homeassistant
```

HA needs BlueZ over D-Bus to use the adapter. The current container has no `/run/dbus` mount.
Recreate it with one (same image, network, restart policy and config):

```bash
docker stop homeassistant && docker rm homeassistant
docker run -d --name homeassistant --restart unless-stopped --network host \
    -e TZ=Europe/Paris --cap-add NET_ADMIN --cap-add NET_RAW \
    -v /home/eve/homeassistant:/config -v /run/dbus:/run/dbus:ro \
    ghcr.io/home-assistant/home-assistant:2026.1
```

HA then discovers "Ignuvi" from its service UUID (Settings → Devices & services). The
climate entity has `heat` / `off` (off = Arrêt), plus the presets Confort, Éco, Hors-gel (`away`),
Confort −1 and Confort −2. It reconnects when the board advertises again.

## MFi coprocessor

The 8-pin chip marked "629 3959 1638" is an Apple MFi authentication coprocessor 2.0C (MFI337S3959, Infineon).
It is the only device on I²C1 (PB8 SCL, PB9 SDA) and answers at **0x10**. Pinout as measured: 1 and 6 GND,
2 SDA, 5 VDD, 7 SCL, 8 NC?, 3 and 4 not measured.

`apps/i2c_scan` scans the bus, then reads the chip. It logs to the console only, since USART1 RX is not wired:

```text
<inf> i2c_scan: ACK at 0x10
<inf> i2c_scan: device version 5, firmware 1, protocol 2.0
<inf> i2c_scan: certificate length 908
<inf> i2c_scan: C:3082038806092a864886f70d010702...
```

Device version 5 means 2.0C. The `C:` lines are the PKCS#7 certificate (registers 0x31 onward, 128-byte pages).
The register map comes from [WACResearch `authchip`](https://github.com/BertoldVdb/WACResearch/tree/master/authchip).
Decode the certificate on the host:

```bash
grep -ao 'C:[0-9a-f]*' scan.log | cut -c3- | tr -d '\n' | xxd -r -p > mfi_cert.der
openssl pkcs7 -inform DER -in mfi_cert.der -print_certs -noout -text
```

The certificate is issued by "Apple iPod Accessories Certification Authority" to
`CN=IPA_2222AA180718AA06AA2413AA306240`, an RSA key of 1024 bits, valid from 2018-07-18 to 2022-02-14.
The chip is the HomeKit / WAC accessory identity: a firmware that wants to stay HomeKit-certified needs it.
Its challenge signing (registers 0x10–0x21) is not used yet.

## Wi-Fi

The Wi-Fi core (BCM43430A0) sits on the STM32 SDIO peripheral. Zephyr's STM32 SDHC driver needs
the SDMMC IDMA, which the F4 lacks, so this module has its own `st,stm32f4-sdio` SDHC driver
(`drivers/sdhc`): polled FIFO, 4-bit, in-band card interrupt, clock capped by `max-bus-freq`
(12 MHz). The Zephyr AIROC (WHD) driver sits on top. WL_REG_ON is PB12.

The firmware and NVRAM are not committed. WHD's hal_infineon blobs are for 43438A1, which this
chip is not. WHD needs a firmware that runs the WPA handshake itself (the `idsup` feature), and
the public A0 images lack it: the Linux FMAC build (7.13.53.9) and the Android DHD build
(`fw_bcm43438a0.bin`, 7.10.226.49) leave it to a host wpa_supplicant, so a WPA2 join fails with
`whd_wifi_set_passphrase ... checkres = 33556434` (`WLC_SET_WSEC_PMK` → bad argument). They only
join open networks.

The board's own SPI flash holds a WICED A0 firmware with `idsup-idauth`, 7.45.96.88, so get it
from there. `apps/spi_dump` streams the whole flash over the console (frames of 4 KiB blocks
with a CRC32). `dump.py` collects them into `out/spi_flash.bin`, which is untracked and must
stay out of git. The firmware sits in a resource table at 0x130000, with a second copy at
0x4a0000: a u32 total length, then (offset, size) u32 pairs counted from byte 4 for the
firmware, an NVRAM and an encrypted blob.

```bash
# once per checkout: WHD patch (hal_infineon), see zephyr/patches.yml
west patch -sm ignuvi apply
ZEPHYR_BASE=$PWD/zephyr west build -p -b ignuvi ignuvi/apps/spi_dump -- \
    -DPython3_EXECUTABLE=$(which python3)
west flash
cd ignuvi/apps/spi_dump && python dump.py /dev/ttyACM0 out/spi_flash.bin &  # ~70 s, needs pyserial
# start the app (BOOT0 is strapped high, see Flashing below), then once dump.py is done:
dd if=out/spi_flash.bin of=../wifi/fw/43430a0-7.45.96.88.bin \
    iflag=skip_bytes,count_bytes skip=$((0x13001c)) count=434400
cd -
curl -L -o ignuvi/apps/wifi/fw/nvram_ap6212.txt \
    https://raw.githubusercontent.com/armbian/firmware/master/ap6212/nvram.txt
ZEPHYR_BASE=$PWD/zephyr west build -p -b ignuvi ignuvi/apps/wifi -- \
    -DCONFIG_IGNUVI_WIFI_SSID='"myssid"' -DCONFIG_IGNUVI_WIFI_PSK='"mypsk"' \
    -DPython3_EXECUTABLE=$(which python3)
west flash
```

The app scans and logs the access points. If an SSID is set, it then connects, starts DHCP and
pings 8.8.8.8 four times.
WHD allocates with `k_malloc`, so this app has a 48 KiB heap (`CONFIG_HEAP_MEM_POOL_SIZE`). That
heap is for the vendor driver only.

On the bench, the 7.45.96.88 firmware loads with its built-in CLM (so the CLM blob stays
empty) and the MAC is read from the chip. The scan lists the nearby networks, and a WPA2-PSK
join plus DHCP work. Set `CONFIG_IGNUVI_WIFI_FW="fw/brcmfmac43430a0-sdio.bin"` to use the Linux
image instead (open networks only).

The patch in `zephyr/patches/hal_infineon` makes WHD accept firmware without the `mfp` iovar
(the 7.13 image has none). Without it, the join stops earlier in `whd_wifi_prepare_join`.

**Flashing:** `west flash` does not verify, and on this board the ST-LINK has silently
corrupted an image once (one extra word, the rest shifted), so the board crashed at boot.
Verify after a flash:

```bash
openocd -s ignuvi/boards/vendor/ignuvi/support -f ignuvi/boards/vendor/ignuvi/support/openocd.cfg \
    -c init -c "reset halt" -c "verify_image build/zephyr/zephyr.hex" -c ignuvi_run_app
```

## Home Assistant integration

Copy `custom_components/ignuvi_pilot_wire/` to `/config/custom_components/` on
Home Assistant machine, then restart it

Hold the board's middle button 3 s (blue LED blinks): the module will appear on Home
Assistant. Confirm it while the LED blinks, the integration pairs then. If setup fails with
"not bonded", hold the button again and reload the entry. If the module does not appear, see
*Known issue: pairing mode without advertising* above.

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
