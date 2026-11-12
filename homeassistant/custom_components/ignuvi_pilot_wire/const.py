"""Constants: must match ignuvi/apps/pilot_wire (enum pw_mode)."""

DOMAIN = "ignuvi_pilot_wire"
SERVICE_UUID = "6f1e0001-5b2c-4d8a-9c3e-2a7b41c0d5e1"
MODE_UUID = "6f1e0002-5b2c-4d8a-9c3e-2a7b41c0d5e1"

CONFORT, ECO, HORS_GEL, ARRET, CONFORT_M1, CONFORT_M2 = range(6)

# Arret is the climate "off" state, the others are presets of "heat"
PRESETS = {
    CONFORT: "comfort",
    ECO: "eco",
    HORS_GEL: "away",
    CONFORT_M1: "comfort_1",
    CONFORT_M2: "comfort_2",
}
