"""Pilot wire as a climate entity: Arret = off, the other orders are presets."""

from __future__ import annotations

from homeassistant.components.climate import (
    ClimateEntity,
    ClimateEntityFeature,
    HVACMode,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import UnitOfTemperature
from homeassistant.core import HomeAssistant
from homeassistant.helpers.device_registry import CONNECTION_BLUETOOTH, DeviceInfo
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from . import PilotWire
from .const import ARRET, CONFORT, PRESETS

MODES = {preset: mode for mode, preset in PRESETS.items()}


async def async_setup_entry(
    hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback
) -> None:
    async_add_entities([PilotWireClimate(entry.runtime_data, entry.title)])


class PilotWireClimate(ClimateEntity):
    _attr_has_entity_name = True
    _attr_name = None
    _attr_translation_key = "pilot_wire"
    _attr_hvac_modes = [HVACMode.HEAT, HVACMode.OFF]
    _attr_preset_modes = list(PRESETS.values())
    _attr_supported_features = (
        ClimateEntityFeature.PRESET_MODE
        | ClimateEntityFeature.TURN_ON
        | ClimateEntityFeature.TURN_OFF
    )
    # No sensor on the board: required by the base class, never shown
    _attr_native_temperature_unit = UnitOfTemperature.CELSIUS

    def __init__(self, pw: PilotWire, title: str) -> None:
        self._pw = pw
        self._last_heat = CONFORT
        self._attr_unique_id = pw.address
        self._attr_device_info = DeviceInfo(
            connections={(CONNECTION_BLUETOOTH, pw.address)},
            name=title,
            manufacturer="vendor",
            model="Ignuvi",
        )

    async def async_added_to_hass(self) -> None:
        self.async_on_remove(self._pw.add_listener(self.async_write_ha_state))

    @property
    def available(self) -> bool:
        return self._pw.connected and self._pw.mode is not None

    @property
    def hvac_mode(self) -> HVACMode | None:
        if self._pw.mode is None:
            return None
        return HVACMode.OFF if self._pw.mode == ARRET else HVACMode.HEAT

    @property
    def preset_mode(self) -> str | None:
        return PRESETS.get(self._pw.mode)

    async def async_set_preset_mode(self, preset_mode: str) -> None:
        self._last_heat = MODES[preset_mode]
        await self._pw.set_mode(self._last_heat)

    async def async_set_hvac_mode(self, hvac_mode: HVACMode) -> None:
        if hvac_mode == HVACMode.OFF:
            if self._pw.mode in PRESETS:
                self._last_heat = self._pw.mode
            await self._pw.set_mode(ARRET)
        else:
            await self._pw.set_mode(self._last_heat)

    async def async_turn_on(self) -> None:
        await self.async_set_hvac_mode(HVACMode.HEAT)

    async def async_turn_off(self) -> None:
        await self.async_set_hvac_mode(HVACMode.OFF)
