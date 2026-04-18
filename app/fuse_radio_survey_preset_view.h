#pragma once

#include "fuse_radio.h"

#include <gui/view.h>
#include <input/input.h>

typedef struct FuseRadioSurveyPresetView FuseRadioSurveyPresetView;

typedef void (*FuseRadioSurveyPresetViewCallback)(FuseRadioSurveyPreset preset, void* context);

FuseRadioSurveyPresetView* fuse_radio_survey_preset_view_alloc(void);
void fuse_radio_survey_preset_view_free(FuseRadioSurveyPresetView* instance);
View* fuse_radio_survey_preset_view_get_view(FuseRadioSurveyPresetView* instance);
void fuse_radio_survey_preset_view_set_callback(
    FuseRadioSurveyPresetView* instance,
    FuseRadioSurveyPresetViewCallback callback,
    void* context);
void fuse_radio_survey_preset_view_set_preset(
    FuseRadioSurveyPresetView* instance,
    FuseRadioSurveyPreset preset);
FuseRadioSurveyPreset fuse_radio_survey_preset_view_get_preset(FuseRadioSurveyPresetView* instance);