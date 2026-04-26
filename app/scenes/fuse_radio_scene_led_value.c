#include "../fuse_radio_app_i.h"

static const char* fuse_radio_scene_led_value_title(FuseRadioLedChannel channel) {
    switch(channel) {
    case FuseRadioLedChannelRed:
        return "LED Red";
    case FuseRadioLedChannelGreen:
        return "LED Green";
    case FuseRadioLedChannelBlue:
    default:
        return "LED Blue";
    }
}

static uint8_t fuse_radio_scene_led_value_current(const FuseRadioApp* app) {
    switch(app->led_edit_channel) {
    case FuseRadioLedChannelRed:
        return app->led_red;
    case FuseRadioLedChannelGreen:
        return app->led_green;
    case FuseRadioLedChannelBlue:
    default:
        return app->led_blue;
    }
}

void fuse_radio_scene_led_value_on_enter(void* context) {
    FuseRadioApp* app = context;

    fuse_radio_value_picker_view_set_title(
        app->value_picker_view, fuse_radio_scene_led_value_title(app->led_edit_channel));
    fuse_radio_value_picker_view_set_value(
        app->value_picker_view, fuse_radio_scene_led_value_current(app));
    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewValuePicker);
}

bool fuse_radio_scene_led_value_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeCustom && event.event == FuseRadioCustomEventLedValueSet) {
        if(fuse_radio_app_set_led_manual(app)) {
            return scene_manager_previous_scene(app->scene_manager);
        }

        return true;
    }

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    return false;
}

void fuse_radio_scene_led_value_on_exit(void* context) {
    UNUSED(context);
}