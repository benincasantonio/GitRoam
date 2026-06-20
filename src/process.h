#ifndef PROCESS_H
#define PROCESS_H

#include <stddef.h>

typedef struct {
    int exit_code;
    char *stdout_data;
    size_t stdout_length;
    char *stderr_data;
    size_t stderr_length;
} process_result;

int process_run(const char *const argv[], process_result *result);
void process_result_destroy(process_result *result);

#endif
