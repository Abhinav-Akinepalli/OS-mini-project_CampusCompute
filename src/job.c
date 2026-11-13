/*
 * job.c - job model, file parser and input validation (FR1)
 * Owner: Person A.
 *
 * Job file format (one job per line, '#' starts a comment):
 *     <id> <arrival> <burst> <priority>
 * Every bad line is reported as  "<file>:<line>: <problem>"  and the whole
 * file is rejected if any line is bad, so nothing runs on partial input.
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "job.h"

int parse_long_strict(const char *s, long lo, long hi, long *out)
{
    char *end;
    long v;

    if (s == NULL || *s == '\0')
        return -1;
    errno = 0;
    v = strtol(s, &end, 10);
    if (end == s || *end != '\0')
        return -1;                       /* not a pure integer */
    if (errno == ERANGE || v < lo || v > hi)
        return -2;                       /* out of range */
    *out = v;
    return 0;
}

const char *status_name(JobStatus s)
{
    switch (s) {
    case JOB_NEW:     return "NEW";
    case JOB_READY:   return "READY";
    case JOB_RUNNING: return "RUNNING";
    case JOB_DONE:    return "DONE";
    }
    return "?";
}

void job_reset(Job *j)
{
    j->remaining  = j->burst;
    j->start      = -1;
    j->completion = 0;
    j->turnaround = 0;
    j->waiting    = 0;
    j->status     = JOB_NEW;
}

/* Parse one field; on failure print "<path>:<line>: <what> ..." and return -1. */
static int read_field(const char *tok, const char *what, long lo, long hi,
                      const char *path, int lineno, long *out)
{
    int rc = parse_long_strict(tok, lo, hi, out);
    if (rc == -1) {
        CC_PARSE_ERROR("%s:%d: %s '%s' is not an integer", path, lineno, what, tok);
        return -1;
    }
    if (rc == -2) {
        CC_PARSE_ERROR("%s:%d: %s must be between %ld and %ld (got '%s')",
                 path, lineno, what, lo, hi, tok);
        return -1;
    }
    return 0;
}

int load_jobs(const char *path, Job jobs[MAX_JOBS])
{
    FILE *fp = fopen(path, "r");
    char  line[256];
    int   lineno = 0, n = 0, errors = 0, too_many_reported = 0;
    const int max_jobs = MAX_JOBS;

    if (fp == NULL) {
        CC_ERROR("cannot open '%s': %s", path, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof line, fp) != NULL) {
        char *fields[8];
        int   nf = 0;
        long  id, arrival, burst, prio;
        size_t len = strlen(line);

        lineno++;

        /* A line that filled the buffer without a newline is too long. */
        if (len == sizeof line - 1 && line[len - 1] != '\n') {
            int c;
            CC_PARSE_ERROR("%s:%d: line is too long", path, lineno);
            errors++;
            while ((c = fgetc(fp)) != EOF && c != '\n')
                ;                                  /* discard the rest */
            continue;
        }

        char *hash = strchr(line, '#');            /* strip comment */
        if (hash != NULL)
            *hash = '\0';

        for (char *tok = strtok(line, " \t\r\n"); tok != NULL;
             tok = strtok(NULL, " \t\r\n")) {
            if (nf < 8)
                fields[nf] = tok;
            nf++;
        }
        if (nf == 0)
            continue;                              /* blank / comment-only */

        if (nf != 4) {
            CC_PARSE_ERROR("%s:%d: expected 4 fields (id arrival burst priority) but found %d",
                     path, lineno, nf);
            errors++;
            continue;
        }

        /* Validate all four fields so every problem on the line is shown. */
        int bad = 0;
        bad |= read_field(fields[0], "job ID",       1, MAX_JOB_ID,   path, lineno, &id);
        bad |= read_field(fields[1], "arrival time", 0, MAX_BURST,    path, lineno, &arrival);
        bad |= read_field(fields[2], "CPU burst",    1, MAX_BURST,    path, lineno, &burst);
        bad |= read_field(fields[3], "priority",     0, MAX_PRIORITY, path, lineno, &prio);
        if (bad) {
            errors++;
            continue;
        }

        for (int i = 0; i < n; i++) {
            if (jobs[i].id == (int)id) {
                CC_PARSE_ERROR("%s:%d: duplicate job ID %ld (first used by an earlier line)",
                         path, lineno, id);
                bad = 1;
                break;
            }
        }
        if (bad) {
            errors++;
            continue;
        }

        if (n >= max_jobs) {
            if (!too_many_reported) {
                CC_PARSE_ERROR("%s:%d: more than %d jobs; extra jobs are not accepted",
                         path, lineno, max_jobs);
                too_many_reported = 1;
            }
            errors++;
            continue;
        }

        jobs[n].id       = (int)id;
        jobs[n].arrival  = (int)arrival;
        jobs[n].burst    = (int)burst;
        jobs[n].priority = (int)prio;
        job_reset(&jobs[n]);
        n++;
    }

    if (ferror(fp)) {
        CC_ERROR("error while reading '%s': %s", path, strerror(errno));
        fclose(fp);
        return -1;
    }
    fclose(fp);

    if (errors > 0) {
        CC_ERROR("%s: %d invalid line(s) - nothing was scheduled", path, errors);
        return -1;
    }
    if (n == 0) {
        CC_ERROR("'%s' contains no jobs (expected lines like: 1 0 5 2)", path);
        return -1;
    }
    return n;
}
