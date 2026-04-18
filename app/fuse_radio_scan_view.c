#include "fuse_radio_scan_view.h"

#include <gui/canvas.h>
#include <gui/elements.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct FuseRadioScanView {
    View* view;
    FuseRadioScanViewCallback callback;
    void* context;
};

typedef struct {
    FuseRadioScanResults results;
    FuseRadioWifiState wifi_state;
    uint8_t selected_index;
    uint8_t channel_list_offset;
    bool show_channel_summary;
    FuseRadioScanViewMode mode;
} FuseRadioScanViewModel;

typedef struct {
    uint8_t channel;
    uint8_t count;
} FuseRadioChannelSummary;

#define FUSE_RADIO_CHANNEL_LINES      4U
#define FUSE_RADIO_SSID_VISIBLE_CHARS 12U

static const char*
    fuse_radio_scan_view_display_ssid(const FuseRadioAccessPoint* ap, char* buffer) {
    if(!ap->ssid[0]) {
        return "<hidden>";
    }

    const size_t ssid_length = strlen(ap->ssid);
    if(ssid_length <= FUSE_RADIO_SSID_VISIBLE_CHARS) {
        return ap->ssid;
    }

    memcpy(buffer, ap->ssid, FUSE_RADIO_SSID_VISIBLE_CHARS - 1U);
    memcpy(&buffer[FUSE_RADIO_SSID_VISIBLE_CHARS - 1U], "..", 3U);
    return buffer;
}

static uint8_t fuse_radio_scan_view_collect_channels(
    const FuseRadioScanResults* results,
    FuseRadioChannelSummary* channels,
    uint8_t capacity) {
    uint8_t used = 0;

    for(uint8_t ap_index = 0; ap_index < results->count; ap_index++) {
        const uint8_t channel = results->aps[ap_index].channel;
        bool found = false;

        for(uint8_t channel_index = 0; channel_index < used; channel_index++) {
            if(channels[channel_index].channel == channel) {
                channels[channel_index].count++;
                found = true;
                break;
            }
        }

        if(found || used >= capacity) {
            continue;
        }

        channels[used].channel = channel;
        channels[used].count = 1U;
        used++;
    }

    for(uint8_t left = 0; left < used; left++) {
        for(uint8_t right = left + 1U; right < used; right++) {
            if(channels[right].channel < channels[left].channel) {
                const FuseRadioChannelSummary temp = channels[left];
                channels[left] = channels[right];
                channels[right] = temp;
            }
        }
    }

    return used;
}

static uint8_t fuse_radio_scan_view_signal_bars(int16_t rssi) {
    if(rssi >= -55) return 4;
    if(rssi >= -67) return 3;
    if(rssi >= -78) return 2;
    if(rssi >= -90) return 1;
    return 0;
}

static void fuse_radio_scan_view_draw_signal(Canvas* canvas, int16_t rssi) {
    const uint8_t bars = fuse_radio_scan_view_signal_bars(rssi);
    const uint8_t base_x = 102;
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

static void fuse_radio_scan_view_draw_callback(Canvas* canvas, void* model) {
    FuseRadioScanViewModel* view_model = model;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 4, 10, "WiFi Scan");

    canvas_set_font(canvas, FontSecondary);
    if(view_model->wifi_state == FuseRadioWifiStateScanning) {
        canvas_draw_str(canvas, 80, 10, "Scanning");
    } else if(view_model->wifi_state == FuseRadioWifiStateScanComplete) {
        canvas_draw_str(canvas, 76, 10, "Results");
    } else if(view_model->wifi_state == FuseRadioWifiStateError) {
        canvas_draw_str(canvas, 88, 10, "Error");
    }

    canvas_draw_line(canvas, 0, 13, 127, 13);

    if(view_model->results.has_error) {
        elements_multiline_text_aligned(
            canvas, 64, 30, AlignCenter, AlignCenter, view_model->results.error);
        elements_button_center(canvas, "Retry");
        return;
    }

    if(view_model->wifi_state == FuseRadioWifiStateScanning && view_model->results.count == 0U) {
        elements_multiline_text_aligned(
            canvas, 64, 30, AlignCenter, AlignCenter, "Listening for APs\nfrom the ESP");
        return;
    }

    if(view_model->results.count == 0U) {
        elements_multiline_text_aligned(
            canvas, 64, 28, AlignCenter, AlignCenter, "No access points\nfound yet");
        elements_button_center(canvas, "Rescan");
        return;
    }

    if(view_model->show_channel_summary && view_model->mode == FuseRadioScanViewModeBrowse) {
        FuseRadioChannelSummary channels[FUSE_RADIO_MAX_AP_COUNT] = {0};
        const uint8_t channel_count = fuse_radio_scan_view_collect_channels(
            &view_model->results, channels, COUNT_OF(channels));

        canvas_draw_frame(canvas, 3, 17, 122, 38);
        canvas_set_font(canvas, FontSecondary);

        for(uint8_t row = 0; row < FUSE_RADIO_CHANNEL_LINES; row++) {
            const uint8_t channel_index = view_model->channel_list_offset + row;
            if(channel_index >= channel_count) {
                break;
            }

            const FuseRadioChannelSummary* channel = &channels[channel_index];
            char line[32];
            const uint8_t hashes = channel->count > 8U ? 8U : channel->count;
            int written = snprintf(line, sizeof(line), "CH%u: ", (unsigned)channel->channel);
            if(written < 0) {
                continue;
            }

            size_t position = (size_t)written;
            for(uint8_t hash = 0; hash < hashes && position + 1U < sizeof(line); hash++) {
                line[position++] = '#';
            }

            if(position + 6U < sizeof(line)) {
                snprintf(
                    &line[position], sizeof(line) - position, " (%u)", (unsigned)channel->count);
            } else {
                line[position] = '\0';
            }

            canvas_draw_str(canvas, 8, 27 + row * 9, line);
        }

        if(channel_count > FUSE_RADIO_CHANNEL_LINES) {
            char range[18];
            const uint8_t visible_end =
                MIN((uint8_t)(view_model->channel_list_offset + FUSE_RADIO_CHANNEL_LINES),
                    channel_count);
            snprintf(
                range,
                sizeof(range),
                "%u-%u/%u",
                (unsigned)(view_model->channel_list_offset + 1U),
                (unsigned)visible_end,
                (unsigned)channel_count);
            canvas_draw_str(canvas, 86, 28, range);
        }

        elements_button_center(canvas, "Rescan");
        return;
    }

    const FuseRadioAccessPoint* ap = &view_model->results.aps[view_model->selected_index];
    char ssid[FUSE_RADIO_MAX_SSID_LENGTH + 1U] = {0};

    canvas_draw_frame(canvas, 3, 17, 122, 38);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 8, 28, fuse_radio_scan_view_display_ssid(ap, ssid));

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 8, 39, ap->auth);

    char channel[12];
    snprintf(channel, sizeof(channel), "CH %u", ap->channel);
    canvas_draw_str(canvas, 8, 50, channel);

    char rssi[14];
    snprintf(rssi, sizeof(rssi), "%d dBm", ap->rssi);
    canvas_draw_str(canvas, 66, 50, rssi);
    fuse_radio_scan_view_draw_signal(canvas, ap->rssi);

    char counter[18];
    snprintf(
        counter,
        sizeof(counter),
        "%u/%u",
        (unsigned)(view_model->selected_index + 1U),
        (unsigned)view_model->results.count);
    canvas_draw_str(canvas, 90, 39, counter);

    elements_button_center(
        canvas, view_model->mode == FuseRadioScanViewModeSelect ? "Select" : "Rescan");
}

static bool fuse_radio_scan_view_input_callback(InputEvent* event, void* context) {
    FuseRadioScanView* instance = context;
    bool consumed = false;

    if(event->type != InputTypeShort) {
        return false;
    }

    with_view_model(
        instance->view,
        FuseRadioScanViewModel * model,
        {
            if((event->key == InputKeyLeft || event->key == InputKeyRight) &&
               model->mode == FuseRadioScanViewModeBrowse) {
                model->show_channel_summary = !model->show_channel_summary;
                consumed = true;
            } else if(event->key == InputKeyUp) {
                if(model->show_channel_summary) {
                    if(model->channel_list_offset > 0U) {
                        model->channel_list_offset--;
                        consumed = true;
                    }
                } else if(model->selected_index > 0U) {
                    model->selected_index--;
                    consumed = true;
                }
            } else if(event->key == InputKeyDown) {
                if(model->show_channel_summary) {
                    FuseRadioChannelSummary channels[FUSE_RADIO_MAX_AP_COUNT] = {0};
                    const uint8_t channel_count = fuse_radio_scan_view_collect_channels(
                        &model->results, channels, COUNT_OF(channels));
                    if((model->channel_list_offset + FUSE_RADIO_CHANNEL_LINES) < channel_count) {
                        model->channel_list_offset++;
                        consumed = true;
                    }
                } else if((model->selected_index + 1U) < model->results.count) {
                    model->selected_index++;
                    consumed = true;
                }
            } else if(event->key == InputKeyOk) {
                consumed = true;
            }
        },
        consumed);

    if(consumed && event->key == InputKeyOk && instance->callback) {
        FuseRadioScanViewAction action = FuseRadioScanViewActionRescan;

        with_view_model(
            instance->view,
            FuseRadioScanViewModel * model,
            {
                if(model->mode == FuseRadioScanViewModeSelect && model->results.count > 0U) {
                    action = FuseRadioScanViewActionSelect;
                }
            },
            false);

        instance->callback(action, instance->context);
    }

    return consumed;
}

FuseRadioScanView* fuse_radio_scan_view_alloc(void) {
    FuseRadioScanView* instance = malloc(sizeof(FuseRadioScanView));

    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(FuseRadioScanViewModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, fuse_radio_scan_view_draw_callback);
    view_set_input_callback(instance->view, fuse_radio_scan_view_input_callback);

    return instance;
}

void fuse_radio_scan_view_free(FuseRadioScanView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* fuse_radio_scan_view_get_view(FuseRadioScanView* instance) {
    furi_assert(instance);
    return instance->view;
}

void fuse_radio_scan_view_set_callback(
    FuseRadioScanView* instance,
    FuseRadioScanViewCallback callback,
    void* context) {
    furi_assert(instance);
    instance->callback = callback;
    instance->context = context;
}

void fuse_radio_scan_view_set_mode(FuseRadioScanView* instance, FuseRadioScanViewMode mode) {
    furi_assert(instance);

    with_view_model(
        instance->view,
        FuseRadioScanViewModel * model,
        {
            model->mode = mode;
            if(mode == FuseRadioScanViewModeSelect) {
                model->show_channel_summary = false;
            }
        },
        true);
}

uint8_t fuse_radio_scan_view_get_selected_index(FuseRadioScanView* instance) {
    furi_assert(instance);

    uint8_t selected_index = 0U;
    with_view_model(
        instance->view,
        FuseRadioScanViewModel * model,
        { selected_index = model->selected_index; },
        false);
    return selected_index;
}

void fuse_radio_scan_view_set_data(
    FuseRadioScanView* instance,
    const FuseRadioScanResults* results,
    FuseRadioWifiState wifi_state) {
    furi_assert(instance);
    furi_assert(results);

    with_view_model(
        instance->view,
        FuseRadioScanViewModel * model,
        {
            const uint8_t previous = model->selected_index;
            FuseRadioChannelSummary channels[FUSE_RADIO_MAX_AP_COUNT] = {0};
            const uint8_t channel_count =
                fuse_radio_scan_view_collect_channels(results, channels, COUNT_OF(channels));
            model->results = *results;
            model->wifi_state = wifi_state;
            if(model->results.count == 0U) {
                model->selected_index = 0U;
            } else if(previous < model->results.count) {
                model->selected_index = previous;
            } else {
                model->selected_index = model->results.count - 1U;
            }

            if(channel_count <= FUSE_RADIO_CHANNEL_LINES) {
                model->channel_list_offset = 0U;
            } else if(model->channel_list_offset + FUSE_RADIO_CHANNEL_LINES > channel_count) {
                model->channel_list_offset = channel_count - FUSE_RADIO_CHANNEL_LINES;
            }
        },
        true);
}
