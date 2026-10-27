/*
 * Cut the nice!nano VCC output (LEDs, and the display on the right half) when
 * the keyboard goes idle, and bring it back on the next keypress. The LEDs
 * draw ~0.5-1 mA each even when dark.
 *
 * This replaces ZMK's RGB_UNDERGLOW_AUTO_OFF_IDLE, which turns the LEDs off
 * through zmk_rgb_underglow_off() and so saves "off" to flash 60 s later. A
 * half that then goes into deep sleep wakes with its LEDs off for good. Here
 * only the animation is paused, so the saved LED state never changes.
 *
 * The Sharp memory display loses its image without power, so on wake the
 * whole LVGL screen is invalidated and redrawn. Deep sleep cuts VCC by itself
 * (PM suspend of the EXT_POWER device) and wakes with a reset.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/settings/settings.h>

#include <drivers/ext_power.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/rgb_underglow.h>
#include <zmk/workqueue.h>

#if IS_ENABLED(CONFIG_ZMK_DISPLAY)
#include <lvgl.h>
#include <zmk/display.h>
#endif

#define STRIP_NODE DT_CHOSEN(zmk_underglow)
#define STRIP_NUM_PIXELS DT_PROP(STRIP_NODE, chain_length)

/* Driven directly instead of through ext_power_enable/disable: those save the
 * state to flash on every change. */
static const struct gpio_dt_spec vcc = GPIO_DT_SPEC_GET(DT_NODELABEL(vcc_switch), control_gpios);
static const struct device *const ext_power = DEVICE_DT_GET(DT_NODELABEL(vcc_switch));
static const struct device *const led_strip = DEVICE_DT_GET(STRIP_NODE);

/* rgb_underglow.c's animation timer, stopped while idle */
extern struct k_timer underglow_tick;

#if IS_ENABLED(CONFIG_ZMK_DISPLAY)
static const struct gpio_dt_spec extcomin = GPIO_DT_SPEC_GET(DT_NODELABEL(lcd), extcomin_gpios);

/* The ls0xx driver's EXTCOMIN toggle thread. Paused while the display is
 * unpowered so it doesn't feed the panel through its input pin. */
extern struct k_thread vcom_toggle_thread;

/* The display work queue owns LVGL and the display */
#define IDLE_POWER_WORK_Q zmk_display_work_q()
#else
#define IDLE_POWER_WORK_Q zmk_workqueue_lowprio_work_q()
#endif

static bool vcc_off;

/* Black frame, so the LEDs go dark at once instead of fading as VCC drops,
 * and show nothing after VCC comes back while the LEDs are off. */
static void clear_leds(void) {
    static struct led_rgb pixels[STRIP_NUM_PIXELS];

    led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);
}

static void vcc_off_cb(struct k_work *work) {
    LOG_DBG("idle: cutting VCC");
    k_timer_stop(&underglow_tick);
    clear_leds();
#if IS_ENABLED(CONFIG_ZMK_DISPLAY)
    k_thread_suspend(&vcom_toggle_thread);
    gpio_pin_set_dt(&extcomin, 0);
#endif
    gpio_pin_set_dt(&vcc, 0);
    vcc_off = true;
}

/* Delayed so the display blanking runs first */
static K_WORK_DELAYABLE_DEFINE(vcc_off_work, vcc_off_cb);

static void vcc_on_cb(struct k_work *work) {
    if (!vcc_off) {
        return;
    }

    LOG_DBG("active: restoring VCC");
    vcc_off = false;

    /* With RGB_UNDERGLOW_EXT_POWER (left half) VCC stays off while the LEDs
     * are toggled off */
    if (ext_power_get(ext_power)) {
        gpio_pin_set_dt(&vcc, 1);
        k_msleep(5);
    }

    bool leds_on = false;
    zmk_rgb_underglow_get_state(&leds_on);
    if (leds_on) {
        k_timer_start(&underglow_tick, K_NO_WAIT, K_MSEC(50));
    } else {
        clear_leds();
    }

#if IS_ENABLED(CONFIG_ZMK_DISPLAY)
    k_thread_resume(&vcom_toggle_thread);
    lv_obj_invalidate(lv_scr_act());
#endif
}

static K_WORK_DEFINE(vcc_on_work, vcc_on_cb);

static int idle_power_listener(const zmk_event_t *eh) {
    struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);
    if (ev == NULL) {
        return -ENOTSUP;
    }

    switch (ev->state) {
    case ZMK_ACTIVITY_ACTIVE:
        k_work_cancel_delayable(&vcc_off_work);
        k_work_submit_to_queue(IDLE_POWER_WORK_Q, &vcc_on_work);
        break;
    case ZMK_ACTIVITY_IDLE:
        k_work_schedule_for_queue(IDLE_POWER_WORK_Q, &vcc_off_work, K_MSEC(200));
        break;
    default:
        break;
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(idle_power, idle_power_listener);
ZMK_SUBSCRIPTION(idle_power, zmk_activity_state_changed);

#if !IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)
/* The EXT_POWER on/off state is kept in flash and applied at boot. A stale
 * "off" saved by an earlier &ext_power test would leave VCC cut until the
 * idle code first toggles it, so force it back on once settings are loaded.
 * ext_power_enable saves "on", so this only fixes the stored state once. */
static int idle_power_settings_commit(void) {
    if (!ext_power_get(ext_power)) {
        LOG_DBG("EXT_POWER was saved off, turning it on");
        ext_power_enable(ext_power);
    }

    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(mango_vcc, "mango_vcc", NULL, NULL, idle_power_settings_commit,
                               NULL);
#endif
