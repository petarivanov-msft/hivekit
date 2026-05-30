/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 HiveKit Contributors
 *
 * sensor_pir.c — PIR motion sensor driver (GPIO interrupt, ZCL Occupancy cluster)
 *
 * ⚠️  UNTESTED — community verification welcome.
 *     Flash and report results: https://github.com/petarivanov-msft/hivekit/issues
 *
 * ═══════════════════════════════════════════════════════════════
 *  PIR MODULE OVERVIEW
 * ═══════════════════════════════════════════════════════════════
 * PIR (Passive Infrared) modules sense changes in IR radiation from moving bodies.
 * They output a digital signal:
 *   - AM312:    3.3V supply, 3.3V logic OUT, ~2 s hold time (fixed), ultra-low
 *               quiescent current (~6 µA). Best choice for direct ESP32-C6.
 *               Source: AM312 datasheet
 *               URL: https://www.datasheet.live/am312-pir-motion-detector
 *   - HC-SR501: 5–12 V supply required; OUT is 3.3 V with most silicon variants.
 *               Hold-time (0.5–300 s) and sensitivity adjustable via trimmer pots.
 *               Source: HC-SR501 datasheet
 *               URL: https://www.epitran.it/ebayDrive/datasheet/44.pdf
 *
 * POWER NOTE (no deep sleep):
 *   A PIR sensor must be continuously powered to detect motion. Deep sleep
 *   disables GPIO monitoring and would cause missed events. This firmware does
 *   NOT enable deep sleep. See sdkconfig.defaults.
 *
 * ═══════════════════════════════════════════════════════════════
 *  GPIO INTERRUPT PATTERN (ESP-IDF v5.x)
 * ═══════════════════════════════════════════════════════════════
 * Rule: ISR handlers MUST be placed in IRAM (IRAM_ATTR).
 *       They MUST NOT: allocate heap, call non-IRAM functions, log, block.
 *       They SHOULD: post a notification/item to a FreeRTOS queue.
 *
 * Pattern used here:
 *   1. gpio_install_isr_service(0) — installs the shared ISR dispatcher.
 *      Returns ESP_ERR_INVALID_STATE if already installed (treated as OK).
 *      Source: ESP-IDF GPIO API reference
 *      URL: https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/gpio.html
 *   2. gpio_isr_handler_add(pin, handler, arg) — registers our per-pin ISR.
 *   3. Consumer task calls xQueueReceive(), debounces, and calls hivekit_report_pir().
 *   4. GPIO edge: GPIO_INTR_ANYEDGE — catches both motion-on and motion-off transitions.
 *
 * ═══════════════════════════════════════════════════════════════
 *  ZCL OCCUPANCY SENSING CLUSTER (0x0406)
 * ═══════════════════════════════════════════════════════════════
 * Attributes used:
 *   0x0000  Occupancy          bitmap8   bit 0: 1=occupied, 0=unoccupied
 *   0x0001  OccupancySensorType enum8    0=PIR (source: ZCL spec §4.8.2.1)
 *
 * Reporting: event-driven (change-triggered) with a periodic keepalive.
 *   min-interval: 0s (report immediately on change)
 *   max-interval: CONFIG_HIVEKIT_PIR_KEEPALIVE_INTERVAL_S (default 300s)
 *
 * Source: ZCL specification §4.8 "Occupancy Sensing Cluster"
 * (ZigBee Cluster Library rev 7, cluster 0x0406)
 *
 * ═══════════════════════════════════════════════════════════════
 *  DEBOUNCE
 * ═══════════════════════════════════════════════════════════════
 * PIR modules can "chatter" around the detection threshold, producing
 * multiple edges within milliseconds. Debounce is implemented in the
 * consumer task (NOT in the ISR) by tracking the last report time and
 * suppressing changes within CONFIG_HIVEKIT_PIR_DEBOUNCE_MS of each other.
 * Default: 250 ms.
 */

#include "sensor_pir.h"
#include "hivekit.h"
#include "sdkconfig.h"

#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "sensor_pir";

/* ── Configuration defaults (overridden by Kconfig / sdkconfig.defaults) ──── */

#ifndef CONFIG_HIVEKIT_PIR_GPIO
#define CONFIG_HIVEKIT_PIR_GPIO            2
#endif

/* Active-high: 1=HIGH means occupied, 0=LOW means occupied when false */
#ifndef CONFIG_HIVEKIT_PIR_ACTIVE_HIGH
#define CONFIG_HIVEKIT_PIR_ACTIVE_HIGH     1
#endif

/* Minimum milliseconds between occupancy state change reports */
#ifndef CONFIG_HIVEKIT_PIR_DEBOUNCE_MS
#define CONFIG_HIVEKIT_PIR_DEBOUNCE_MS     250
#endif

/* Keepalive report interval in seconds (also used as ZCL max-interval) */
#ifndef CONFIG_HIVEKIT_PIR_KEEPALIVE_INTERVAL_S
#define CONFIG_HIVEKIT_PIR_KEEPALIVE_INTERVAL_S  300
#endif

/* ── Constants ────────────────────────────────────────────────────────────── */

/* ISR queue depth: 8 events is more than enough for burst suppression */
#define PIR_ISR_QUEUE_DEPTH    8

/* Stack size for the consumer task */
#define PIR_TASK_STACK_BYTES   3072

/* ── Module state ─────────────────────────────────────────────────────────── */

static QueueHandle_t s_evt_queue = NULL;

/* ── ISR handler ──────────────────────────────────────────────────────────── */

/**
 * @brief GPIO ISR handler for PIR signal pin.
 *
 * IRAM_ATTR: placed in IRAM so it can run during flash cache misses.
 * This is mandatory for GPIO ISR handlers.
 * Source: ESP-IDF Programming Guide — GPIO Interrupt Latency
 * URL: https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/gpio.html
 *
 * Constraints (ISR context):
 *   - No heap allocation
 *   - No logging (ESP_LOGx calls are NOT ISR-safe)
 *   - No blocking operations
 *   - Use xQueueSendFromISR with portYIELD_FROM_ISR
 */
static void IRAM_ATTR pir_isr_handler(void *arg)
{
    /* Read current GPIO level directly — don't trust the edge direction */
    uint32_t level = (uint32_t)gpio_get_level((gpio_num_t)(intptr_t)arg);

    BaseType_t higher_prio_woken = pdFALSE;
    xQueueSendFromISR(s_evt_queue, &level, &higher_prio_woken);

    /* Yield to the consumer task if it has higher priority and was unblocked */
    portYIELD_FROM_ISR(higher_prio_woken);
}

/* ── Consumer task ────────────────────────────────────────────────────────── */

/**
 * @brief PIR event consumer task.
 *
 * Reads GPIO level events from the ISR queue, applies debounce, translates
 * the active-high/low polarity to an occupancy boolean, and calls
 * hivekit_report_pir().
 *
 * Keepalive: if no state change has been reported within the keepalive interval,
 * sends a repeat of the current state. This prevents Z2M from marking the
 * device unavailable.
 */
static void pir_consumer_task(void *pvParameters)
{
    const gpio_num_t pin           = (gpio_num_t)CONFIG_HIVEKIT_PIR_GPIO;
    const bool       active_high   = (bool)CONFIG_HIVEKIT_PIR_ACTIVE_HIGH;
    const TickType_t debounce_tick = pdMS_TO_TICKS(CONFIG_HIVEKIT_PIR_DEBOUNCE_MS);
    const TickType_t keepalive_tick =
        pdMS_TO_TICKS((uint32_t)CONFIG_HIVEKIT_PIR_KEEPALIVE_INTERVAL_S * 1000U);

    /* Read initial GPIO state so we report the correct starting occupancy */
    bool last_occupied = (gpio_get_level(pin) != 0) == active_high;
    TickType_t last_report_tick = 0; /* Will trigger an initial report on first keepalive */

    uint32_t gpio_level;

    while (1) {
        /* Block for up to keepalive_tick waiting for a new ISR event.
         * If the queue is empty for the whole interval, send a keepalive. */
        BaseType_t got_event = xQueueReceive(s_evt_queue, &gpio_level, keepalive_tick);

        TickType_t now = xTaskGetTickCount();

        if (got_event == pdTRUE) {
            /* Translate GPIO level to occupancy using polarity config */
            bool occupied = ((gpio_level != 0) == active_high);

            /* Debounce: ignore if same state, or if too soon after last report */
            if (occupied == last_occupied) {
                continue; /* Duplicate edge — skip */
            }
            if ((now - last_report_tick) < debounce_tick) {
                ESP_LOGD(TAG, "PIR debounce suppressed (level=%lu, occupied=%d)",
                         gpio_level, (int)occupied);
                continue;
            }

            last_occupied    = occupied;
            last_report_tick = now;

            ESP_LOGI(TAG, "PIR: motion %s (GPIO%d level=%lu)",
                     occupied ? "DETECTED" : "cleared",
                     (int)pin, gpio_level);

            hivekit_led_set_pattern(HIVEKIT_LED_SINGLE_FLASH);
            hivekit_report_pir(occupied);

        } else {
            /* Keepalive: no ISR event for the full interval — re-report current state */
            ESP_LOGD(TAG, "PIR keepalive: occupied=%d", (int)last_occupied);
            hivekit_report_pir(last_occupied);
            last_report_tick = now;
        }
    }
}

/* ── Public API ───────────────────────────────────────────────────────────── */

esp_err_t pir_init(void)
{
    const gpio_num_t pin = (gpio_num_t)CONFIG_HIVEKIT_PIR_GPIO;

    ESP_LOGI(TAG, "PIR init: GPIO%d, active-%s, debounce=%dms, keepalive=%ds",
             (int)pin,
             CONFIG_HIVEKIT_PIR_ACTIVE_HIGH ? "high" : "low",
             CONFIG_HIVEKIT_PIR_DEBOUNCE_MS,
             CONFIG_HIVEKIT_PIR_KEEPALIVE_INTERVAL_S);

    /* Validate GPIO number */
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_GPIO(pin),
                        ESP_ERR_INVALID_ARG, TAG,
                        "Invalid PIR GPIO: %d", (int)pin);

    /* Create the ISR → consumer queue */
    s_evt_queue = xQueueCreate(PIR_ISR_QUEUE_DEPTH, sizeof(uint32_t));
    ESP_RETURN_ON_FALSE(s_evt_queue, ESP_ERR_NO_MEM, TAG, "ISR queue create failed");

    /* Configure GPIO as input, no pull (PIR module drives the line actively) */
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        /* ANYEDGE: capture both motion-start (rising) and motion-end (falling).
         * Source: ESP-IDF GPIO driver — gpio_int_type_t
         * URL: https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/gpio.html */
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_cfg), TAG, "GPIO config failed");

    /* Install shared GPIO ISR service.
     * intr_alloc_flags=0: default flag (level interrupt, shared service handles routing).
     * If already installed by another driver, returns ESP_ERR_INVALID_STATE — treat as OK.
     * Source: gpio_install_isr_service() docs
     * URL: https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/gpio.html */
    esp_err_t isr_err = gpio_install_isr_service(0);
    if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(isr_err));
        return isr_err;
    }

    /* Register our ISR handler for this specific GPIO.
     * Pass pin number as the arg — the ISR reads it to call gpio_get_level(). */
    ESP_RETURN_ON_ERROR(
        gpio_isr_handler_add(pin, pir_isr_handler, (void *)(intptr_t)pin),
        TAG, "ISR handler add failed");

    /* Spawn the consumer task that debounces and calls hivekit_report_pir() */
    BaseType_t task_ok = xTaskCreate(
        pir_consumer_task,
        "pir_consumer",
        PIR_TASK_STACK_BYTES,
        NULL,
        4,     /* Same priority as sensor_task in SHT40/BME280 builds */
        NULL
    );
    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, TAG,
                        "Consumer task create failed");

    ESP_LOGI(TAG, "PIR ready (GPIO%d, ISR+queue+task installed)", (int)pin);
    return ESP_OK;
}
