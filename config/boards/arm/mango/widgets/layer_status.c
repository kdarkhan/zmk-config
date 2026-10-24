/*
 *
 * Copyright (c) 2023 HellTM
 * SPDX-License-Identifier: MIT
 *
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include "layer_status.h"
#include <zmk/events/layer_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>

LV_IMG_DECLARE(layer_0);
LV_IMG_DECLARE(layer_0_1);
const void *layer_0_images[] = {
    &layer_0_1, &layer_0, 
};

LV_IMG_DECLARE(layer_1);
LV_IMG_DECLARE(layer_1_1);
const void *layer_1_images[] = {
    &layer_1_1, &layer_1, 
};

LV_IMG_DECLARE(layer_2);
LV_IMG_DECLARE(layer_2_1);
const void *layer_2_images[] = {
    &layer_2_1, &layer_2, 
};

LV_IMG_DECLARE(layer_3);
LV_IMG_DECLARE(layer_3_1);
const void *layer_3_images[] = {
    &layer_3_1, &layer_3, 
};

LV_IMG_DECLARE(layer_unknown);
LV_IMG_DECLARE(layer_unknown_1);
const void *layer_unknown_images[] = {
    &layer_unknown_1, &layer_unknown, 
};

// explicit function declarations
void init_layer_anim(struct zmk_widget_layer_status *widget);

lv_anim_t layer_anim;
const void **layer_images;
int current_layer_frame = 0;

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets); 

struct layer_status_state {
    uint8_t index;
    const char *label;
};

static struct layer_status_state get_state(const zmk_event_t *eh) {
    uint8_t index = zmk_keymap_highest_layer_active();
    return (struct layer_status_state){.index = index, .label = zmk_keymap_layer_name(index)};
}

void animate_layer_images(void * var, int value) {
    lv_obj_t *obj = (lv_obj_t *)var;
    lv_img_set_src(obj, layer_images[current_layer_frame]);
    if (current_layer_frame == 0){
        current_layer_frame = 1;
    } else {
        current_layer_frame = 0;
    }
}

static void set_layer_indicator(lv_obj_t *icon, struct layer_status_state state) {
    const char *layer_label = state.label;
    uint8_t active_layer_index = state.index;

    switch (active_layer_index) {
    case 0:
        layer_images = layer_0_images;
        break;
    case 1:
        layer_images = layer_1_images;
        break;
    case 2:
        layer_images = layer_2_images;
        break;
    case 3:
        layer_images = layer_3_images;
        break;
    default:
        layer_images = layer_unknown_images;
    }

    // restart animation
    struct zmk_widget_layer_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        lv_anim_del(icon, (lv_anim_exec_xcb_t) animate_layer_images);
        current_layer_frame = 0;
        init_layer_anim(widget);
    }
}

void init_layer_anim(struct zmk_widget_layer_status *widget) {
    // Initialize the animation
    lv_anim_init(&layer_anim);
    lv_anim_set_var(&layer_anim, widget->obj);
    lv_anim_set_exec_cb(&layer_anim, (lv_anim_exec_xcb_t) animate_layer_images);
    lv_anim_set_time(&layer_anim, 100);
    lv_anim_set_values(&layer_anim, 0, 1);
    lv_anim_set_delay(&layer_anim, 0);
    lv_anim_set_repeat_count(&layer_anim, 0);
    lv_anim_start(&layer_anim);
}

static void layer_status_update_cb(struct layer_status_state state) {
    struct zmk_widget_layer_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_layer_indicator(widget->obj, state); }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_layer_status, struct layer_status_state, layer_status_update_cb, get_state)

ZMK_SUBSCRIPTION(widget_layer_status, zmk_layer_state_changed);

int zmk_widget_layer_status_init(struct zmk_widget_layer_status *widget, lv_obj_t *parent) {
    widget->obj = lv_img_create(parent);

    sys_slist_append(&widgets, &widget->node);

    layer_images = layer_0_images;
    init_layer_anim(widget);

    widget_layer_status_init();
    return 0;
}

lv_obj_t *zmk_widget_layer_status_obj(struct zmk_widget_layer_status *widget) {
    return widget->obj;
}
