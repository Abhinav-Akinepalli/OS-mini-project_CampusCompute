/*
 * rlimit.c - FR7: process resource limits with setrlimit() (Person B)
 *
 * Every demo runs in a forked CHILD, so the limit only affects that child and
 * the parent (the rest of the program) keeps working normally.
 *
 *   nofile : RLIMIT_NOFILE = 16  -> open() starts failing with EMFILE
 *   mem    : RLIMIT_AS = 256 MB  -> malloc() starts returning NULL (ENOMEM)
 *   cpu    : RLIMIT_CPU = 1 s    -> kernel sends SIGXCPU and the child is killed
 *
 * Usage: campus rlimit [nofile|mem|cpu|all]      (default: all)
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include "campus.h"
#include "sysutil.h"

static void print_limit(const char *tag, int resource)
{
    struct rlimit rl;
    if (getrlimit(resource, &rl) == -1) {
        perror("getrlimit");
        return;
    }
    char soft[32], hard[32];
    if (rl.rlim_cur == RLIM_INFINITY) snprintf(soft, sizeof soft, "unlimited");
    else snprintf(soft, sizeof soft, "%llu", (unsigned long long)rl.rlim_cur);
    if (rl.rlim_max == RLIM_INFINITY) snprintf(hard, sizeof hard, "unlimited");
    else snprintf(hard, sizeof hard, "%llu", (unsigned long long)rl.rlim_max);
    printf("  %-7s soft=%s hard=%s\n", tag, soft, hard);
}

/* Lower only the SOFT limit (never above the hard limit). Returns 0 or -1. */
static int set_soft(int resource, rlim_t want)
{
    struct rlimit rl;
    if (getrlimit(resource, &rl) == -1)
        return -1;
    if (rl.rlim_max != RLIM_INFINITY && want > rl.rlim_max)
        want = rl.rlim_max;
    rl.rlim_cur = want;
    return setrlimit(resource, &rl);
}

/* ---- child bodies: return 0 if the expected effect was observed ---- */

static int child_nofile(void)
{
    const rlim_t limit = 16;
    print_limit("before:", RLIMIT_NOFILE);
    if (set_soft(RLIMIT_NOFILE, limit) == -1) {
        perror("  setrlimit(RLIMIT_NOFILE)");
        return 1;
    }
    print_limit("after: ", RLIMIT_NOFILE);

    struct rlimit rl;
    getrlimit(RLIMIT_NOFILE, &rl);
    int lim = (int)rl.rlim_cur;

    int already = 0;                            /* descriptors already in use below the limit */
    for (int fd = 0; fd < lim; fd++)
        if (fcntl(fd, F_GETFD) != -1)
            already++;
    int expected = lim - already;

    int fds[64], opened = 0, err = 0;
    for (;;) {
        int fd = open("/dev/null", O_RDONLY);
        if (fd == -1) {
            err = errno;
            break;
        }
        fds[opened++] = fd;
        if (opened == 64)
            break;
    }
    printf("  open(\"/dev/null\") succeeded %d time(s), then failed: %s (errno %d)\n",
           opened, strerror(err), err);
    printf("  expected %d successes = limit %d - %d descriptors already open\n",
           expected, lim, already);
    for (int i = 0; i < opened; i++)
        close(fds[i]);
    return (err == EMFILE && opened == expected) ? 0 : 1;
}

static int child_mem(void)
{
    const rlim_t limit = 256UL * 1024 * 1024;
    const size_t chunk = 16UL * 1024 * 1024;
    print_limit("before:", RLIMIT_AS);
    if (set_soft(RLIMIT_AS, limit) == -1) {
        perror("  setrlimit(RLIMIT_AS)");
        return 1;
    }
    print_limit("after: ", RLIMIT_AS);

    void *blocks[64];
    int n = 0, err = 0;
    for (;;) {
        void *p = malloc(chunk);
        if (p == NULL) {
            err = errno;
            break;
        }
        blocks[n++] = p;
        if (n == 64)
            break;
    }
    printf("  malloc(16 MB) succeeded %d time(s) (%d MB), then returned NULL: %s (errno %d)\n",
           n, n * 16, strerror(err), err);
    for (int i = 0; i < n; i++)
        free(blocks[i]);
    return (err == ENOMEM && n > 0) ? 0 : 1;
}

static int child_cpu(void)
{
    struct rlimit core = {0, 0};
    setrlimit(RLIMIT_CORE, &core);              /* no core file for this deliberate kill */

    print_limit("before:", RLIMIT_CPU);
    struct rlimit rl;
    getrlimit(RLIMIT_CPU, &rl);
    rl.rlim_cur = 1;                            /* soft: SIGXCPU after 1 s of CPU time */
    if (rl.rlim_max == RLIM_INFINITY || rl.rlim_max > 2)
        rl.rlim_max = 2;                        /* hard: SIGKILL after 2 s (backup) */
    if (setrlimit(RLIMIT_CPU, &rl) == -1) {
        perror("  setrlimit(RLIMIT_CPU)");
        return 1;
    }
    print_limit("after: ", RLIMIT_CPU);
    printf("  burning CPU in a busy loop...\n");
    fflush(stdout);

    volatile unsigned long x = 0;
    for (;;)
        x++;
    return 1;                                   /* never reached */
}

/* ---- parent side ---- */

static int run_in_child(const char *title, int (*body)(void), int expect_sigxcpu)
{
    printf("\n== %s ==\n", title);
    fflush(NULL);
    pid_t p = fork();
    if (p == -1) {
        perror("rlimit: fork");
        return 1;
    }
    if (p == 0)
        _exit(body());

    int st;
    if (waitpid(p, &st, 0) == -1) {
        perror("rlimit: waitpid");
        return 1;
    }
    char desc[96];
    describe_status(st, desc, sizeof desc);
    printf("  [parent] child %d %s\n", (int)p, desc);

    int pass;
    if (expect_sigxcpu)
        pass = WIFSIGNALED(st) && WTERMSIG(st) == SIGXCPU;
    else
        pass = WIFEXITED(st) && WEXITSTATUS(st) == 0;
    printf("  Result: %s\n", pass ? "PASS - limit had the expected effect" : "FAIL");
    return pass ? 0 : 1;
}

static int rlimit_usage(void)
{
    fprintf(stderr, "usage: campus rlimit [nofile|mem|cpu|all]\n");
    return 1;
}

int run_rlimit(int argc, char **argv)
{
    const char *which = "all";
    if (argc > 3)
        return rlimit_usage();
    if (argc == 3)
        which = argv[2];

    int do_nofile = !strcmp(which, "nofile") || !strcmp(which, "all");
    int do_mem    = !strcmp(which, "mem")    || !strcmp(which, "all");
    int do_cpu    = !strcmp(which, "cpu")    || !strcmp(which, "all");
    if (!do_nofile && !do_mem && !do_cpu) {
        fprintf(stderr, "rlimit: unknown demo '%s'\n", which);
        return rlimit_usage();
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    int fails = 0;
    if (do_nofile)
        fails += run_in_child("RLIMIT_NOFILE: open-file limit of 16", child_nofile, 0);
    if (do_mem)
        fails += run_in_child("RLIMIT_AS: address-space limit of 256 MB", child_mem, 0);
    if (do_cpu)
        fails += run_in_child("RLIMIT_CPU: CPU-time limit of 1 second", child_cpu, 1);

    printf("\nrlimit summary: %s\n", fails == 0 ? "all demos PASS" : "some demos FAILED");
    return fails == 0 ? 0 : 1;
}
