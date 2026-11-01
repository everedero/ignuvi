# SPDX-License-Identifier: Apache-2.0

# Same DTC warnings as samr21_xpro: SPI on sercom, pinmux/gpio overlap
list(APPEND EXTRA_DTC_FLAGS "-Wno-spi_bus_bridge")
list(APPEND EXTRA_DTC_FLAGS "-Wno-unique_unit_address_if_enabled")
