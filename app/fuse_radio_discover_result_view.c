#include "fuse_radio_discover_result_view.h"

#include <gui/canvas.h>
#include <gui/elements.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct FuseRadioDiscoverResultView {
    View* view;
    FuseRadioDiscoverResultViewCallback callback;
    void* context;
};

typedef struct {
    FuseRadioDiscoverResults results;
    uint8_t page_index;
} FuseRadioDiscoverResultViewModel;

static void fuse_radio_discover_result_view_draw_badge(
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

static void fuse_radio_discover_result_view_copy_fit(
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

    const size_t src_len = strlen(src);
    size_t copy_len = src_len;
    if(copy_len > max_visible_chars) {
        copy_len = max_visible_chars;
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

static const char* fuse_radio_discover_result_view_source_label(FuseRadioDiscoverNameSource source) {
    switch(source) {
    case FuseRadioDiscoverNameSourceMdns:
        return "mDNS";
    case FuseRadioDiscoverNameSourceReverseDns:
        return "DNS";
    case FuseRadioDiscoverNameSourceNone:
    default:
        return "IP";
    }
}

static const char* fuse_radio_discover_result_view_role_badge(const FuseRadioDiscoverHost* host) {
    if(host->is_gateway) {
        return "GW";
    }

    if(host->has_role) {
        return host->role;
    }

    if(host->has_name) {
        return host->name_source == FuseRadioDiscoverNameSourceMdns ? "mDNS" : "NAME";
    }

    if(host->has_vendor) {
        return "OUI";
    }

    if(host->has_mac) {
        return "ARP";
    }

    return "LIVE";
}

static void fuse_radio_discover_result_view_format_mac_tail(
    const FuseRadioDiscoverHost* host,
    char* buffer,
    size_t buffer_size) {
    if(buffer == NULL || buffer_size == 0U) {
        return;
    }

    buffer[0] = '\0';
    if(!host->has_mac) {
        return;
    }

    const size_t mac_len = strlen(host->mac);
    if(mac_len >= 8U) {
        snprintf(buffer, buffer_size, "MAC %s", host->mac + mac_len - 8U);
    } else {
        snprintf(buffer, buffer_size, "MAC %s", host->mac);
    }
}

static void fuse_radio_discover_result_view_format_latency(
    const FuseRadioDiscoverHost* host,
    char* buffer,
    size_t buffer_size) {
    const char* quality = "slow";

    if(buffer == NULL || buffer_size == 0U) {
        return;
    }

    if(host->rtt_ms <= 4U) {
        quality = "fast";
    } else if(host->rtt_ms <= 12U) {
        quality = "good";
    } else if(host->rtt_ms <= 35U) {
        quality = "ok";
    }

    snprintf(buffer, buffer_size, "%ums %s", (unsigned)host->rtt_ms, quality);
}

static void fuse_radio_discover_result_view_format_primary(
    const FuseRadioDiscoverHost* host,
    char* buffer,
    size_t buffer_size) {
    const char* primary = "Active host";

    if(buffer == NULL || buffer_size == 0U) {
        return;
    }

    if(host->is_gateway) {
        primary = "Gateway";
    } else if(host->has_name) {
        primary = host->name;
    } else if(host->has_role) {
        if(strcmp(host->role, "WEB") == 0) {
            primary = "Web service";
        } else if(strcmp(host->role, "SSH") == 0) {
            primary = "Remote shell";
        } else if(strcmp(host->role, "DNS") == 0) {
            primary = "Name server";
        } else if(strcmp(host->role, "NAS") == 0) {
            primary = "File share";
        } else if(strcmp(host->role, "CAM") == 0) {
            primary = "Camera stream";
        } else {
            primary = host->role;
        }
    } else if(host->has_vendor) {
        primary = host->vendor;
    } else if(host->has_mac) {
        primary = "ARP responder";
    }

    fuse_radio_discover_result_view_copy_fit(buffer, buffer_size, primary, 15U);
}

static void fuse_radio_discover_result_view_format_secondary(
    const FuseRadioDiscoverHost* host,
    char* buffer,
    size_t buffer_size) {
    char mac_tail[16];

    if(buffer == NULL || buffer_size == 0U) {
        return;
    }

    fuse_radio_discover_result_view_format_mac_tail(host, mac_tail, sizeof(mac_tail));

    if(host->is_gateway && host->has_vendor) {
        fuse_radio_discover_result_view_copy_fit(buffer, buffer_size, host->vendor, 12U);
    } else if(host->has_services) {
        fuse_radio_discover_result_view_copy_fit(buffer, buffer_size, host->services, 14U);
    } else if(host->has_name && host->has_vendor) {
        fuse_radio_discover_result_view_copy_fit(buffer, buffer_size, host->vendor, 12U);
    } else if(host->has_mac) {
        fuse_radio_discover_result_view_copy_fit(buffer, buffer_size, mac_tail, 12U);
    } else if(host->has_name) {
        snprintf(buffer, buffer_size, "%s name", fuse_radio_discover_result_view_source_label(host->name_source));
    } else {
        snprintf(buffer, buffer_size, "ICMP reply");
    }
}

static void fuse_radio_discover_result_view_draw_overview(
    Canvas* canvas,
    const FuseRadioDiscoverResults* results) {
    char line[24];

    canvas_draw_rframe(canvas, 4, 17, 120, 38, 2);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 8, 28, results->subnet[0] ? results->subnet : "Subnet unknown");
    canvas_set_font(canvas, FontSecondary);

    snprintf(line, sizeof(line), "Found %u", (unsigned)results->found_count);
    canvas_draw_str(canvas, 8, 39, line);
    snprintf(line, sizeof(line), "Scanned %u", (unsigned)results->scanned_count);
    canvas_draw_str(canvas, 70, 39, line);

    snprintf(line, sizeof(line), "%lums", (unsigned long)results->duration_ms);
    canvas_draw_str(canvas, 8, 50, line);
    if(results->truncated_count > 0U) {
        snprintf(line, sizeof(line), "+%u hidden", (unsigned)results->truncated_count);
        canvas_draw_str(canvas, 70, 50, line);
    } else if(results->gateway_ip[0]) {
        snprintf(line, sizeof(line), "GW %s", results->gateway_ip);
        canvas_draw_str(canvas, 56, 50, line);
    } else if(results->self_ip[0]) {
        snprintf(line, sizeof(line), "Self %s", results->self_ip);
        canvas_draw_str(canvas, 54, 50, line);
    }
}

static void fuse_radio_discover_result_view_draw_host(
    Canvas* canvas,
    const FuseRadioDiscoverHost* host) {
    char primary[20];
    char secondary[20];
    char latency[16];

    canvas_draw_frame(canvas, 3, 17, 122, 38);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 8, 28, host->ip);

    fuse_radio_discover_result_view_draw_badge(
        canvas, 98, 19, fuse_radio_discover_result_view_role_badge(host));

    fuse_radio_discover_result_view_format_primary(host, primary, sizeof(primary));
    fuse_radio_discover_result_view_format_secondary(host, secondary, sizeof(secondary));
    fuse_radio_discover_result_view_format_latency(host, latency, sizeof(latency));

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 8, 39, primary);
    canvas_draw_str(canvas, 8, 50, secondary);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 82, 50, latency);
}

static void fuse_radio_discover_result_view_draw_callback(Canvas* canvas, void* model) {
    FuseRadioDiscoverResultViewModel* view_model = model;
    char header[22];
    uint8_t page_count = (uint8_t)(view_model->results.count + 1U);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    if(view_model->results.has_error) {
        canvas_draw_str(canvas, 4, 10, "Discover error");
        canvas_draw_line(canvas, 0, 13, 127, 13);
        elements_multiline_text_aligned(
            canvas, 64, 32, AlignCenter, AlignCenter, view_model->results.error);
        elements_button_center(canvas, "Retry");
        return;
    }

    snprintf(
        header,
        sizeof(header),
        "Hosts %u/%u",
        (unsigned)(view_model->page_index + 1U),
        (unsigned)page_count);
    canvas_draw_str(canvas, 4, 10, header);
    canvas_draw_line(canvas, 0, 13, 127, 13);

    if(view_model->results.count == 0U) {
        elements_multiline_text_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "No active hosts found");
        elements_button_center(canvas, "Retry");
        return;
    }

    if(view_model->page_index == 0U) {
        fuse_radio_discover_result_view_draw_overview(canvas, &view_model->results);
    } else {
        fuse_radio_discover_result_view_draw_host(
            canvas, &view_model->results.hosts[view_model->page_index - 1U]);
    }

    elements_button_center(canvas, "Retry");
}

static bool fuse_radio_discover_result_view_input_callback(InputEvent* event, void* context) {
    FuseRadioDiscoverResultView* instance = context;
    bool consumed = false;

    if(event->type != InputTypeShort) {
        return false;
    }

    with_view_model(
        instance->view,
        FuseRadioDiscoverResultViewModel * model,
        {
            const uint8_t page_count =
                model->results.has_error ? 1U : (uint8_t)(model->results.count + 1U);

            if(event->key == InputKeyUp && page_count > 0U) {
                if(model->page_index > 0U) {
                    model->page_index--;
                } else {
                    model->page_index = page_count - 1U;
                }
                consumed = true;
            } else if(event->key == InputKeyDown && page_count > 0U) {
                model->page_index = (uint8_t)((model->page_index + 1U) % page_count);
                consumed = true;
            } else if(event->key == InputKeyOk) {
                consumed = true;
            }
        },
        consumed);

    if(consumed && event->key == InputKeyOk && instance->callback) {
        instance->callback(FuseRadioDiscoverResultViewActionRepeat, instance->context);
    }

    return consumed;
}

FuseRadioDiscoverResultView* fuse_radio_discover_result_view_alloc(void) {
    FuseRadioDiscoverResultView* instance = malloc(sizeof(FuseRadioDiscoverResultView));

    instance->view = view_alloc();
    view_allocate_model(
        instance->view,
        ViewModelTypeLocking,
        sizeof(FuseRadioDiscoverResultViewModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, fuse_radio_discover_result_view_draw_callback);
    view_set_input_callback(instance->view, fuse_radio_discover_result_view_input_callback);

    return instance;
}

void fuse_radio_discover_result_view_free(FuseRadioDiscoverResultView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* fuse_radio_discover_result_view_get_view(FuseRadioDiscoverResultView* instance) {
    furi_assert(instance);
    return instance->view;
}

void fuse_radio_discover_result_view_set_callback(
    FuseRadioDiscoverResultView* instance,
    FuseRadioDiscoverResultViewCallback callback,
    void* context) {
    furi_assert(instance);
    instance->callback = callback;
    instance->context = context;
}

void fuse_radio_discover_result_view_set_data(
    FuseRadioDiscoverResultView* instance,
    const FuseRadioDiscoverResults* results) {
    furi_assert(instance);
    furi_assert(results);

    with_view_model(
        instance->view,
        FuseRadioDiscoverResultViewModel * model,
        {
            model->results = *results;
            if(model->results.has_error || model->results.count == 0U) {
                model->page_index = 0U;
            } else if(model->page_index > model->results.count) {
                model->page_index = model->results.count;
            }
        },
        true);
}