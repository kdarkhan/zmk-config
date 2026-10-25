/*
 * Battery levels of both halves on the central's display, e.g. "L 80  R 75".
 * The central is the right half; the left half is peripheral 0, whose level
 * is fetched over the split link (CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING).
 */

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/split/central.h>

#include "split_battery_status.h"

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct split_battery_status_state {
    uint8_t left;
    uint8_t right;
};

static void set_battery_text(lv_obj_t *label, struct split_battery_status_state state) {
    char left[4] = "--";
    char right[4];

    // The peripheral's level stays 0 until the first report arrives
    if (state.left > 0) {
        snprintf(left, sizeof(left), "%u", state.left);
    }
    snprintf(right, sizeof(right), "%u", state.right);

    lv_label_set_text_fmt(label, "L %s  R %s", left, right);
}

static void split_battery_status_update_cb(struct split_battery_status_state state) {
    struct zmk_widget_split_battery_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_text(widget->obj, state); }
}

static struct split_battery_status_state split_battery_status_get_state(const zmk_event_t *eh) {
    uint8_t left = 0;
    zmk_split_central_get_peripheral_battery_level(0, &left);

    return (struct split_battery_status_state){
        .left = left,
        .right = zmk_battery_state_of_charge(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_split_battery_status, struct split_battery_status_state,
                            split_battery_status_update_cb, split_battery_status_get_state)

ZMK_SUBSCRIPTION(widget_split_battery_status, zmk_battery_state_changed);
ZMK_SUBSCRIPTION(widget_split_battery_status, zmk_peripheral_battery_state_changed);

int zmk_widget_split_battery_status_init(struct zmk_widget_split_battery_status *widget,
                                         lv_obj_t *parent) {
    widget->obj = lv_label_create(parent);
    lv_obj_set_style_text_font(widget->obj, &lv_font_montserrat_16, LV_PART_MAIN);

    sys_slist_append(&widgets, &widget->node);

    widget_split_battery_status_init();
    return 0;
}

lv_obj_t *zmk_widget_split_battery_status_obj(struct zmk_widget_split_battery_status *widget) {
    return widget->obj;
}
