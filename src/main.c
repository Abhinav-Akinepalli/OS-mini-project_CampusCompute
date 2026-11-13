/* CampusCompute CLI dispatcher (FR8).  Owner: Person A. */
#include <stdio.h>
#include <string.h>
#include "campus.h"
#include "job.h"

static void usage(FILE *f)
{
    fprintf(f,
        "CampusCompute - job scheduling and resource management prototype\n"
        "usage: campus <command> [arguments]\n"
        "  sched fcfs|rr|priority|compare <jobfile> [-q N]   CPU scheduling (default quantum %d)\n"
        "  banker <file>                                     Banker's safety algorithm\n"
        "  logger [producers] [consumers] [items]            producer-consumer logger (threads)\n"
        "  ipc                                               shared memory + named semaphores\n"
        "  worker normal|fail|signal                         fork/waitpid, simulated failures\n"
        "  rlimit [nofile|mem|cpu|all]                       setrlimit demonstrations\n"
        "  all                                               run every feature, print summary\n"
        "  help                                              show this message\n"
        "Examples:\n"
        "  ./campus sched rr tests/jobs.txt -q 3\n"
        "  ./campus banker tests/banker_safe.txt\n"
        "Job file : one job per line \"id arrival burst priority\"; '#' starts a comment;\n"
        "           smaller priority number = higher priority.\n"
        "Rules    : tie-break = %s.\n"
        "           Round Robin: %s.\n"
        "Exit code: 0 = success; banker: 0 safe, 1 unsafe; 2 = usage or bad input.\n",
        DEFAULT_QUANTUM, TIE_BREAK_RULE, RR_QUEUE_RULE);
}

int main(int argc, char **argv)
{
    /* Line-buffer stdout even when piped (e.g. "| tail"), so output from
     * fork()ed children and from stderr stays in a sensible order. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (argc < 2) { fprintf(stderr, "error: no command given\n"); usage(stderr); return 2; }
    const char *c = argv[1];
    if (!strcmp(c, "help") || !strcmp(c, "--help") || !strcmp(c, "-h")) {
        if (argc > 2) { fprintf(stderr, "error: 'help' takes no arguments (got '%s')\n", argv[2]); return 2; }
        usage(stdout);
        return 0;
    }
    if (!strcmp(c, "sched"))  return run_sched(argc, argv);
    if (!strcmp(c, "banker")) return run_banker(argc, argv);
    if (!strcmp(c, "logger")) return run_logger(argc, argv);
    if (!strcmp(c, "ipc"))    return run_ipc(argc, argv);
    if (!strcmp(c, "worker")) return run_worker(argc, argv);
    if (!strcmp(c, "rlimit")) return run_rlimit(argc, argv);
    if (!strcmp(c, "all"))    return run_all(argc, argv);
    fprintf(stderr, "campus: unknown command '%s'\n", c);
    fprintf(stderr, "Run 'campus help' to see the available commands.\n");
    return 2;
}
