#include "fuse_radio_ble_scan_view.h"

#include <gui/canvas.h>
#include <gui/elements.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct FuseRadioBleScanView {
    View* view;
    FuseRadioBleScanViewCallback callback;
    void* context;
};

typedef struct {
    FuseRadioBleScanResults scan_results;
    FuseRadioSavedBleResults saved_results;
    FuseRadioBleScanViewMode mode;
    uint8_t selected_index;
    uint8_t animation_phase;
} FuseRadioBleScanViewModel;

#define FUSE_RADIO_BLE_TITLE_X 4

static const char* fuse_radio_ble_scan_view_scanning_text(uint8_t phase) {
    switch(phase & 0x03U) {
    case 1U:
        return "Scanning.";
    case 2U:
        return "Scanning..";
    case 3U:
        return "Scanning...";
    default:
        return "Scanning";
    }
}

static uint8_t fuse_radio_ble_scan_view_signal_bars(int16_t rssi) {
    if(rssi >= -55) return 4;
    if(rssi >= -67) return 3;
    if(rssi >= -78) return 2;
    if(rssi >= -90) return 1;
    return 0;
}

static void fuse_radio_ble_scan_view_draw_signal(Canvas* canvas, int16_t rssi) {
    const uint8_t bars = fuse_radio_ble_scan_view_signal_bars(rssi);
    const uint8_t base_x = 103;
    const uint8_t base_y = 31;

    for(uint8_t index = 0; index < 4; index++) {
        const uint8_t height = 3 + index * 3;
        const uint8_t x = base_x + index * 5;
        const uint8_t y = base_y - height;

        if(index < bars) {
            canvas_draw_box(canvas, x, y, 3, height);
        } else {
            canvas_draw_frame(canvas, x, y, 3, height);
        }
    }
}

static void fuse_radio_ble_scan_view_copy_fit(
    char* dst,
    size_t dst_size,
    const char* src,
    size_t max_visible_chars) {
    if(dst == NULL || dst_size == 0U) {
        return;
    }

    dst[0] = '\0';
    if(src == NULL || src[0] == '\0') {
        return;
    }

    size_t copy_len = strlen(src);
    if(copy_len > max_visible_chars) {
        copy_len = max_visible_chars;
    }
    if(copy_len >= dst_size) {
        copy_len = dst_size - 1U;
    }

    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';

    if(copy_len < strlen(src) && copy_len >= 2U) {
        dst[copy_len - 2U] = '.';
        dst[copy_len - 1U] = '.';
    }
}

static void fuse_radio_ble_scan_view_draw_badge(
    Canvas* canvas,
    uint8_t x,
    uint8_t y,
    const char* text) {
    canvas_draw_box(canvas, x, y, 24, 11);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, x + 2, y + 9, text);
    canvas_set_color(canvas, ColorBlack);
}

static const char* fuse_radio_ble_scan_view_title_for_device(
    const FuseRadioBleDevice* device,
    char* buffer,
    size_t buffer_size) {
    if(device->has_name) {
        return device->name;
    }

    if(device->has_appearance) {
        return device->appearance;
    }

    if(device->has_company) {
        snprintf(buffer, buffer_size, "%s BLE", device->company);
        return buffer;
    }

    if(device->classification[0]) {
        return device->classification;
    }

    return "Nearby BLE";
}

static const char* fuse_radio_ble_scan_view_title_for_saved(
    const FuseRadioSavedBleDevice* device,
    char* buffer,
    size_t buffer_size) {
    if(device->has_name) {
        return device->name;
    }

    if(device->has_appearance) {
        return device->appearance;
    }

    if(device->has_company) {
        snprintf(buffer, buffer_size, "%s BLE", device->company);
        return buffer;
    }

    if(device->classification[0]) {
        return device->classification;
    }

    return "Saved BLE";
}

static void fuse_radio_ble_scan_view_draw_scan_card(
    Canvas* canvas,
    const FuseRadioBleScanResults* results,
    uint8_t selected_index) {
    const FuseRadioBleDevice* device = &results->devices[selected_index];
    char primary[20];
    char title[20];
    char detail[24];
    char detail2[24];
    char counter[18];
    char rssi[18];

    canvas_draw_rframe(canvas, 4, 17, 120, 38, 2);
    canvas_set_font(canvas, FontPrimary);
    fuse_radio_ble_scan_view_copy_fit(
        primary,
        sizeof(primary),
        fuse_radio_ble_scan_view_title_for_device(device, title, sizeof(title)),
        16U);
    canvas_draw_str(canvas, 8, 28, primary);

    canvas_set_font(canvas, FontSecondary);
    snprintf(
        detail,
        sizeof(detail),
        "%s  %s",
        device->classification[0] ? device->classification : "BLE",
        device->has_company ? device->company : "unknown");
    canvas_draw_str(canvas, 8, 39, detail);
    snprintf(
        detail2,
        sizeof(detail2),
        "%s  %s",
        device->proximity[0] ? device->proximity : "Far",
        device->has_appearance ? device->appearance : (device->connectable ? "ready" : "seen"));
    canvas_draw_str(canvas, 8, 50, detail2);

    snprintf(rssi, sizeof(rssi), "%d dBm", device->rssi);
    canvas_draw_str(canvas, 78, 50, rssi);
    snprintf(counter, sizeof(counter), "%u/%u", (unsigned)(selected_index + 1U), (unsigned)results->count);
    canvas_draw_str(canvas, 92, 39, counter);
    fuse_radio_ble_scan_view_draw_signal(canvas, device->rssi);
}

static void fuse_radio_ble_scan_view_draw_saved_card(
    Canvas* canvas,
    const FuseRadioSavedBleResults* results,
    uint8_t selected_index) {
    const FuseRadioSavedBleDevice* device = &results->devices[selected_index];
    char primary[20];
    char title[20];
    char detail[24];
    char detail2[24];
    char counter[18];

    canvas_draw_rframe(canvas, 4, 17, 120, 38, 2);
    canvas_set_font(canvas, FontPrimary);
    fuse_radio_ble_scan_view_copy_fit(
        primary,
        sizeof(primary),
        fuse_radio_ble_scan_view_title_for_saved(device, title, sizeof(title)),
        16U);
    canvas_draw_str(canvas, 8, 28, primary);

    canvas_set_font(canvas, FontSecondary);
    snprintf(
        detail,
        sizeof(detail),
        "%s  %s",
        device->classification[0] ? device->classification : "BLE",
        device->has_company ? device->company : "saved");
    canvas_draw_str(canvas, 8, 39, detail);
    snprintf(
        detail2,
        sizeof(detail2),
        "%s  %s",
        device->proximity[0] ? device->proximity : (device->seen_recently ? "Near" : "saved"),
        device->has_appearance ? device->appearance : (device->seen_recently ? "live" : "stored"));
    canvas_draw_str(canvas, 8, 50, detail2);
    snprintf(counter, sizeof(counter), "%u/%u", (unsigned)(selected_index + 1U), (unsigned)results->count);
    canvas_draw_str(canvas, 92, 39, counter);

    if(device->seen_recently) {
        fuse_radio_ble_scan_view_draw_badge(canvas, 92, 19, "LIVE");
        fuse_radio_ble_scan_view_draw_signal(canvas, device->last_rssi);
    } else {
        fuse_radio_ble_scan_view_draw_badge(canvas, 92, 19, "SAVED");
    }
}

static void fuse_radio_ble_scan_view_draw_progress(
    Canvas* canvas,
    const FuseRadioBleScanResults* results,
    uint8_t animation_phase) {
    char line[32];
    char detail[32];
    char detail2[32];
    char percent[8];
    const uint8_t fill_width = (uint8_t)((108U * results->progress_percent) / 100U);
    const uint16_t duration_ms = results->scan_duration_ms ? results->scan_duration_ms : 4500U;

    snprintf(percent, sizeof(percent), "%u%%", (unsigned)results->progress_percent);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 95, 25, percent);

    canvas_draw_rframe(canvas, 9, 31, 110, 11, 2);
    if(fill_width > 0U) {
        canvas_draw_box(canvas, 10, 32, fill_width, 9);
    }

    canvas_set_font(canvas, FontSecondary);
    snprintf(
        line,
        sizeof(line),
        "Window %u.%us  Seen %u",
        (unsigned)(duration_ms / 1000U),
        (unsigned)((duration_ms % 1000U) / 100U),
        (unsigned)results->count);
    canvas_draw_str(canvas, 8, 49, line);

    snprintf(
        detail,
        sizeof(detail),
        "%s nearby advertisers",
        fuse_radio_ble_scan_view_scanning_text(animation_phase));
    canvas_draw_str(canvas, 8, 60, detail);

    if(results->count > 0U) {
        snprintf(detail2, sizeof(detail2), "Results after scan");
        canvas_draw_str(canvas, 8, 24, detail2);
    }
}

static void fuse_radio_ble_scan_view_draw_callback(Canvas* canvas, void* model) {
    FuseRadioBleScanViewModel* view_model = model;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    if(view_model->mode == FuseRadioBleScanViewModeSaved) {
        canvas_draw_str(canvas, FUSE_RADIO_BLE_TITLE_X, 10, "Saved BLE");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 79, 10, "Library");
        canvas_draw_line(canvas, 0, 13, 127, 13);

        if(view_model->saved_results.count == 0U) {
            elements_multiline_text_aligned(
                canvas,
                64,
                31,
                AlignCenter,
                AlignCenter,
                "No saved devices yet\nSave one from Scan Nearby");
            return;
        }

        fuse_radio_ble_scan_view_draw_saved_card(
            canvas, &view_model->saved_results, view_model->selected_index);
        elements_button_center(canvas, "Menu");
        return;
    }

    canvas_draw_str(canvas, FUSE_RADIO_BLE_TITLE_X, 10, "BLE Scan");
    canvas_set_font(canvas, FontSecondary);
    if(view_model->scan_results.active) {
        canvas_draw_str(
            canvas, 66, 10, fuse_radio_ble_scan_view_scanning_text(view_model->animation_phase));
    } else if(view_model->scan_results.has_error) {
        canvas_draw_str(canvas, 88, 10, "Error");
    } else {
        canvas_draw_str(canvas, 76, 10, "Results");
    }

    canvas_draw_line(canvas, 0, 13, 127, 13);

    if(view_model->scan_results.has_error) {
        elements_multiline_text_aligned(
            canvas,
            64,
            30,
            AlignCenter,
            AlignCenter,
            view_model->scan_results.error);
        elements_button_center(canvas, "Again");
        return;
    }

    if(view_model->scan_results.active) {
        fuse_radio_ble_scan_view_draw_progress(
            canvas, &view_model->scan_results, view_model->animation_phase);
        return;
    }

    if(view_model->scan_results.count == 0U) {
        elements_multiline_text_aligned(
            canvas, 64, 28, AlignCenter, AlignCenter, "No BLE advertisers\nfound yet");
        elements_button_center(canvas, "Again");
        return;
    }

    fuse_radio_ble_scan_view_draw_scan_card(
        canvas, &view_model->scan_results, view_model->selected_index);
    elements_button_left(canvas, "Again");
    elements_button_center(canvas, "Menu");
}

static bool fuse_radio_ble_scan_view_input_callback(InputEvent* event, void* context) {
    FuseRadioBleScanView* instance = context;
    bool consumed = false;
    FuseRadioBleScanViewAction action = FuseRadioBleScanViewActionRefresh;
    bool trigger_action = false;

    if(event->type != InputTypeShort) {
        return false;
    }

    with_view_model(
        instance->view,
        FuseRadioBleScanViewModel * model,
        {
            const uint8_t count =
                model->mode == FuseRadioBleScanViewModeSaved ? model->saved_results.count :
                                                             model->scan_results.count;

            if(model->mode == FuseRadioBleScanViewModeScan && model->scan_results.active) {
                consumed = false;
            } else if(event->key == InputKeyUp && model->selected_index > 0U) {
                model->selected_index--;
                consumed = true;
            } else if(event->key == InputKeyDown && (model->selected_index + 1U) < count) {
                model->selected_index++;
                consumed = true;
            } else if(event->key == InputKeyLeft && model->mode == FuseRadioBleScanViewModeScan) {
                consumed = true;
                trigger_action = true;
                action = FuseRadioBleScanViewActionRefresh;
            } else if(event->key == InputKeyOk) {
                consumed = true;
                trigger_action = true;
                action = count > 0U ? FuseRadioBleScanViewActionSelect :
                                      FuseRadioBleScanViewActionRefresh;
            }
        },
        consumed);

    if(trigger_action && instance->callback) {
        instance->callback(action, instance->context);
    }

    return consumed;
}

FuseRadioBleScanView* fuse_radio_ble_scan_view_alloc(void) {
    FuseRadioBleScanView* instance = malloc(sizeof(FuseRadioBleScanView));

    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(FuseRadioBleScanViewModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, fuse_radio_ble_scan_view_draw_callback);
    view_set_input_callback(instance->view, fuse_radio_ble_scan_view_input_callback);

    return instance;
}

void fuse_radio_ble_scan_view_free(FuseRadioBleScanView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* fuse_radio_ble_scan_view_get_view(FuseRadioBleScanView* instance) {
    furi_assert(instance);
    return instance->view;
}

void fuse_radio_ble_scan_view_set_callback(
    FuseRadioBleScanView* instance,
    FuseRadioBleScanViewCallback callback,
    void* context) {
    furi_assert(instance);
    instance->callback = callback;
    instance->context = context;
}

void fuse_radio_ble_scan_view_set_mode(
    FuseRadioBleScanView* instance,
    FuseRadioBleScanViewMode mode) {
    furi_assert(instance);

    with_view_model(
        instance->view,
        FuseRadioBleScanViewModel * model,
        {
            model->mode = mode;
            if(mode == FuseRadioBleScanViewModeSaved && model->selected_index >= model->saved_results.count) {
                model->selected_index = model->saved_results.count > 0U ? model->saved_results.count - 1U : 0U;
            }
            if(mode == FuseRadioBleScanViewModeScan && model->selected_index >= model->scan_results.count) {
                model->selected_index = model->scan_results.count > 0U ? model->scan_results.count - 1U : 0U;
            }
        },
        true);
}

void fuse_radio_ble_scan_view_set_scan_data(
    FuseRadioBleScanView* instance,
    const FuseRadioBleScanResults* results) {
    furi_assert(instance);
    furi_assert(results);

    with_view_model(
        instance->view,
        FuseRadioBleScanViewModel * model,
        {
            const uint8_t previous = model->selected_index;
            model->scan_results = *results;
            if(model->scan_results.count == 0U) {
                model->selected_index = 0U;
            } else if(previous < model->scan_results.count) {
                model->selected_index = previous;
            } else {
                model->selected_index = model->scan_results.count - 1U;
            }
        },
        true);
}

void fuse_radio_ble_scan_view_set_saved_data(
    FuseRadioBleScanView* instance,
    const FuseRadioSavedBleResults* results) {
    furi_assert(instance);
    furi_assert(results);

    with_view_model(
        instance->view,
        FuseRadioBleScanViewModel * model,
        {
            const uint8_t previous = model->selected_index;
            model->saved_results = *results;
            if(model->saved_results.count == 0U) {
                model->selected_index = 0U;
            } else if(previous < model->saved_results.count) {
                model->selected_index = previous;
            } else {
                model->selected_index = model->saved_results.count - 1U;
            }
        },
        true);
}

uint8_t fuse_radio_ble_scan_view_get_selected_index(FuseRadioBleScanView* instance) {
    furi_assert(instance);

    uint8_t selected_index = 0U;
    with_view_model(
        instance->view,
        FuseRadioBleScanViewModel * model,
        { selected_index = model->selected_index; },
        false);
    return selected_index;
}

void fuse_radio_ble_scan_view_advance_animation(FuseRadioBleScanView* instance) {
    furi_assert(instance);

    with_view_model(
        instance->view,
        FuseRadioBleScanViewModel * model,
        {
            if(model->mode == FuseRadioBleScanViewModeScan && model->scan_results.active) {
                model->animation_phase = (uint8_t)((model->animation_phase + 1U) & 0x03U);
            }
        },
        true);
}