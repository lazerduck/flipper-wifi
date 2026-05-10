#include "fuse_radio_gatt_browser_view.h"

#include <gui/canvas.h>
#include <gui/elements.h>

#include <stdio.h>
#include <string.h>

struct FuseRadioGattBrowserView {
    View* view;
};

typedef struct {
    FuseRadioGattResults results;
    uint8_t selected_service;
    uint8_t selected_characteristic;
} FuseRadioGattBrowserViewModel;

static void fuse_radio_gatt_browser_view_copy_fit(
    char* dst,
    size_t dst_size,
    const char* src,
    size_t max_chars) {
    size_t src_len;

    if(dst == NULL || dst_size == 0U) {
        return;
    }

    dst[0] = '\0';
    if(src == NULL || src[0] == '\0') {
        return;
    }

    src_len = strlen(src);
    size_t copy_len = src_len;
    if(copy_len > max_chars) {
        copy_len = max_chars;
    }
    if(copy_len >= dst_size) {
        copy_len = dst_size - 1U;
    }

    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';

    if(copy_len < src_len && copy_len >= 2U) {
        dst[copy_len - 2U] = '.';
        dst[copy_len - 1U] = '.';
    }
}

static void fuse_radio_gatt_browser_view_draw_service_header(
    Canvas* canvas,
    const FuseRadioGattBrowserViewModel* model) {
    const uint8_t svc_count = model->results.svc_count;
    char header[30];

    canvas_set_font(canvas, FontPrimary);

    if(svc_count == 0U) {
        canvas_draw_str(canvas, 4, 10, "GATT Explorer");
        canvas_draw_line(canvas, 0, 13, 127, 13);
        return;
    }

    const FuseRadioGattService* svc = &model->results.svcs[model->selected_service];
    const char* svc_title = (svc->name[0] != '\0' && svc->name[0] != '-') ? svc->name : svc->uuid;
    char title[20];

    fuse_radio_gatt_browser_view_copy_fit(title, sizeof(title), svc_title, 12U);

    snprintf(
        header,
        sizeof(header),
        "< %.12s %u/%u >",
        title,
        (unsigned)(model->selected_service + 1U),
        (unsigned)svc_count);

    canvas_draw_str(canvas, 4, 10, header);
    canvas_draw_line(canvas, 0, 13, 127, 13);
}

static void fuse_radio_gatt_browser_view_draw_empty(Canvas* canvas, const char* text) {
    elements_multiline_text_aligned(canvas, 64, 41, AlignCenter, AlignCenter, text);
}

static void fuse_radio_gatt_browser_view_draw_characteristic(
    Canvas* canvas,
    const FuseRadioGattCharacteristic* chr,
    bool selected,
    uint8_t y) {
    char name[14];
    char value[22];
    char line[36];

    const char* chr_name = (chr->name[0] != '\0' && chr->name[0] != '-') ? chr->name : chr->uuid;

    if(chr->has_value && chr->value[0] != '\0') {
        fuse_radio_gatt_browser_view_copy_fit(value, sizeof(value), chr->value, 16U);
    } else {
        fuse_radio_gatt_browser_view_copy_fit(value, sizeof(value), chr->props, 16U);
    }

    fuse_radio_gatt_browser_view_copy_fit(name, sizeof(name), chr_name, 11U);
    snprintf(line, sizeof(line), "%s %s", name, value);

    if(selected) {
        canvas_draw_box(canvas, 2, y, 124, 10);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_rframe(canvas, 2, y, 124, 10, 1);
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 5, y + 8, line);

    if(selected) {
        canvas_set_color(canvas, ColorBlack);
    }
}

static void fuse_radio_gatt_browser_view_draw_callback(Canvas* canvas, void* model) {
    const FuseRadioGattBrowserViewModel* m = model;

    canvas_clear(canvas);
    fuse_radio_gatt_browser_view_draw_service_header(canvas, m);

    if(m->results.svc_count == 0U) {
        fuse_radio_gatt_browser_view_draw_empty(canvas, "No services discovered");
        elements_button_left(canvas, "Back");
        return;
    }

    const FuseRadioGattService* svc = &m->results.svcs[m->selected_service];

    if(svc->chr_count == 0U) {
        fuse_radio_gatt_browser_view_draw_empty(canvas, "No chars in service");
        elements_button_left(canvas, "Back");
        return;
    }

    const uint8_t selected = m->selected_characteristic;
    const uint8_t first = (selected > 1U) ? (uint8_t)(selected - 1U) : 0U;
    uint8_t draw_index = first;
    uint8_t row_y = 16U;

    for(uint8_t rows = 0U; rows < 4U && draw_index < svc->chr_count; rows++, draw_index++) {
        fuse_radio_gatt_browser_view_draw_characteristic(
            canvas,
            &svc->chrs[draw_index],
            draw_index == selected,
            row_y);
        row_y = (uint8_t)(row_y + 10U);
    }
}

static bool fuse_radio_gatt_browser_view_input_callback(InputEvent* event, void* context) {
    FuseRadioGattBrowserView* instance = context;
    bool consumed = false;

    if(event->type != InputTypeShort) {
        return false;
    }

    with_view_model(
        instance->view,
        FuseRadioGattBrowserViewModel * model,
        {
            if(model->results.svc_count == 0U) {
                consumed = (event->key != InputKeyBack);
            } else {
                if(event->key == InputKeyLeft) {
                    if(model->selected_service > 0U) {
                        model->selected_service--;
                    } else {
                        model->selected_service = (uint8_t)(model->results.svc_count - 1U);
                    }

                    const FuseRadioGattService* svc = &model->results.svcs[model->selected_service];
                    if(svc->chr_count == 0U) {
                        model->selected_characteristic = 0U;
                    } else if(model->selected_characteristic >= svc->chr_count) {
                        model->selected_characteristic = (uint8_t)(svc->chr_count - 1U);
                    }
                    consumed = true;
                } else if(event->key == InputKeyRight) {
                    model->selected_service =
                        (uint8_t)((model->selected_service + 1U) % model->results.svc_count);

                    const FuseRadioGattService* svc = &model->results.svcs[model->selected_service];
                    if(svc->chr_count == 0U) {
                        model->selected_characteristic = 0U;
                    } else if(model->selected_characteristic >= svc->chr_count) {
                        model->selected_characteristic = (uint8_t)(svc->chr_count - 1U);
                    }
                    consumed = true;
                } else if(event->key == InputKeyUp) {
                    const FuseRadioGattService* svc = &model->results.svcs[model->selected_service];
                    if(svc->chr_count > 0U) {
                        if(model->selected_characteristic > 0U) {
                            model->selected_characteristic--;
                        } else {
                            model->selected_characteristic = (uint8_t)(svc->chr_count - 1U);
                        }
                        consumed = true;
                    }
                } else if(event->key == InputKeyDown) {
                    const FuseRadioGattService* svc = &model->results.svcs[model->selected_service];
                    if(svc->chr_count > 0U) {
                        model->selected_characteristic =
                            (uint8_t)((model->selected_characteristic + 1U) % svc->chr_count);
                        consumed = true;
                    }
                }
            }
        },
        consumed);

    return consumed;
}

FuseRadioGattBrowserView* fuse_radio_gatt_browser_view_alloc(void) {
    FuseRadioGattBrowserView* instance = malloc(sizeof(FuseRadioGattBrowserView));

    instance->view = view_alloc();
    view_allocate_model(
        instance->view,
        ViewModelTypeLocking,
        sizeof(FuseRadioGattBrowserViewModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, fuse_radio_gatt_browser_view_draw_callback);
    view_set_input_callback(instance->view, fuse_radio_gatt_browser_view_input_callback);

    return instance;
}

void fuse_radio_gatt_browser_view_free(FuseRadioGattBrowserView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* fuse_radio_gatt_browser_view_get_view(FuseRadioGattBrowserView* instance) {
    furi_assert(instance);
    return instance->view;
}

void fuse_radio_gatt_browser_view_set_data(
    FuseRadioGattBrowserView* instance,
    const FuseRadioGattResults* results,
    uint8_t preferred_service_index) {
    furi_assert(instance);
    furi_assert(results);

    with_view_model(
        instance->view,
        FuseRadioGattBrowserViewModel * model,
        {
            model->results = *results;

            if(model->results.svc_count == 0U) {
                model->selected_service = 0U;
                model->selected_characteristic = 0U;
            } else {
                if(preferred_service_index < model->results.svc_count) {
                    model->selected_service = preferred_service_index;
                } else if(model->selected_service >= model->results.svc_count) {
                    model->selected_service = (uint8_t)(model->results.svc_count - 1U);
                }

                const FuseRadioGattService* svc = &model->results.svcs[model->selected_service];
                if(svc->chr_count == 0U) {
                    model->selected_characteristic = 0U;
                } else if(model->selected_characteristic >= svc->chr_count) {
                    model->selected_characteristic = (uint8_t)(svc->chr_count - 1U);
                }
            }
        },
        true);
}
