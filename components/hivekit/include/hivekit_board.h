/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 HiveKit Contributors
 *
 * hivekit_board.h — board-level hardware initialisation helpers
 *
 * These functions are called very early in app_main(), before the Zigbee
 * stack or NVS are initialised.  Each helper is guarded by a Kconfig symbol
 * so that only the relevant hardware is touched on a given board.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configure the RF switch on the Seeed XIAO ESP32-C6 to use the
 *        on-PCB chip antenna.
 *
 * The XIAO ESP32-C6 has a GPIO-driven RF switch:
 *   GPIO3  = RF amplifier enable  (0 = enabled)
 *   GPIO14 = antenna select       (0 = PCB chip antenna, 1 = U.FL external)
 *
 * Both pins must be driven LOW to activate the on-PCB antenna.  The chip
 * leaves them in an undriven state at power-on, which degrades RF performance
 * by approximately 20–30 dB.
 *
 * Guarded by CONFIG_HIVEKIT_BOARD_XIAO_C6_ANTENNA (default y).
 * Has no effect (becomes an empty inline) when the symbol is not set.
 */
#ifdef CONFIG_HIVEKIT_BOARD_XIAO_C6_ANTENNA
void hivekit_board_xiao_c6_init_antenna(void);
#else
static inline void hivekit_board_xiao_c6_init_antenna(void) {}
#endif

#ifdef __cplusplus
}
#endif
