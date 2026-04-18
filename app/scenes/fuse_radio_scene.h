#pragma once

#include <gui/scene_manager.h>

#define ADD_SCENE(prefix, name, id) FuseRadioScene##id,
typedef enum {
#include "fuse_radio_scene_config.h"
    FuseRadioSceneNum,
} FuseRadioScene;
#undef ADD_SCENE

extern const SceneManagerHandlers fuse_radio_scene_handlers;

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_enter(void* context);
#include "fuse_radio_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) \
    bool prefix##_scene_##name##_on_event(void* context, SceneManagerEvent event);
#include "fuse_radio_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_exit(void* context);
#include "fuse_radio_scene_config.h"
#undef ADD_SCENE