# Pilot wire: mains test over Bluetooth

On mains the board GND is the phase. **Never connect USB, the probe, a serial adapter or a
scope ground while the board is on mains.** The only link is the Bluetooth shell
(`overlay-test.conf` + `test.overlay`). The commands are described in the
[README](../../README.md#mains-test-over-bluetooth).

## 1. Before mains (bench, USB connected)

```bash
cd ~/Documents/zephyr_workspace && source ~/zephyrproject/.venv/bin/activate
ZEPHYR_BASE=$PWD/zephyr west build -p -b ignuvi ignuvi/apps/pilot_wire -- \
    -DEXTRA_CONF_FILE=overlay-test.conf -DEXTRA_DTC_OVERLAY_FILE=test.overlay \
    -DPython3_EXECUTABLE=/home/eve/zephyrproject/.venv/bin/python3
west flash
# pair if not bonded yet: hold PB5 for 3 s (blue LED blinks), then
bluetoothctl --timeout 30 scan on
bluetoothctl pair <addr> && bluetoothctl trust <addr>
pip install bleak   # in the venv
```

The bond is stored in the SPI flash settings, so reflashing keeps it.

Then **unplug USB and the probe**, and stop the Home Assistant integration: the board takes one
connection at a time.

## 2. On mains

```bash
bluetoothctl info <addr>          # Paired: yes, Bonded: yes
python3 ignuvi/apps/pilot_wire/sim/nus.py <addr>
```

Wait about 10 s between a `mode` and its `status`, so the window holds enough half-waves. The
first `mode` or `invert` may answer "link not encrypted": encryption is then requested, so send
it again.

| Command | Expected |
| --- | --- |
| `pw status` | `zero-cross ok`, about 50.0 Hz, 0 glitches, half-wave about 9000..11000 us, PC7 high close to 50 %. `LOST`: unplug from mains and stop here |
| `pw mode 0`, then `pw status` | Confort: `+0 -0`. TP1 is several hundred mV in one PC7 level and about 0 in the other. The verdict line says `want invert 0` or `want invert 1` |
| `pw mode 3`, then `pw status` | Arrêt: `+N -0`, N about 500 in 10 s. TP3 about 0 with the LED on means the triac conducts. The heater stops |
| `pw mode 2`, then `pw status` | Hors-gel: `+0 -N`. The heater holds about 7 °C |
| `pw mode 1`, then `pw status` | Éco: `+N -N` |
| `pw invert 1` (only if the verdict asks) | then repeat modes 3 and 2, and check the heater again |
| `pw mode <final>` | the mode to leave it in. The mode and invert are saved |

Ctrl-D quits `nus.py`. The `zero-cross present` and 10 s `mode N, half-waves on` log lines also
show up in the terminal.

If the heater and the verdict disagree, trust the heater. Phase and neutral may be swapped at the
wall, and the ADC can't see that.
