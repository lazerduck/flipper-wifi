#include "fuse_radio_scene.h"

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const fuse_radio_scene_on_enter_handlers[])(void*) = {
#include "fuse_radio_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const fuse_radio_scene_on_event_handlers[])(void* context, SceneManagerEvent event) = {
#include "fuse_radio_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const fuse_radio_scene_on_exit_handlers[])(void* context) = {
#include "fuse_radio_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers fuse_radio_scene_handlers = {
    .on_enter_handlers = fuse_radio_scene_on_enter_handlers,
    .on_event_handlers = fuse_radio_scene_on_event_handlers,
    .on_exit_handlers = fuse_radio_scene_on_exit_handlers,
    .scene_num = FuseRadioSceneNum,
};