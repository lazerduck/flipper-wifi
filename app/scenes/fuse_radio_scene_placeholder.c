#include "../fuse_radio_app_i.h"

void fuse_radio_scene_placeholder_on_enter(void* context) {
    FuseRadioApp* app = context;

    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 8, AlignCenter, AlignTop, FontPrimary, app->placeholder_title);
    widget_add_string_multiline_element(
        app->widget, 64, 30, AlignCenter, AlignCenter, FontSecondary, app->placeholder_message);

    view_dispatcher_switch_to_view(app->view_dispatcher, FuseRadioViewWidget);
}

bool fuse_radio_scene_placeholder_on_event(void* context, SceneManagerEvent event) {
    FuseRadioApp* app = context;

    if(event.type == SceneManagerEventTypeBack) {
        return scene_manager_previous_scene(app->scene_manager);
    }

    UNUSED(app);
    return false;
}

void fuse_radio_scene_placeholder_on_exit(void* context) {
    FuseRadioApp* app = context;
    widget_reset(app->widget);
}
