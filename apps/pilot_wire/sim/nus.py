#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Terminal for the pilot_wire shell over BLE (overlay-test.conf): nus.py <address>

Pair once with bluetoothctl during the pairing window. The board is then not
discoverable, but BlueZ still connects to the bonded address. Type shell
commands (pw status, pw mode 2, pw invert 1); Ctrl-D quits."""

import asyncio
import sys

from bleak import BleakClient

RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # host -> board
TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # board -> host, notify


async def main(address):
    loop = asyncio.get_running_loop()
    async with BleakClient(address, timeout=30) as client:
        print(f"connected to {address}, MTU {client.mtu_size}", file=sys.stderr)
        await client.start_notify(TX, lambda _, data: print(data.decode(errors="replace"),
                                                             end="", flush=True))
        chunk = max(client.mtu_size - 3, 20)
        while client.is_connected:
            line = await loop.run_in_executor(None, sys.stdin.readline)
            if not line:
                break
            data = line.rstrip("\n").encode() + b"\r"
            for i in range(0, len(data), chunk):
                await client.write_gatt_char(RX, data[i:i + chunk], response=False)
        await asyncio.sleep(0.5)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    asyncio.run(main(sys.argv[1]))
