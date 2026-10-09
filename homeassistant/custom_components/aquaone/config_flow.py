"""Manual host and port setup for a verified Luma device."""

from typing import Any

import voluptuous as vol

from homeassistant import config_entries
from homeassistant.const import CONF_HOST, CONF_PORT
from homeassistant.helpers.aiohttp_client import async_get_clientsession

from .api.client import AquaOneClient
from .api.errors import (
    AquaOneAuthenticationError,
    AquaOneConnectionError,
    AquaOneDeviceRejectedError,
    AquaOneInvalidResponseError,
    AquaOneTimeoutError,
    AquaOneUnsupportedProtocolError,
)
from .const import CONF_DEVICE_ID, CONF_DEVICE_TYPE, DEFAULT_PORT, DOMAIN
from .products.luma import AquaOneUnsupportedProductError, LumaAdapter


class AquaOneConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    """Configure one physical aquaOne Luma."""

    async def async_step_user(self, user_input: dict[str, Any] | None = None):
        errors: dict[str, str] = {}
        if user_input is not None:
            try:
                client = AquaOneClient(
                    async_get_clientsession(self.hass),
                    user_input[CONF_HOST],
                    user_input[CONF_PORT],
                )
                snapshot = await LumaAdapter(client).async_get_snapshot()
            except (AquaOneConnectionError, AquaOneTimeoutError) as exc:
                errors["base"] = "cannot_connect"
            except AquaOneUnsupportedProtocolError as exc:
                errors["base"] = "unsupported_protocol"
            except AquaOneUnsupportedProductError as exc:
                errors["base"] = "unsupported_product"
            except (AquaOneAuthenticationError, AquaOneDeviceRejectedError,
                    AquaOneInvalidResponseError, ValueError) as exc:
                errors["base"] = "invalid_device"
            except Exception:  # HA flow boundary: show a generic error, never a payload.
                errors["base"] = "unknown"
            else:
                system = snapshot.system
                await self.async_set_unique_id(system.device_key)
                self._abort_if_unique_id_configured()
                return self.async_create_entry(
                    title=system.device_name.strip() or "Luma",
                    data={
                        CONF_HOST: user_input[CONF_HOST],
                        CONF_PORT: user_input[CONF_PORT],
                        CONF_DEVICE_TYPE: system.device_type,
                        CONF_DEVICE_ID: system.device_id,
                    },
                )
        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema({
                vol.Required(CONF_HOST): str,
                vol.Required(CONF_PORT, default=DEFAULT_PORT): vol.All(
                    vol.Coerce(int), vol.Range(min=1, max=65535)
                ),
            }),
            errors=errors,
        )
