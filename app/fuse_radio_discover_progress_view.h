#pragma once

#include <gui/view.h>

typedef struct FuseRadioDiscoverProgressView FuseRadioDiscoverProgressView;

typedef struct {
    uint16_t scanned_count;
    uint16_t total_count;
    uint16_t found_count;
    uint8_t progress_percent;
    uint8_t animation_frame;
    char subnet[24];
    char current_ip[16];
} FuseRadioDiscoverProgressSnapshot;

FuseRadioDiscoverProgressView* fuse_radio_discover_progress_view_alloc(void);
void fuse_radio_discover_progress_view_free(FuseRadioDiscoverProgressView* instance);
View* fuse_radio_discover_progress_view_get_view(FuseRadioDiscoverProgressView* instance);
void fuse_radio_discover_progress_view_set_snapshot(
    FuseRadioDiscoverProgressView* instance,
    const FuseRadioDiscoverProgressSnapshot* snapshot);
void fuse_radio_discover_progress_view_advance_animation(FuseRadioDiscoverProgressView* instance);