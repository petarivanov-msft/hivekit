# HiveKit — Button & LED UX Spec

Standard UX for every HiveKit sensor. Designed for the **single BOOT button** on XIAO ESP32-C6 (GPIO9), but the same logic applies to any GPIO button.

## Button behaviour

| Action | Duration | What happens |
|---|---|---|
| **Single short press** | < 1s | Send "identify" (LED blinks 3×) — useful to find which sensor is which in Z2M |
| **Long press** | **3 seconds** | **Trigger pairing mode** (network steering). LED slow-blinks while searching. |
| **Very long press** | **10 seconds** | **Factory reset** — leave network, wipe Zigbee NVS, reboot fresh. LED fast-blinks during reset. |
| Double short press | reserved | TBD (force-report sensor values?) |

## LED states

| State | Pattern |
|---|---|
| Booting / initialising | Solid on |
| Searching for network (pairing mode) | Slow blink, 1 Hz |
| Joined successfully | Solid for 2s, then off |
| Sending a reading | Single 100ms flash |
| Factory reset in progress | Fast blink, 5 Hz for 1s, then off |
| Sleeping | Off |
| Error | 3 short flashes, then off |

## Implementation (ESP-Zigbee-SDK v2.0)

> All API names verified against [`ezbee/bdb.h` on `main`](https://github.com/espressif/esp-zigbee-sdk/blob/main/components/esp-zigbee-lib/include/ezbee/bdb.h) (Apache-2.0, Espressif Systems).

### Pairing (3-second press)

```c
#include "ezbee/bdb.h"

static void on_button_long_press_3s(void) {
    if (ezb_bdb_dev_joined()) {
        // Already joined — flash LED twice, do nothing
        hivekit_led_blink(2, 100);
        return;
    }
    // Not joined → kick off network steering
    ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
    hivekit_led_set_pattern(LED_PATTERN_SLOW_BLINK);
}
```

### Factory reset (10-second press)

```c
#include "ezbee/bdb.h"
#include "esp_zigbee_core.h"   // for esp_zb_nvram_erase_at_start

static void on_button_long_press_10s(void) {
    hivekit_led_set_pattern(LED_PATTERN_FAST_BLINK);

    if (ezb_bdb_dev_joined()) {
        // Proper Zigbee leave + NVS clear (v2 API).
        // Triggers ZB_ZDO_SIGNAL_LEAVE with EZB_NWK_LEAVE_TYPE_RESET.
        ezb_bdb_reset_via_local_action();
    } else {
        // Not on a network → ezb_bdb_reset_via_local_action() is a no-op.
        // Fall back to wiping the NVRAM on next boot and rebooting.
        esp_zb_nvram_erase_at_start(true);
    }

    // Give the stack a moment to flush, then reboot.
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}
```

### Single short press (identify)

```c
static void on_button_single_press(void) {
    if (!ezb_bdb_dev_joined()) return;
    // Send "identify" - LED blinks so user knows which physical device this is
    hivekit_led_blink(3, 200);
    // Could also send an Identify cluster command to the coordinator
}
```

## Why these timings

- **3 s for pairing** — long enough to not trigger from accidental brush, short enough that a user doesn't give up
- **10 s for factory reset** — deliberately long so it can't happen by accident; matches "nuclear option" UX from IKEA, Xiaomi, Sonoff
- **No Zigbee spec mandates timings** — these are pure UX choice. Vendors vary wildly (Xiaomi 5s reset, IKEA 4 quick presses, Hue paperclip).

## XIAO ESP32-C6 specifics

- BOOT button is on **GPIO9**
- Active LOW (pressed = 0)
- Pull-up is internal — enable in code
- ⚠️ Same GPIO holds the chip in download mode at boot. If user holds BOOT during USB plug-in, chip enters flash mode — that's intentional and how the web flasher works.

## Dependencies (`idf_component.yml`)

```yaml
dependencies:
  espressif/button: "^4.0.0"
  espressif/led_indicator: "^0.9.0"
  espressif/esp-zboss-lib: "~2.0.0"
  espressif/esp-zigbee-lib: "~2.0.0"
```

## References

- ESP-Zigbee-SDK v2 BDB header: <https://github.com/espressif/esp-zigbee-sdk/blob/main/components/esp-zigbee-lib/include/ezbee/bdb.h>
- v2 migration guide (commissioning): <https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32/migration-guide/v2.x/commissioning.html>
- Discussion on factory-reset / leave behaviour: <https://github.com/espressif/esp-zigbee-sdk/issues/66>
- Force fresh steering / NVS erase: <https://github.com/espressif/esp-zigbee-sdk/issues/796>
- `espressif/button` component: <https://components.espressif.com/components/espressif/button>
