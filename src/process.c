#include "process.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} byte_buffer;

static bool buffer_append(byte_buffer *buffer, const char *data, size_t length)
{
    char *expanded;
    size_t capacity;

    if (buffer->length + length + 1 > buffer->capacity) {
        capacity = buffer->capacity == 0 ? 4096 : buffer->capacity;
        while (capacity < buffer->length + length + 1) {
            capacity *= 2;
        }
        expanded = realloc(buffer->data, capacity);
        if (expanded == NULL) {
            return false;
        }
        buffer->data = expanded;
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->length, data, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return true;
}

static void close_pipe(int descriptors[2])
{
    if (descriptors[0] >= 0) {
        (void)close(descriptors[0]);
    }
    if (descriptors[1] >= 0) {
        (void)close(descriptors[1]);
    }
}

int process_run(const char *const argv[], process_result *result)
{
    int output_pipe[2] = { -1, -1 };
    int error_pipe[2] = { -1, -1 };
    pid_t child;
    byte_buffer output = { 0 };
    byte_buffer error = { 0 };
    struct pollfd descriptors[2];
    int open_descriptors = 2;
    int status = 0;
    bool allocation_failed = false;
    int saved_error = 0;

    if (argv == NULL || argv[0] == NULL || result == NULL) {
        errno = EINVAL;
        return -1;
    }
    memset(result, 0, sizeof(*result));
    if (pipe(output_pipe) != 0 || pipe(error_pipe) != 0) {
        close_pipe(output_pipe);
        close_pipe(error_pipe);
        return -1;
    }
    child = fork();
    if (child < 0) {
        close_pipe(output_pipe);
        close_pipe(error_pipe);
        return -1;
    }
    if (child == 0) {
        (void)close(output_pipe[0]);
        (void)close(error_pipe[0]);
        if (dup2(output_pipe[1], STDOUT_FILENO) < 0 ||
            dup2(error_pipe[1], STDERR_FILENO) < 0) {
            _exit(126);
        }
        (void)close(output_pipe[1]);
        (void)close(error_pipe[1]);
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    (void)close(output_pipe[1]);
    output_pipe[1] = -1;
    (void)close(error_pipe[1]);
    error_pipe[1] = -1;
    descriptors[0].fd = output_pipe[0];
    descriptors[0].events = POLLIN | POLLHUP;
    descriptors[1].fd = error_pipe[0];
    descriptors[1].events = POLLIN | POLLHUP;
    while (open_descriptors > 0) {
        int poll_result = poll(descriptors, 2, -1);
        size_t index;

        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            saved_error = errno;
            (void)kill(child, SIGTERM);
            break;
        }
        for (index = 0; index < 2; index++) {
            char chunk[4096];
            ssize_t count;
            byte_buffer *buffer = index == 0 ? &output : &error;

            if (descriptors[index].fd < 0 ||
                (descriptors[index].revents &
                 (POLLIN | POLLHUP | POLLERR)) == 0) {
                continue;
            }
            count = read(descriptors[index].fd, chunk, sizeof(chunk));
            if (count > 0) {
                if (!buffer_append(buffer, chunk, (size_t)count)) {
                    errno = ENOMEM;
                    (void)kill(child, SIGTERM);
                    allocation_failed = true;
                    break;
                }
            } else if (count == 0 || (count < 0 && errno != EINTR)) {
                (void)close(descriptors[index].fd);
                descriptors[index].fd = -1;
                open_descriptors--;
            }
        }
        if (allocation_failed || saved_error != 0) {
            break;
        }
    }
    {
        size_t index;
        for (index = 0; index < 2; index++) {
            if (descriptors[index].fd >= 0) {
                (void)close(descriptors[index].fd);
                descriptors[index].fd = -1;
            }
        }
    }
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    if (allocation_failed || saved_error != 0) {
        free(output.data);
        free(error.data);
        errno = allocation_failed ? ENOMEM : saved_error;
        return -1;
    }
    if (output.data == NULL) {
        output.data = calloc(1, 1);
    }
    if (error.data == NULL) {
        error.data = calloc(1, 1);
    }
    if (output.data == NULL || error.data == NULL) {
        free(output.data);
        free(error.data);
        errno = ENOMEM;
        return -1;
    }
    result->stdout_data = output.data;
    result->stdout_length = output.length;
    result->stderr_data = error.data;
    result->stderr_length = error.length;
    if (WIFEXITED(status)) {
        result->exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result->exit_code = 128 + WTERMSIG(status);
    } else {
        result->exit_code = 1;
    }
    return 0;
}

void process_result_destroy(process_result *result)
{
    if (result == NULL) {
        return;
    }
    free(result->stdout_data);
    free(result->stderr_data);
    memset(result, 0, sizeof(*result));
}
