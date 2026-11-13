#ifndef JOB_H
#define JOB_H

#include <stdio.h>

/* Job model (FR1).  Input file: one job per line  "id arrival burst priority".
 * Lines may be blank or start with '#'.  Lower priority number = more urgent. */
#define MAX_JOBS        64
#define MAX_BURST       100000      /* also the limit for arrival time and RR quantum */
#define MAX_JOB_ID      1000000
#define MAX_PRIORITY    1000
#define DEFAULT_QUANTUM 2

/* Scheduling rules shared by every policy (documented in README). */
#define TIE_BREAK_RULE "earlier arrival, then smaller job id"
#define RR_QUEUE_RULE  "a job arriving exactly when a quantum expires is queued before the pre-empted job"

/* Message format shared by Person A's code:
 *   general errors   ->  stderr, "error: ..."
 *   input-file errors -> stderr, "file:line: reason"                          */
#define CC_ERROR(...)                                                          \
    do { fflush(stdout); fprintf(stderr, "error: "); fprintf(stderr, __VA_ARGS__); \
         fputc('\n', stderr); } while (0)
#define CC_PARSE_ERROR(...)                                                    \
    do { fflush(stdout); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)

typedef enum { JOB_NEW, JOB_READY, JOB_RUNNING, JOB_DONE } JobStatus;

typedef struct {
    int id, arrival, burst, priority;
    int remaining;                 /* CPU time still needed                */
    int start;                     /* first time on CPU, -1 if never       */
    int completion, turnaround, waiting;
    JobStatus status;
} Job;

/* Reads and validates a job file.  On success returns the job count (>0).
 * On failure prints "file:line: reason" to stderr (every bad line) and returns -1. */
int load_jobs(const char *path, Job jobs[MAX_JOBS]);

const char *status_name(JobStatus s);
void        job_reset(Job *j);     /* clears runtime fields before a simulation */

/* Whole-string base-10 integer in [lo, hi]: 0 ok, -1 not a number, -2 out of range. */
int parse_long_strict(const char *s, long lo, long hi, long *out);

#endif
