/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 HiveKit Contributors
 *
 * hivekit_board.c — board-level hardware initialisation
 *
 * Implements board-specific init helpers declared in hivekit_board.h.
 * Each implementation is compiled only when its Kconfig guard is set.
 *
 * Reference: florianL21/zigbee-co2-sensor configure_internal_antenna()
 * Pattern: use gpio_config_t (modern ESP-IDF GPIO API, not legacy
 *          gpio_pad_select_gpio / gpio_set_direction).
 */

#include "hivekit_board.h"

#ifdef CONFIG_HIVEKIT_BOARD_XIAO_C6_ANTENNA

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "hivekit_board";

/* GPIO3:  RF amplifier enable — drive LOW to enable the amp */
#define XIAO_C6_GPIO_RF_AMP_EN  GPIO_NUM_3
/* GPIO14: Antenna select — drive LOW for on-PCB chip antenna */
#define XIAO_C6_GPIO_ANT_SEL    GPIO_NUM_14

void hivekit_board_xiao_c6_init_antenna(void)
{
    /* Configure both pins as outputs, pull-up/pull-down disabled,
     * interrupts disabled.  This is the modern gpio_config_t approach;
     * it replaces the legacy gpio_pad_select_gpio + gpio_set_direction pair. */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << XIAO_C6_GPIO_RF_AMP_EN) |
                        (1ULL << XIAO_C6_GPIO_ANT_SEL),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    /* Drive both LOW:
     *   GPIO3=0  → RF amplifier enabled
     *   GPIO14=0 → on-PCB chip antenna selected */
    gpio_set_level(XIAO_C6_GPIO_RF_AMP_EN, 0);
    gpio_set_level(XIAO_C6_GPIO_ANT_SEL,   0);

    ESP_LOGI(TAG, "internal antenna selected (GPIO3=0, GPIO14=0)");
}

#endif /* CONFIG_HIVEKIT_BOARD_XIAO_C6_ANTENNA */
