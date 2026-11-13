/*
 * worker.c - FR6: worker failure detection (Person B)
 *
 * The parent fork()s three workers and collects each one with waitpid().
 * The status is decoded with WIFEXITED/WEXITSTATUS and WIFSIGNALED/WTERMSIG.
 *
 *   campus worker normal  : all three workers exit with code 0
 *   campus worker fail    : worker 2 deliberately exits with code 42
 *   campus worker signal  : worker 2 deliberately kills itself with SIGTERM
 *
 * The failures are SIMULATED by this program on purpose; they are printed as
 * such and are NOT real operating-system faults.
 *
 * The command returns 0 when the parent's detection matched what was
 * intended for the chosen mode (that is what "PASS" means here).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "campus.h"
#include "sysutil.h"

#define N_WORKERS      3
#define FAILING_WORKER 2
#define SIM_FAIL_CODE  42

typedef enum { MODE_NORMAL, MODE_FAIL, MODE_SIGNAL } Mode;

/* Runs in the child process and never returns. */
static void worker_body(int idx, Mode mode)
{
    printf("[worker %d pid %d] started\n", idx, (int)getpid());
    fflush(stdout);

    volatile unsigned long sum = 0;             /* pretend to compute something */
    for (unsigned long i = 0; i < 2000000UL * (unsigned long)idx; i++)
        sum += i;
    usleep(100000 * (unsigned)idx);             /* stagger the finishing times */

    if (idx == FAILING_WORKER && mode == MODE_FAIL) {
        printf("[worker %d pid %d] *** SIMULATED FAILURE: exiting with code %d "
               "(deliberate, not a real OS fault) ***\n", idx, (int)getpid(), SIM_FAIL_CODE);
        fflush(stdout);
        _exit(SIM_FAIL_CODE);
    }
    if (idx == FAILING_WORKER && mode == MODE_SIGNAL) {
        printf("[worker %d pid %d] *** SIMULATED FAILURE: sending myself SIGTERM "
               "(deliberate, not a real OS fault) ***\n", idx, (int)getpid());
        fflush(stdout);
        raise(SIGTERM);
        _exit(99);                              /* only reached if SIGTERM is ignored */
    }
    printf("[worker %d pid %d] finished OK\n", idx, (int)getpid());
    fflush(stdout);
    _exit(0);
}

static int worker_usage(void)
{
    fprintf(stderr, "usage: campus worker normal|fail|signal\n");
    return 1;
}

int run_worker(int argc, char **argv)
{
    Mode mode;
    if (argc != 3)
        return worker_usage();
    if (strcmp(argv[2], "normal") == 0)      mode = MODE_NORMAL;
    else if (strcmp(argv[2], "fail") == 0)   mode = MODE_FAIL;
    else if (strcmp(argv[2], "signal") == 0) mode = MODE_SIGNAL;
    else {
        fprintf(stderr, "worker: unknown mode '%s'\n", argv[2]);
        return worker_usage();
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("[parent pid %d] starting %d workers, mode=%s%s\n", (int)getpid(), N_WORKERS, argv[2],
           mode == MODE_NORMAL ? "" : " (failure of worker 2 is SIMULATED)");

    pid_t pids[N_WORKERS];
    int started = 0;
    fflush(NULL);                               /* avoid duplicated stdio buffers in children */
    for (int i = 1; i <= N_WORKERS; i++) {
        pid_t p = fork();
        if (p == -1) {
            perror("worker: fork");
            break;
        }
        if (p == 0)
            worker_body(i, mode);
        pids[started++] = p;
    }

    int ok_count = 0, bad_count = 0;
    int bad_idx = 0, bad_exit = -1, bad_sig = -1;

    for (int reaped = 0; reaped < started; ) {
        int st;
        pid_t r = waitpid(-1, &st, 0);
        if (r == -1) {
            if (errno == EINTR)
                continue;
            perror("worker: waitpid");
            break;
        }
        reaped++;
        int idx = 0;
        for (int k = 0; k < started; k++)
            if (pids[k] == r)
                idx = k + 1;

        char desc[96];
        int kind = describe_status(st, desc, sizeof desc);
        printf("[parent] worker %d (pid %d) %s -> %s\n", idx, (int)r, desc,
               kind == 0 ? "OK" : "FAILURE DETECTED");
        if (kind == 0) {
            ok_count++;
        } else {
            bad_count++;
            bad_idx = idx;
            if (WIFEXITED(st))   bad_exit = WEXITSTATUS(st);
            if (WIFSIGNALED(st)) bad_sig = WTERMSIG(st);
        }
    }

    /* did the parent see exactly what this mode was designed to produce? */
    int pass = (started == N_WORKERS);
    if (mode == MODE_NORMAL)
        pass = pass && ok_count == N_WORKERS && bad_count == 0;
    else if (mode == MODE_FAIL)
        pass = pass && bad_count == 1 && bad_idx == FAILING_WORKER && bad_exit == SIM_FAIL_CODE;
    else
        pass = pass && bad_count == 1 && bad_idx == FAILING_WORKER && bad_sig == SIGTERM;

    printf("Summary: %d worker(s) OK, %d failed%s -> detection %s\n", ok_count, bad_count,
           bad_count ? " (simulated)" : "", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
