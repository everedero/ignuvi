"""Config flow: Bluetooth discovery on the pilot-wire service UUID."""

from __future__ import annotations

from typing import Any

import voluptuous as vol

from homeassistant.components.bluetooth import (
    BluetoothServiceInfoBleak,
    async_discovered_service_info,
)
from homeassistant.config_entries import ConfigFlow, ConfigFlowResult
from homeassistant.const import CONF_ADDRESS

from .const import DOMAIN, SERVICE_UUID


class IntuitivConfigFlow(ConfigFlow, domain=DOMAIN):
    """Add one Intuitiv board."""

    VERSION = 1

    def __init__(self) -> None:
        self._discovery: BluetoothServiceInfoBleak | None = None

    async def async_step_bluetooth(
        self, discovery_info: BluetoothServiceInfoBleak
    ) -> ConfigFlowResult:
        await self.async_set_unique_id(discovery_info.address)
        self._abort_if_unique_id_configured()
        self._discovery = discovery_info
        self.context["title_placeholders"] = {"name": self._title()}
        return await self.async_step_confirm()

    async def async_step_confirm(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        if user_input is not None:
            return self._create()
        self._set_confirm_only()
        return self.async_show_form(
            step_id="confirm", description_placeholders={"name": self._title()}
        )

    async def async_step_user(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        found = {
            info.address: info
            for info in async_discovered_service_info(self.hass)
            if SERVICE_UUID in info.service_uuids and info.address not in self._async_current_ids()
        }
        if user_input is not None:
            self._discovery = found[user_input[CONF_ADDRESS]]
            await self.async_set_unique_id(self._discovery.address)
            self._abort_if_unique_id_configured()
            return self._create()
        if not found:
            return self.async_abort(reason="no_devices_found")
        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema(
                {
                    vol.Required(CONF_ADDRESS): vol.In(
                        {a: f"{i.name} ({a})" for a, i in found.items()}
                    )
                }
            ),
        )

    def _title(self) -> str:
        return f"{self._discovery.name or 'Intuitiv'} {self._discovery.address[-5:]}"

    def _create(self) -> ConfigFlowResult:
        # A discovery flow left on its confirm step must not duplicate an entry
        # created meanwhile by another flow
        self._abort_if_unique_id_configured()
        return self.async_create_entry(
            title=self._title(), data={CONF_ADDRESS: self._discovery.address}
        )
