"""Self-check without a board. In the HA container:
docker exec homeassistant python3 /config/custom_components/intuitiv_pilot_wire/selftest.py
"""

import asyncio
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(__file__))))

import custom_components.intuitiv_pilot_wire as pkg  # noqa: E402
from custom_components.intuitiv_pilot_wire import config_flow  # noqa: E402, F401
from custom_components.intuitiv_pilot_wire.climate import PilotWireClimate  # noqa: E402
from custom_components.intuitiv_pilot_wire.const import MODE_UUID  # noqa: E402

from homeassistant.components.climate import HVACMode  # noqa: E402


class FakeClient:
    is_connected = True

    def __init__(self, mode):
        self.value = bytes([mode])
        self.writes = []
        self.notify = None
        self.paired = False

    async def read_gatt_char(self, uuid):
        assert uuid == MODE_UUID
        return bytearray(self.value)

    async def pair(self):
        self.paired = True

    async def start_notify(self, uuid, cb):
        assert uuid == MODE_UUID
        assert self.paired, "CCC write before the bond"
        self.notify = cb

    async def write_gatt_char(self, uuid, data, response):
        assert uuid == MODE_UUID and response
        self.writes.append(bytes(data))

    async def disconnect(self):
        self.is_connected = False


async def main():
    client = FakeClient(1)

    async def fake_establish(cls, device, name, disconnected_callback):
        client.on_disc = disconnected_callback
        return client

    pkg.establish_connection = fake_establish
    pw = pkg.PilotWire("43:43:A0:12:1F:AC")
    await pw.connect(object())
    assert pw.connected and pw.mode == 1

    ent = PilotWireClimate(pw, "Intuitiv 1F:AC")
    states = []
    pw.add_listener(lambda: states.append((ent.hvac_mode, ent.preset_mode)))
    assert ent.available and ent.hvac_mode == HVACMode.HEAT and ent.preset_mode == "eco"

    # HA -> board: one byte per order
    for preset, byte in [("comfort", 0), ("away", 2), ("comfort_1", 4), ("comfort_2", 5)]:
        await ent.async_set_preset_mode(preset)
        assert client.writes[-1] == bytes([byte]), (preset, client.writes)
    await ent.async_turn_off()
    assert client.writes[-1] == b"\x03" and ent.hvac_mode == HVACMode.OFF
    assert ent.preset_mode is None
    await ent.async_turn_on()  # back to the last preset
    assert client.writes[-1] == b"\x05" and ent.preset_mode == "comfort_2"

    # board -> HA: notification
    client.notify(None, bytearray([2]))
    assert states[-1] == (HVACMode.HEAT, "away")
    client.notify(None, bytearray([3]))
    assert states[-1] == (HVACMode.OFF, None)
    client.notify(None, bytearray([1, 2]))  # malformed: ignored
    assert pw.mode == 3

    # disconnect -> unavailable, writes refused
    client.on_disc(client)
    assert not ent.available
    try:
        await ent.async_set_preset_mode("eco")
        raise AssertionError("write while disconnected")
    except ConnectionError:
        pass

    print("selftest OK")


asyncio.run(main())
