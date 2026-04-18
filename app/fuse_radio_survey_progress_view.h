#pragma once

#include <gui/view.h>

typedef struct FuseRadioSurveyProgressView FuseRadioSurveyProgressView;

typedef struct {
    uint8_t current_channel;
    uint8_t completed_count;
    uint8_t total_count;
    uint8_t progress_percent;
    uint8_t animation_frame;
} FuseRadioSurveyProgressSnapshot;

FuseRadioSurveyProgressView* fuse_radio_survey_progress_view_alloc(void);
void fuse_radio_survey_progress_view_free(FuseRadioSurveyProgressView* instance);
View* fuse_radio_survey_progress_view_get_view(FuseRadioSurveyProgressView* instance);
void fuse_radio_survey_progress_view_set_snapshot(
    FuseRadioSurveyProgressView* instance,
    const FuseRadioSurveyProgressSnapshot* snapshot);
void fuse_radio_survey_progress_view_advance_animation(FuseRadioSurveyProgressView* instance);