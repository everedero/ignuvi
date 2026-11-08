"""Intuitiv pilot-wire heater over Bluetooth LE."""

from __future__ import annotations

import asyncio
import logging
from collections.abc import Callable

from bleak.backends.device import BLEDevice
from bleak_retry_connector import BleakClientWithServiceCache, establish_connection

from homeassistant.components import bluetooth
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import CONF_ADDRESS, Platform
from homeassistant.core import HomeAssistant, callback
from homeassistant.exceptions import ConfigEntryNotReady

from .const import MODE_UUID

_LOGGER = logging.getLogger(__name__)
PLATFORMS = [Platform.CLIMATE]


class PilotWire:
    """One board: a persistent connection, the mode, notifications."""

    def __init__(self, address: str) -> None:
        self.address = address
        self.mode: int | None = None
        self._client: BleakClientWithServiceCache | None = None
        self._lock = asyncio.Lock()
        self._listeners: list[Callable[[], None]] = []

    @property
    def connected(self) -> bool:
        return self._client is not None and self._client.is_connected

    def add_listener(self, cb: Callable[[], None]) -> Callable[[], None]:
        self._listeners.append(cb)
        return lambda: self._listeners.remove(cb)

    def _update(self) -> None:
        for cb in self._listeners:
            cb()

    def _on_notify(self, _char, data: bytearray) -> None:
        if len(data) == 1:
            self.mode = data[0]
            self._update()

    def _on_disconnect(self, _client) -> None:
        _LOGGER.debug("%s disconnected", self.address)
        self._client = None
        self._update()

    async def connect(self, device: BLEDevice) -> None:
        async with self._lock:
            if self.connected:
                return
            client = await establish_connection(
                BleakClientWithServiceCache,
                device,
                self.address,
                disconnected_callback=self._on_disconnect,
            )
            try:
                self.mode = (await client.read_gatt_char(MODE_UUID))[0]
                # The CCC write needs an encrypted link: bond first (Just Works,
                # no-op once bonded). The board only accepts it in pairing mode.
                try:
                    await client.pair()
                except Exception as err:
                    raise ConnectionError(
                        "not bonded: hold the middle button 3 s, then reload"
                    ) from err
                await client.start_notify(MODE_UUID, self._on_notify)
            except Exception:
                await client.disconnect()
                raise
            self._client = client
        self._update()

    async def set_mode(self, mode: int) -> None:
        if not self.connected:
            raise ConnectionError(f"{self.address} not connected")
        await self._client.write_gatt_char(MODE_UUID, bytes([mode]), response=True)
        self.mode = mode
        self._update()

    async def disconnect(self) -> None:
        if self._client:
            await self._client.disconnect()


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    address = entry.data[CONF_ADDRESS]
    device = bluetooth.async_ble_device_from_address(hass, address, connectable=True)
    if device is None:
        raise ConfigEntryNotReady(f"{address} not seen")

    pw = PilotWire(address)
    try:
        # Unbonded host: the board ignores us, never block HA startup on it
        async with asyncio.timeout(30):
            await pw.connect(device)
    except Exception as err:  # bleak raises many types, TimeoutError too
        raise ConfigEntryNotReady(f"{address}: {err}") from err

    @callback
    def _seen(info: bluetooth.BluetoothServiceInfoBleak, _change) -> None:
        # Advertising restarts after every disconnect: reconnect on it
        if not pw.connected:
            entry.async_create_background_task(
                hass, _reconnect(info.device), f"intuitiv reconnect {address}"
            )

    async def _reconnect(dev: BLEDevice) -> None:
        try:
            await pw.connect(dev)
        except Exception as err:
            _LOGGER.debug("%s reconnect failed: %s", address, err)

    entry.async_on_unload(
        bluetooth.async_register_callback(
            hass,
            _seen,
            bluetooth.BluetoothCallbackMatcher(address=address, connectable=True),
            bluetooth.BluetoothScanningMode.PASSIVE,
        )
    )
    entry.runtime_data = pw
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    ok = await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
    if ok:
        await entry.runtime_data.disconnect()
    return ok
