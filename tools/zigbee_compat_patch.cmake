set(project_dir "${PROJECT_DIR}")
if(project_dir STREQUAL "")
    message(FATAL_ERROR "PROJECT_DIR was not provided to zigbee_compat_patch.cmake")
endif()

set(utils_dir "${project_dir}/managed_components/espressif__esp-zigbee-lib/src/utils")
if(NOT EXISTS "${utils_dir}")
    message(STATUS "zigbee_compat_patch: managed Zigbee component not present yet, skipping")
    return()
endif()

set(esp_mbedtls_file "${utils_dir}/esp_mbedtls.c")
if(EXISTS "${esp_mbedtls_file}")
    file(READ "${esp_mbedtls_file}" esp_mbedtls_content)
    if(NOT esp_mbedtls_content MATCHES "__has_include\\(\"mbedtls/aes.h\"\\)")
        set(old_include "#include \"mbedtls/aes.h\"")
        set(new_include [=[#if defined(__has_include)
#if __has_include("mbedtls/aes.h")
#include "mbedtls/aes.h"
#define ZB_HAVE_MBEDTLS_AES_H 1
#else
#define ZB_HAVE_MBEDTLS_AES_H 0
#endif
#else
#include "mbedtls/aes.h"
#define ZB_HAVE_MBEDTLS_AES_H 1
#endif]=])

        string(REPLACE "${old_include}" "${new_include}" esp_mbedtls_content "${esp_mbedtls_content}")
        if(NOT esp_mbedtls_content MATCHES "ZB_HAVE_MBEDTLS_AES_H")
            message(FATAL_ERROR "zigbee_compat_patch: failed to patch esp_mbedtls.c include guard")
        endif()

        string(REPLACE "#if CONFIG_MBEDTLS_HARDWARE_AES" "#if CONFIG_MBEDTLS_HARDWARE_AES && ZB_HAVE_MBEDTLS_AES_H" esp_mbedtls_content "${esp_mbedtls_content}")
        file(WRITE "${esp_mbedtls_file}" "${esp_mbedtls_content}")
        message(STATUS "zigbee_compat_patch: patched esp_mbedtls.c")
    endif()
endif()

set(entropy_compat_file "${utils_dir}/mbedtls_entropy_compat.c")
set(entropy_compat_content [=[/*
 * Compatibility shims for ESP-IDF mbedTLS v4 where legacy entropy APIs
 * referenced by prebuilt Zigbee libraries are not exported.
 */

#include <stddef.h>

#include "esp_random.h"

#if defined(MBEDTLS_MAJOR_VERSION) && (MBEDTLS_MAJOR_VERSION >= 4)

typedef struct mbedtls_entropy_context mbedtls_entropy_context;
typedef int (*mbedtls_entropy_f_source_ptr)(
    void* data,
    unsigned char* output,
    size_t len,
    size_t* olen);

void mbedtls_entropy_init(mbedtls_entropy_context* ctx) {
    (void)ctx;
}

int mbedtls_entropy_add_source(
    mbedtls_entropy_context* ctx,
    mbedtls_entropy_f_source_ptr f_source,
    void* p_source,
    size_t threshold,
    int strong) {
    (void)ctx;
    (void)f_source;
    (void)p_source;
    (void)threshold;
    (void)strong;
    return 0;
}

int mbedtls_entropy_func(void* data, unsigned char* output, size_t len) {
    (void)data;
    if(output == NULL) {
        return -1;
    }

    esp_fill_random(output, len);
    return 0;
}

void mbedtls_entropy_free(mbedtls_entropy_context* ctx) {
    (void)ctx;
}

#endif
]=])

if(EXISTS "${entropy_compat_file}")
    file(READ "${entropy_compat_file}" current_entropy_compat_content)
else()
    set(current_entropy_compat_content "")
endif()

if(NOT current_entropy_compat_content STREQUAL entropy_compat_content)
    file(WRITE "${entropy_compat_file}" "${entropy_compat_content}")
    message(STATUS "zigbee_compat_patch: wrote mbedtls_entropy_compat.c")
endif()
