
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

#include "forkpty.h"
#include "flutter_pty.h"

#include "include/dart_api.h"
#include "include/dart_api_dl.h"
#include "include/dart_native_api.h"

typedef struct PtyHandle
{
    int ptm;

    int pid;

} PtyHandle;

typedef struct ReadLoopOptions
{
    int fd;

    Dart_Port port;

} ReadLoopOptions;

static void *read_loop(void *arg)
{
    ReadLoopOptions *options = (ReadLoopOptions *)arg;

    char buffer[1024];

    while (1)
    {
        ssize_t n = read(options->fd, buffer, sizeof(buffer));

        if (n < 0)
        {
            // TODO: handle error
            break;
        }

        if (n == 0)
        {
            break;
        }

        Dart_CObject result;
        result.type = Dart_CObject_kTypedData;
        result.value.as_typed_data.type = Dart_TypedData_kUint8;
        result.value.as_typed_data.length = n;
        result.value.as_typed_data.values = (uint8_t *)buffer;

        Dart_PostCObject_DL(options->port, &result);
    }

    return NULL;
}

static void start_read_thread(int fd, Dart_Port port)
{
    ReadLoopOptions *options = malloc(sizeof(ReadLoopOptions));

    options->fd = fd;

    options->port = port;

    pthread_t _thread;

    pthread_create(&_thread, NULL, &read_loop, options);
}

typedef struct WaitExitOptions
{
    int pid;

    Dart_Port port;

} WaitExitOptions;

static void *wait_exit_thread(void *arg)
{
    WaitExitOptions *options = (WaitExitOptions *)arg;

    int status;

    waitpid(options->pid, &status, 0);

    if (WIFEXITED(status))
    {
        Dart_PostInteger_DL(options->port, WEXITSTATUS(status));
    }
    else if (WIFSIGNALED(status))
    {
        Dart_PostInteger_DL(options->port, -WTERMSIG(status));
    }

    return NULL;
}

static void start_wait_exit_thread(int pid, Dart_Port port)
{
    WaitExitOptions *options = malloc(sizeof(WaitExitOptions));

    options->pid = pid;

    options->port = port;

    pthread_t _thread;

    pthread_create(&_thread, NULL, &wait_exit_thread, options);
}

static void set_environment(char **environment)
{
    if (environment == NULL)
    {
        return;
    }

    while (*environment != NULL)
    {
        putenv(*environment);
        environment++;
    }
}

FFI_PLUGIN_EXPORT PtyHandle *pty_create(PtyOptions *options)
{
    struct winsize ws;

    ws.ws_row = options->rows;
    ws.ws_col = options->cols;

    int ptm;

    int pid = pty_forkpty(&ptm, NULL, NULL, &ws);

    if (pid < 0)
    {
        // TODO: handle error
        return NULL;
    }

    if (pid == 0)
    {
        set_environment(options->environment);

        int ok = execvp(options->executable, options->arguments);

        if (ok < 0)
        {
            perror("execvp");
        }
    }

    setsid();

    PtyHandle *handle = (PtyHandle *)malloc(sizeof(PtyHandle));

    handle->ptm = ptm;
    handle->pid = pid;

    start_read_thread(ptm, options->stdout_port);

    start_wait_exit_thread(pid, options->exit_port);

    return handle;
}

FFI_PLUGIN_EXPORT void pty_write(PtyHandle *handle, char *buffer, int length)
{
    write(handle->ptm, buffer, length);
}

FFI_PLUGIN_EXPORT int pty_resize(PtyHandle *handle, int rows, int cols)
{
    struct winsize ws;

    ws.ws_row = rows;
    ws.ws_col = cols;

    return ioctl(handle->ptm, TIOCSWINSZ, &ws);
}

FFI_PLUGIN_EXPORT int pty_getpid(PtyHandle *handle)
{
    return handle->pid;
}

FFI_PLUGIN_EXPORT char *pty_error()
{
    return NULL;
}
