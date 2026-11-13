/*
 * sched.c - CPU scheduling simulator (FR2)
 * Owner: Person A.
 *
 * Policies: FCFS, Round Robin, non-preemptive Priority, and a comparison.
 * Time is simulated (no real sleeping); every number is computed from the
 * input file.
 *
 * Rules (also in README):
 *   - Tie-break: earlier arrival time, then smaller job ID.
 *   - Priority: smaller number = higher priority.
 *   - Round Robin: default quantum DEFAULT_QUANTUM; a job that arrives at
 *     the same instant a quantum expires is queued BEFORE the pre-empted job.
 *   - completion = time the job finishes; turnaround = completion - arrival;
 *     waiting = turnaround - burst.
 */
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "campus.h"
#include "job.h"


/* One slice of the Gantt chart; job_id 0 means the CPU was idle. */
typedef struct {
    int job_id;
    int start;
    int end;
} gantt_seg_t;

typedef struct {
    char   name[16];               /* short name, e.g. "FCFS"                   */
    char   title[64];              /* heading, e.g. "Round Robin (quantum = 2)" */
    int    quantum;                /* 0 when not applicable                     */
    double avg_completion;
    double avg_turnaround;
    double avg_waiting;
    int    makespan;
    int    idle_time;
    int    context_switches;       /* dispatches to a different job             */
    gantt_seg_t *gantt;            /* heap array, free with sched_result_free() */
    int    nseg;
    int    cap;
} sched_result_t;

#define LINE_WIDTH 78
#define MAX_SHOWN_SEGMENTS 60      /* long timelines are truncated on screen */

/* ------------------------------------------------------------------ */
/* Comparators (return <0 if a should run before b)                    */
/* ------------------------------------------------------------------ */
static int cmp_fcfs(const Job *a, const Job *b)
{
    if (a->arrival != b->arrival)
        return a->arrival - b->arrival;
    return a->id - b->id;
}

static int cmp_priority(const Job *a, const Job *b)
{
    if (a->priority != b->priority)
        return a->priority - b->priority;
    return cmp_fcfs(a, b);               /* tie-break rule */
}

/* ------------------------------------------------------------------ */
/* Result / Gantt helpers                                              */
/* ------------------------------------------------------------------ */
static void result_init(sched_result_t *r, const char *name, const char *title,
                        int quantum)
{
    memset(r, 0, sizeof *r);
    snprintf(r->name, sizeof r->name, "%s", name);
    snprintf(r->title, sizeof r->title, "%s", title);
    r->quantum = quantum;
}

static void sched_result_free(sched_result_t *r)
{
    free(r->gantt);
    r->gantt = NULL;
    r->nseg = r->cap = 0;
}

/* Append a slice; merges with the previous slice if it is the same job and
 * contiguous (a lone remaining RR job keeps the CPU without a "switch"). */
static int seg_add(sched_result_t *r, int job_id, int start, int end)
{
    if (end <= start)
        return 0;
    if (r->nseg > 0) {
        gantt_seg_t *last = &r->gantt[r->nseg - 1];
        if (last->job_id == job_id && last->end == start) {
            last->end = end;
            return 0;
        }
    }
    if (r->nseg == r->cap) {
        int newcap = r->cap ? r->cap * 2 : 32;
        gantt_seg_t *p = realloc(r->gantt, (size_t)newcap * sizeof *p);
        if (p == NULL) {
            CC_ERROR("out of memory while building the timeline");
            return -1;
        }
        r->gantt = p;
        r->cap = newcap;
    }
    r->gantt[r->nseg].job_id = job_id;
    r->gantt[r->nseg].start  = start;
    r->gantt[r->nseg].end    = end;
    r->nseg++;
    return 0;
}

static void compute_metrics(Job jobs[], int n, sched_result_t *r)
{
    double sum_ct = 0, sum_tat = 0, sum_wt = 0;
    int total_burst = 0, makespan = 0, dispatches = 0;

    for (int i = 0; i < n; i++) {
        jobs[i].turnaround = jobs[i].completion - jobs[i].arrival;
        jobs[i].waiting    = jobs[i].turnaround - jobs[i].burst;
        sum_ct  += jobs[i].completion;
        sum_tat += jobs[i].turnaround;
        sum_wt  += jobs[i].waiting;
        total_burst += jobs[i].burst;
        if (jobs[i].completion > makespan)
            makespan = jobs[i].completion;
    }
    for (int i = 0; i < r->nseg; i++)
        if (r->gantt[i].job_id != 0)
            dispatches++;

    r->avg_completion = sum_ct / n;
    r->avg_turnaround = sum_tat / n;
    r->avg_waiting    = sum_wt / n;
    r->makespan       = makespan;
    r->idle_time      = makespan - total_burst;
    r->context_switches = dispatches > 0 ? dispatches - 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Non-preemptive engine shared by FCFS and Priority                   */
/* ------------------------------------------------------------------ */
static int sim_nonpreemptive(Job jobs[], int n,
                             int (*cmp)(const Job *, const Job *),
                             sched_result_t *r)
{
    int t = 0, done = 0;

    for (int i = 0; i < n; i++)
        job_reset(&jobs[i]);

    while (done < n) {
        int pick = -1, next_arrival = INT_MAX;

        for (int i = 0; i < n; i++) {
            if (jobs[i].status == JOB_DONE)
                continue;
            if (jobs[i].arrival <= t) {
                jobs[i].status = JOB_READY;
                if (pick < 0 || cmp(&jobs[i], &jobs[pick]) < 0)
                    pick = i;
            } else if (jobs[i].arrival < next_arrival) {
                next_arrival = jobs[i].arrival;
            }
        }

        if (pick < 0) {                  /* nobody ready: CPU idles until next arrival */
            if (seg_add(r, 0, t, next_arrival) < 0)
                return -1;
            t = next_arrival;
            continue;
        }

        Job *j = &jobs[pick];
        j->status = JOB_RUNNING;
        j->start  = t;
        if (seg_add(r, j->id, t, t + j->burst) < 0)
            return -1;
        t += j->burst;
        j->remaining  = 0;
        j->completion = t;
        j->status     = JOB_DONE;
        done++;
    }
    compute_metrics(jobs, n, r);
    return 0;
}

static int sched_fcfs(Job jobs[], int n, sched_result_t *r)
{
    result_init(r, "FCFS", "FCFS (First-Come-First-Served)", 0);
    return sim_nonpreemptive(jobs, n, cmp_fcfs, r);
}

static int sched_priority(Job jobs[], int n, sched_result_t *r)
{
    result_init(r, "Priority", "Priority (non-preemptive)", 0);
    return sim_nonpreemptive(jobs, n, cmp_priority, r);
}

/* ------------------------------------------------------------------ */
/* Round Robin                                                         */
/* ------------------------------------------------------------------ */
typedef struct {
    int items[MAX_JOBS];
    int head, tail, count;
} rr_queue_t;

static void rr_push(rr_queue_t *q, int idx)
{
    q->items[q->tail] = idx;
    q->tail = (q->tail + 1) % MAX_JOBS;
    q->count++;
}

static int rr_pop(rr_queue_t *q)
{
    int idx = q->items[q->head];
    q->head = (q->head + 1) % MAX_JOBS;
    q->count--;
    return idx;
}

/* Move every job that has arrived by time t into the ready queue,
 * in (arrival, ID) order. */
static void rr_admit(Job jobs[], const int order[], int n, int *next,
                     int t, rr_queue_t *q)
{
    while (*next < n && jobs[order[*next]].arrival <= t) {
        int idx = order[(*next)++];
        jobs[idx].status = JOB_READY;
        rr_push(q, idx);
    }
}

static int sched_rr(Job jobs[], int n, int quantum, sched_result_t *r)
{
    char title[64];
    int order[MAX_JOBS];
    rr_queue_t q = { {0}, 0, 0, 0 };
    int t = 0, done = 0, next = 0;

    snprintf(title, sizeof title, "Round Robin (time quantum = %d)", quantum);
    result_init(r, "RR", title, quantum);

    for (int i = 0; i < n; i++) {
        job_reset(&jobs[i]);
        order[i] = i;
    }
    /* insertion sort of indices by (arrival, ID) */
    for (int i = 1; i < n; i++) {
        int key = order[i], k = i - 1;
        while (k >= 0 && cmp_fcfs(&jobs[order[k]], &jobs[key]) > 0) {
            order[k + 1] = order[k];
            k--;
        }
        order[k + 1] = key;
    }

    rr_admit(jobs, order, n, &next, t, &q);
    while (done < n) {
        if (q.count == 0) {              /* idle until the next job arrives */
            int arrive = jobs[order[next]].arrival;
            if (seg_add(r, 0, t, arrive) < 0)
                return -1;
            t = arrive;
            rr_admit(jobs, order, n, &next, t, &q);
            continue;
        }

        int idx = rr_pop(&q);
        Job *j = &jobs[idx];
        int run = j->remaining < quantum ? j->remaining : quantum;

        if (j->start < 0)
            j->start = t;
        j->status = JOB_RUNNING;
        if (seg_add(r, j->id, t, t + run) < 0)
            return -1;
        t += run;
        j->remaining -= run;

        /* Arrivals up to and including time t go in FIRST (tie rule),
         * then the pre-empted job rejoins at the back. */
        rr_admit(jobs, order, n, &next, t, &q);
        if (j->remaining == 0) {
            j->completion = t;
            j->status = JOB_DONE;
            done++;
        } else {
            j->status = JOB_READY;
            rr_push(&q, idx);
        }
    }
    compute_metrics(jobs, n, r);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Printing                                                            */
/* ------------------------------------------------------------------ */
static int digits(int v)
{
    int d = 1;
    while (v >= 10) {
        v /= 10;
        d++;
    }
    return d;
}

static void seg_label(const gantt_seg_t *s, char *buf, size_t sz)
{
    if (s->job_id == 0)
        snprintf(buf, sz, "IDLE");
    else
        snprintf(buf, sz, "J%d", s->job_id);
}

/* Width of a Gantt cell: label plus padding, and wide enough for the
 * start-time marker printed beneath it. */
static int cell_width(const gantt_seg_t *s)
{
    char lab[16];
    int w, tw = digits(s->start) + 1;
    seg_label(s, lab, sizeof lab);
    w = (int)strlen(lab) + 2;
    return w > tw ? w : tw;
}

static void print_gantt(const sched_result_t *r)
{
    int i = 0;
    int lim = r->nseg < MAX_SHOWN_SEGMENTS ? r->nseg : MAX_SHOWN_SEGMENTS;

    printf("\nTimeline (Gantt chart):\n");
    while (i < lim) {
        int width = 1, k = i;

        /* choose how many cells fit on this row */
        while (k < lim) {
            int w = cell_width(&r->gantt[k]);
            if (k > i && width + w + 1 > LINE_WIDTH)
                break;
            width += w + 1;
            k++;
        }
        putchar('|');
        for (int s = i; s < k; s++) {
            char lab[16];
            int w = cell_width(&r->gantt[s]);
            int len, left, right;
            seg_label(&r->gantt[s], lab, sizeof lab);
            len = (int)strlen(lab);
            left = (w - len) / 2;
            right = w - len - left;
            printf("%*s%s%*s|", left, "", lab, right, "");
        }
        putchar('\n');
        for (int s = i; s < k; s++)
            printf("%-*d", cell_width(&r->gantt[s]) + 1, r->gantt[s].start);
        printf("%d\n", r->gantt[k - 1].end);
        i = k;
    }
    if (r->nseg > lim)
        printf("(... %d more segments not shown; the table below is complete)\n", r->nseg - lim);
}

/* "Execution order: J1 -> J2 -> ..." (idle time omitted), word-wrapped. */
static void print_order(const char *prefix, const sched_result_t *r)
{
    int col = (int)strlen(prefix), first = 1;
    int indent = (int)strlen(prefix);

    printf("%s", prefix);
    for (int i = 0; i < r->nseg && i < MAX_SHOWN_SEGMENTS; i++) {
        char tok[32];
        int len;
        if (r->gantt[i].job_id == 0)
            continue;
        snprintf(tok, sizeof tok, "J%d", r->gantt[i].job_id);
        len = (int)strlen(tok);
        if (!first) {
            if (col + 4 + len > LINE_WIDTH) {
                printf(" ->\n%*s", indent, "");
                col = indent;
            } else {
                printf(" -> ");
                col += 4;
            }
        }
        printf("%s", tok);
        col += len;
        first = 0;
    }
    if (r->nseg > MAX_SHOWN_SEGMENTS)
        printf(" -> ...");
    putchar('\n');
}

static void print_job_inputs(const Job jobs[], int n)
{
    printf("%-5s %8s %6s %9s\n", "Job", "Arrival", "Burst", "Priority");
    for (int i = 0; i < n; i++)
        printf("J%-4d %8d %6d %9d\n", jobs[i].id, jobs[i].arrival,
               jobs[i].burst, jobs[i].priority);
}

static void print_report(const sched_result_t *r, const Job jobs[], int n,
                         const char *path)
{
    printf("\n=== %s ===\n", r->title);
    printf("Input file : %s (%d jobs)\n", path, n);
    printf("Tie-break  : %s\n", TIE_BREAK_RULE);
    if (strcmp(r->name, "Priority") == 0)
        printf("Priority   : smaller number = higher priority (0 is highest)\n");
    if (strcmp(r->name, "RR") == 0) {
        printf("Quantum    : %d time units\n", r->quantum);
        printf("Queue rule : %s\n", RR_QUEUE_RULE);
    }

    print_gantt(r);
    printf("\n");
    print_order("Execution order: ", r);

    printf("\n%-5s %8s %6s %5s %6s %11s %11s %8s %7s\n", "Job", "Arrival",
           "Burst", "Prio", "Start", "Completion", "Turnaround", "Waiting",
           "Status");
    for (int i = 0; i < n; i++)
        printf("J%-4d %8d %6d %5d %6d %11d %11d %8d %7s\n", jobs[i].id,
               jobs[i].arrival, jobs[i].burst, jobs[i].priority,
               jobs[i].start, jobs[i].completion, jobs[i].turnaround,
               jobs[i].waiting, status_name(jobs[i].status));

    printf("\nAverage completion time : %.2f\n", r->avg_completion);
    printf("Average turnaround time : %.2f\n", r->avg_turnaround);
    printf("Average waiting time    : %.2f\n", r->avg_waiting);
    printf("Total time (makespan)   : %d\n", r->makespan);
    if (r->idle_time > 0)
        printf("CPU idle time           : %d\n", r->idle_time);
}

/* Print the names of every policy whose metric equals the minimum. */
static void print_best(const char *label, const sched_result_t res[], int cnt,
                       double (*metric)(const sched_result_t *))
{
    double best = metric(&res[0]);
    int first = 1;

    for (int i = 1; i < cnt; i++)
        if (metric(&res[i]) < best)
            best = metric(&res[i]);
    printf("%-26s: ", label);
    for (int i = 0; i < cnt; i++) {
        if (metric(&res[i]) - best < 1e-9) {
            printf("%s%s", first ? "" : ", ", res[i].name);
            first = 0;
        }
    }
    printf(" (%.2f)\n", best);
}

static double metric_wt(const sched_result_t *r)  { return r->avg_waiting; }
static double metric_tat(const sched_result_t *r) { return r->avg_turnaround; }

static void print_comparison(const sched_result_t res[], int cnt,
                             const Job jobs[], int n, const char *path,
                             int quantum)
{
    char label[32];

    printf("\n=== Scheduling comparison (same job set) ===\n");
    printf("Input file : %s (%d jobs)\n", path, n);
    printf("Tie-break  : %s\n", TIE_BREAK_RULE);
    printf("Quantum    : %d (Round Robin)\n\n", quantum);
    print_job_inputs(jobs, n);

    printf("\nExecution order:\n");
    for (int i = 0; i < cnt; i++) {
        snprintf(label, sizeof label, "  %-9s: ", res[i].name);
        print_order(label, &res[i]);
    }

    printf("\n%-10s %7s %8s %8s %9s %10s\n", "Policy", "Avg CT", "Avg TAT",
           "Avg WT", "Makespan", "Ctx switch");
    for (int i = 0; i < cnt; i++)
        printf("%-10s %7.2f %8.2f %8.2f %9d %10d\n", res[i].name,
               res[i].avg_completion, res[i].avg_turnaround,
               res[i].avg_waiting, res[i].makespan, res[i].context_switches);

    printf("\n");
    print_best("Lowest avg waiting time", res, cnt, metric_wt);
    print_best("Lowest avg turnaround time", res, cnt, metric_tat);
    printf("(CT = completion, TAT = turnaround, WT = waiting; "
           "Ctx switch = dispatches to a different job)\n");
}

/* ------------------------------------------------------------------ */
/* CLI: campus sched fcfs|rr|priority|compare <jobfile> [-q N]         */
/* Exit codes: 0 ok, 1 internal failure, 2 usage / bad input.          */
/* ------------------------------------------------------------------ */
static void sched_usage(void)
{
    fprintf(stderr, "usage: campus sched fcfs|rr|priority|compare <jobfile> [-q N]\n"
                    "  -q N   Round Robin time quantum (positive integer, 1..%d, default %d);\n"
                    "         only for rr and compare\n", MAX_BURST, DEFAULT_QUANTUM);
}

int run_sched(int argc, char **argv)
{
    const char *algo, *path = NULL;
    long quantum = DEFAULT_QUANTUM;
    int  quantum_given = 0;
    Job jobs[MAX_JOBS], work[MAX_JOBS];
    int n, rc = 0;

    if (argc < 3) {
        CC_ERROR("sched: missing policy");
        sched_usage();
        return 2;
    }
    algo = argv[2];
    if (strcasecmp(algo, "fcfs") && strcasecmp(algo, "rr") &&
        strcasecmp(algo, "priority") && strcasecmp(algo, "compare")) {
        CC_ERROR("unknown policy '%s'", algo);
        sched_usage();
        return 2;
    }

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-q") == 0) {
            if (quantum_given) {
                CC_ERROR("-q given more than once");
                sched_usage();
                return 2;
            }
            if (i + 1 >= argc) {
                CC_ERROR("-q needs a value");
                sched_usage();
                return 2;
            }
            if (parse_long_strict(argv[++i], 1, MAX_BURST, &quantum) != 0) {
                CC_ERROR("quantum must be a positive integer (1..%d), got '%s'",
                         MAX_BURST, argv[i]);
                return 2;
            }
            quantum_given = 1;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            CC_ERROR("unexpected argument '%s'", argv[i]);
            sched_usage();
            return 2;
        } else if (path == NULL) {
            path = argv[i];
        } else {
            CC_ERROR("unexpected argument '%s'", argv[i]);
            sched_usage();
            return 2;
        }
    }

    if (path == NULL) {
        CC_ERROR("sched: missing job file");
        sched_usage();
        return 2;
    }
    if (quantum_given && strcasecmp(algo, "rr") && strcasecmp(algo, "compare")) {
        CC_ERROR("-q only applies to 'rr' and 'compare'");
        sched_usage();
        return 2;
    }

    n = load_jobs(path, jobs);
    if (n < 0)
        return 2;

    if (strcasecmp(algo, "compare") == 0) {
        sched_result_t res[3];
        memset(res, 0, sizeof res);

        memcpy(work, jobs, sizeof jobs);
        if (sched_fcfs(work, n, &res[0]) < 0) rc = 1;
        memcpy(work, jobs, sizeof jobs);
        if (!rc && sched_rr(work, n, (int)quantum, &res[1]) < 0) rc = 1;
        memcpy(work, jobs, sizeof jobs);
        if (!rc && sched_priority(work, n, &res[2]) < 0) rc = 1;
        if (!rc)
            print_comparison(res, 3, jobs, n, path, (int)quantum);
        for (int i = 0; i < 3; i++)
            sched_result_free(&res[i]);
        return rc;
    }

    {
        sched_result_t res;
        memset(&res, 0, sizeof res);
        memcpy(work, jobs, sizeof jobs);

        if (strcasecmp(algo, "fcfs") == 0)
            rc = sched_fcfs(work, n, &res);
        else if (strcasecmp(algo, "rr") == 0)
            rc = sched_rr(work, n, (int)quantum, &res);
        else
            rc = sched_priority(work, n, &res);

        if (rc == 0)
            print_report(&res, work, n, path);
        sched_result_free(&res);
        return rc == 0 ? 0 : 1;
    }
}
