#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""NUS shell over BLE: refused unencrypted, then status, Arret counts."""

import asyncio
import sys

from bumble.core import UUID
from bumble.device import Device, Peer
from bumble.hci import Address
from bumble.transport import open_transport

RX = UUID("6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
TX = UUID("6E400003-B5A3-F393-E0A9-E50E24DCCA9E")
out = []


async def main():
    async with await open_transport(sys.argv[1]) as (src, sink):
        dev = Device.with_hci("central", Address("F0:F1:F2:F3:F4:F5"), src, sink)
        await dev.power_on()
        found = asyncio.get_running_loop().create_future()
        dev.on("advertisement", lambda a: found.done() or found.set_result(a.address))
        await dev.start_scanning(active=True)
        addr = await asyncio.wait_for(found, 20)
        await dev.stop_scanning()
        conn = await dev.connect(addr)
        peer = Peer(conn)
        await peer.discover_services()
        await peer.discover_characteristics()
        rx = peer.get_characteristics_by_uuid(RX)[0]
        tx = peer.get_characteristics_by_uuid(TX)[0]
        await tx.subscribe(lambda v: out.append(bytes(v).decode(errors="replace")))

        async def cmd(c, wait=1.5):
            out.clear()
            for i in range(0, len(c) + 1, 20):
                await rx.write_value((c + "\r")[i:i + 20].encode(), with_response=False)
            await asyncio.sleep(wait)
            text = "".join(out)
            print(f"===== {c}\n{text}")
            return text
        t = await cmd("pw mode 3")
        assert "not encrypted" in t, "unencrypted mode change must be refused"
        try:
            await conn.pair()
        except Exception as e:
            print("pair:", e)
        await asyncio.sleep(1)
        await cmd("pw status", 1)
        t = await cmd("pw mode 3")
        assert "mode Arret" in t
        t = await cmd("pw status", 0.5)
        assert "invert 0" in t and "+0 -" not in t and " -0 (PC7 high 0," in t, \
            "Arret, default polarity: positive = PC7 low"
        await conn.disconnect()
        print("PASS")


asyncio.run(main())
