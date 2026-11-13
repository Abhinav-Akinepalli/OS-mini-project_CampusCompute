/*
 * integrate.c - FR8/S20/S21: integrated demonstration (Person B)
 *
 * `campus all` runs every feature in order and prints a summary built from the
 * REAL return values (nothing is hard-coded), then checks that nothing was
 * left behind: no child processes, no extra threads, no IPC objects.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "campus.h"
#include "sysutil.h"

#define MAX_CHECKS 32

typedef struct {
    char name[64];
    int  passed;
    char detail[96];
} Check;

static Check checks[MAX_CHECKS];
static int   nchecks;

static void add_check(const char *name, int passed, const char *detail)
{
    if (nchecks >= MAX_CHECKS)
        return;
    snprintf(checks[nchecks].name, sizeof checks[nchecks].name, "%s", name);
    snprintf(checks[nchecks].detail, sizeof checks[nchecks].detail, "%s", detail);
    checks[nchecks].passed = passed;
    nchecks++;
}

static void banner(int step, int total, const char *title)
{
    printf("\n=============== [%d/%d] %s ===============\n", step, total, title);
    fflush(stdout);
}

/* Run one feature and compare its return code with the expected one. */
static void run_step(int step, int total, const char *title, int (*fn)(int, char **),
                     int argc, char **argv, int expected_rc)
{
    banner(step, total, title);
    int rc = fn(argc, argv);
    char detail[96];
    snprintf(detail, sizeof detail, "return code %d (expected %d)", rc, expected_rc);
    add_check(title, rc == expected_rc, detail);
}

int run_all(int argc, char **argv)
{
    (void)argv;
    if (argc > 2) {
        fprintf(stderr, "usage: campus all\n");
        return 1;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    nchecks = 0;

    char *a_sched[]  = {"campus", "sched", "compare", "tests/jobs.txt", NULL};
    char *a_safe[]   = {"campus", "banker", "tests/banker_safe.txt", NULL};
    char *a_unsafe[] = {"campus", "banker", "tests/banker_unsafe.txt", NULL};
    char *a_logger[] = {"campus", "logger", "3", "1", "5", NULL};
    char *a_ipc[]    = {"campus", "ipc", NULL};
    char *a_wnorm[]  = {"campus", "worker", "normal", NULL};
    char *a_wfail[]  = {"campus", "worker", "fail", NULL};
    char *a_rlim[]   = {"campus", "rlimit", "all", NULL};

    const int total = 9;
    run_step(1, total, "Scheduling comparison",        run_sched,  4, a_sched,  0);
    run_step(2, total, "Banker's: safe state",         run_banker, 3, a_safe,   0);
    run_step(3, total, "Banker's: unsafe state",       run_banker, 3, a_unsafe, 1);
    run_step(4, total, "Producer-consumer logger",     run_logger, 5, a_logger, 0);
    run_step(5, total, "Shared memory + semaphores",   run_ipc,    2, a_ipc,    0);
    run_step(6, total, "Worker: normal completion",    run_worker, 3, a_wnorm,  0);
    run_step(7, total, "Worker: simulated failure",    run_worker, 3, a_wfail,  0);
    run_step(8, total, "Resource limits",              run_rlimit, 3, a_rlim,   0);

    /* ---- step 9: did we clean up after ourselves? ---- */
    banner(9, total, "Cleanup verification");
    char d[96];

    errno = 0;
    pid_t left = waitpid(-1, NULL, WNOHANG);
    snprintf(d, sizeof d, left == -1 && errno == ECHILD ? "no child processes remain"
                                                        : "a child is still around");
    add_check("No leftover child processes", left == -1 && errno == ECHILD, d);
    printf("  %s\n", d);

    int thr = current_thread_count();
    snprintf(d, sizeof d, "threads in this process: %d (expected 1)", thr);
    add_check("All worker threads ended", thr == 1 || thr == -1, d);
    printf("  %s\n", d);

    int ipc = count_own_ipc_objects();
    if (ipc < 0)
        snprintf(d, sizeof d, "/dev/shm not readable, check skipped");
    else
        snprintf(d, sizeof d, "our shared-memory/semaphore objects left: %d (expected 0)", ipc);
    add_check("IPC objects released", ipc == 0 || ipc == -1, d);
    printf("  %s\n", d);

    /* ---- summary ---- */
    printf("\n=============== INTEGRATED SUMMARY ===============\n");
    int failed = 0;
    for (int i = 0; i < nchecks; i++) {
        printf(" %-4s %-32s %s\n", checks[i].passed ? "PASS" : "FAIL", checks[i].name,
               checks[i].detail);
        if (!checks[i].passed)
            failed++;
    }
    printf("--------------------------------------------------\n");
    printf(" %d of %d checks passed%s\n", nchecks - failed, nchecks,
           failed ? "  -> SOME CHECKS FAILED" : "  -> ALL OK");
    printf(" Log file written by the logger demo: %s\n", LOG_PATH);
    return failed ? 1 : 0;
}
