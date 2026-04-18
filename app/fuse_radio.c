#include "fuse_radio.h"

int32_t fuse_radio_app(void* p) {
    (void)p;

    FuseRadioApp* app = fuse_radio_app_alloc();
    fuse_radio_app_run(app);
    fuse_radio_app_free(app);

    return 0;
}
