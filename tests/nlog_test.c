#define _POSIX_C_SOURCE 200809L
#define NLOG_TAG "nlog_test"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ncore/nlog.h"

int main(void)
{
    char output[4096];
    FILE *capture;
    int saved_stderr;
    size_t length;

    capture = tmpfile();
    assert(capture != NULL);

    saved_stderr = dup(STDERR_FILENO);
    assert(saved_stderr >= 0);
    assert(dup2(fileno(capture), STDERR_FILENO) >= 0);

    nlog_set_level(NLOG_LEVEL_INFO);
    NLOG_D("event=filtered");
    NLOG_I("event=nlog_test value=%d", 1);
    fflush(stderr);

    assert(dup2(saved_stderr, STDERR_FILENO) >= 0);
    close(saved_stderr);

    rewind(capture);
    length = fread(output, 1, sizeof(output) - 1, capture);
    output[length] = '\0';
    fclose(capture);

    assert(strstr(output, "event=filtered") == NULL);
    assert(strstr(output, "event=nlog_test value=1") != NULL);
    assert(strstr(output, "level=INFO") != NULL);
    assert(strstr(output, "tag=nlog_test") != NULL);
    assert(strstr(output, "source=nlog_test.c:") != NULL);
    assert(strstr(output, "function=main") != NULL);
    assert(strstr(output, "mono_ms=") != NULL);
    assert(strstr(output, "seq=") != NULL);
    assert(strstr(output, "pid=") != NULL);
    assert(strstr(output, "tid=") != NULL);

    nlog_set_level(NLOG_LEVEL_DEBUG);
    return 0;
}
