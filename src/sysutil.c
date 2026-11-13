#define _GNU_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "sysutil.h"

int describe_status(int status, char *buf, size_t n)
{
    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code == 0)
            snprintf(buf, n, "exited normally (code 0)");
        else
            snprintf(buf, n, "exited with NON-ZERO code %d", code);
        return code == 0 ? 0 : 1;
    }
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        snprintf(buf, n, "was terminated by signal %d (%s)%s", sig, strsignal(sig),
                 WCOREDUMP(status) ? ", core dumped" : "");
        return 2;
    }
    snprintf(buf, n, "unknown wait status 0x%x", status);
    return 3;
}

int count_own_ipc_objects(void)
{
    DIR *d = opendir("/dev/shm");
    if (d == NULL)
        return -1;

    char suffix[32];
    snprintf(suffix, sizeof suffix, "_%d", (int)getpid());
    size_t slen = strlen(suffix);

    int count = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t len = strlen(e->d_name);
        if (strstr(e->d_name, "campus_") != NULL && len >= slen &&
            strcmp(e->d_name + len - slen, suffix) == 0)
            count++;
    }
    closedir(d);
    return count;
}

int current_thread_count(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    if (f == NULL)
        return -1;
    char line[256];
    int threads = -1;
    while (fgets(line, sizeof line, f) != NULL) {
        if (sscanf(line, "Threads: %d", &threads) == 1)
            break;
    }
    fclose(f);
    return threads;
}
