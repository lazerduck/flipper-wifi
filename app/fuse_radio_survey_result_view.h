#pragma once

#include "fuse_radio.h"

#include <gui/view.h>
#include <input/input.h>

typedef enum {
    FuseRadioSurveyResultViewActionRepeat,
} FuseRadioSurveyResultViewAction;

typedef void (*FuseRadioSurveyResultViewCallback)(
    FuseRadioSurveyResultViewAction action,
    void* context);

typedef struct FuseRadioSurveyResultView FuseRadioSurveyResultView;

FuseRadioSurveyResultView* fuse_radio_survey_result_view_alloc(void);
void fuse_radio_survey_result_view_free(FuseRadioSurveyResultView* instance);
View* fuse_radio_survey_result_view_get_view(FuseRadioSurveyResultView* instance);
void fuse_radio_survey_result_view_set_callback(
    FuseRadioSurveyResultView* instance,
    FuseRadioSurveyResultViewCallback callback,
    void* context);
void fuse_radio_survey_result_view_set_data(
    FuseRadioSurveyResultView* instance,
    const FuseRadioSurveyResults* results);