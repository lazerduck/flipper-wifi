#pragma once

#include <stddef.h>

typedef void (*command_response_writer_t)(const char *response);

typedef struct {
    command_response_writer_t write_response;
} command_context_t;

static inline void command_context_write_line(const command_context_t *context, const char *line)
{
    if (context == NULL || context->write_response == NULL || line == NULL) {
        return;
    }

    context->write_response(line);
}