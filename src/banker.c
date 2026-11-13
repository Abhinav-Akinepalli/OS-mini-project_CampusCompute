/*
 * banker.c - Banker's safety algorithm (FR5)
 * Owner: Person A.
 *
 * State file format (whitespace separated integers, '#' starts a comment):
 *     n m                      number of processes, number of resource types
 *     Available[0..m-1]
 *     Allocation  (n rows of m numbers)
 *     Maximum     (n rows of m numbers)
 * Need = Maximum - Allocation is computed by the program.
 *
 * Safety algorithm: Work = Available. Scan P0..Pn-1 in index order; a
 * process that is unfinished and has Need <= Work "finishes" and releases its
 * Allocation (Work += Allocation). Repeat passes until a whole pass makes no
 * progress. If every process finished the state is SAFE and the finishing
 * order is a safe sequence; otherwise it is UNSAFE. (Any safe sequence proves
 * safety; scanning in index order just makes the output repeatable.)
 *
 * Return value of run_banker: 0 = safe, 1 = unsafe, 2 = error.
 */
#include <errno.h>
#include <string.h>
#include "campus.h"
#include "job.h"

#define MAX_PROC      32
#define MAX_RES       16
#define MAX_INSTANCES 1000000
#define MAX_TOKENS    (2 + MAX_RES + 2 * MAX_PROC * MAX_RES)

typedef struct {
    int n, m;
    int available[MAX_RES];
    int allocation[MAX_PROC][MAX_RES];
    int maximum[MAX_PROC][MAX_RES];
    int need[MAX_PROC][MAX_RES];
} banker_state_t;

enum { BANKER_SAFE = 0, BANKER_UNSAFE = 1, BANKER_ERROR = 2 };

/* ------------------------------------------------------------------ */
/* Loading and validation                                              */
/* ------------------------------------------------------------------ */
static int load_state(const char *path, banker_state_t *s)
{
    FILE *fp = fopen(path, "r");
    long tok[MAX_TOKENS];
    int  tok_line[MAX_TOKENS];
    int  nt = 0, lineno = 0, errors = 0, overflow = 0;
    char line[256];

    if (fp == NULL) {
        CC_ERROR("cannot open '%s': %s", path, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof line, fp) != NULL) {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash != NULL)
            *hash = '\0';

        for (char *t = strtok(line, " \t\r\n"); t != NULL;
             t = strtok(NULL, " \t\r\n")) {
            long v;
            int rc;
            if (nt >= MAX_TOKENS) {
                if (!overflow) {
                    CC_PARSE_ERROR("%s:%d: too many numbers (at most %d processes x %d resources)",
                             path, lineno, MAX_PROC, MAX_RES);
                    overflow = 1;
                    errors++;
                }
                continue;
            }
            rc = parse_long_strict(t, 0, MAX_INSTANCES, &v);
            if (rc == -1) {
                CC_PARSE_ERROR("%s:%d: '%s' is not an integer", path, lineno, t);
                errors++;
            } else if (rc == -2) {
                CC_PARSE_ERROR("%s:%d: value '%s' must be between 0 and %d",
                         path, lineno, t, MAX_INSTANCES);
                errors++;
            } else {
                tok[nt] = v;
                tok_line[nt] = lineno;
                nt++;
            }
        }
    }
    if (ferror(fp)) {
        CC_ERROR("error while reading '%s': %s", path, strerror(errno));
        fclose(fp);
        return -1;
    }
    fclose(fp);

    if (errors > 0) {
        CC_ERROR("%s: %d invalid entry(ies) - safety check not run", path, errors);
        return -1;
    }
    if (nt < 2) {
        CC_ERROR("%s: expected 'n m' (processes, resource types) on the first line", path);
        return -1;
    }

    long n = tok[0], m = tok[1];
    if (n < 1 || n > MAX_PROC || m < 1 || m > MAX_RES) {
        CC_ERROR("%s: need 1..%d processes and 1..%d resource types (got n=%ld, m=%ld)",
                 path, MAX_PROC, MAX_RES, n, m);
        return -1;
    }

    int expected = 2 + (int)m + 2 * (int)n * (int)m;
    if (nt < expected) {
        CC_ERROR("%s: expected %d numbers (header 2 + Available %ld + Allocation %ld + Maximum %ld) but found %d",
                 path, expected, m, n * m, n * m, nt);
        return -1;
    }
    if (nt > expected) {
        CC_PARSE_ERROR("%s:%d: unexpected extra data after the Maximum matrix", path,
                 tok_line[expected]);
        return -1;
    }

    s->n = (int)n;
    s->m = (int)m;
    int p = 2;
    for (int j = 0; j < s->m; j++)
        s->available[j] = (int)tok[p++];
    for (int i = 0; i < s->n; i++)
        for (int j = 0; j < s->m; j++)
            s->allocation[i][j] = (int)tok[p++];
    for (int i = 0; i < s->n; i++)
        for (int j = 0; j < s->m; j++)
            s->maximum[i][j] = (int)tok[p++];

    /* Need = Maximum - Allocation must be non-negative. */
    for (int i = 0; i < s->n; i++) {
        for (int j = 0; j < s->m; j++) {
            if (s->allocation[i][j] > s->maximum[i][j]) {
                CC_ERROR("%s: P%d holds %d of resource %c but its Maximum is only %d",
                         path, i, s->allocation[i][j], 'A' + j, s->maximum[i][j]);
                errors++;
            }
            s->need[i][j] = s->maximum[i][j] - s->allocation[i][j];
        }
    }
    if (errors > 0) {
        CC_ERROR("%s: Allocation exceeds Maximum - state is invalid", path);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Printing helpers                                                    */
/* ------------------------------------------------------------------ */
static void fmt_vec(char *buf, size_t sz, const int *v, int m)
{
    size_t off = 0;
    off += (size_t)snprintf(buf + off, sz - off, "[");
    for (int j = 0; j < m && off < sz; j++)
        off += (size_t)snprintf(buf + off, sz - off, j ? " %d" : "%d", v[j]);
    if (off < sz)
        snprintf(buf + off, sz - off, "]");
}

static void print_matrices(const banker_state_t *s)
{
    static const char *titles[3] = { "Allocation", "Maximum", "Need" };
    int bw = s->m * 3 > 10 ? s->m * 3 : 10;   /* column-block width */

    printf("\n     ");
    for (int k = 0; k < 3; k++)
        printf("   %-*s", bw, titles[k]);
    printf("\n     ");
    for (int k = 0; k < 3; k++) {
        printf("   ");
        for (int j = 0; j < s->m; j++)
            printf("%3c", 'A' + j);
        printf("%*s", bw - s->m * 3, "");
    }
    printf("\n");
    for (int i = 0; i < s->n; i++) {
        printf("  P%-2d", i);
        for (int k = 0; k < 3; k++) {
            const int (*mat)[MAX_RES] = (k == 0) ? s->allocation :
                                        (k == 1) ? s->maximum : s->need;
            printf("   ");
            for (int j = 0; j < s->m; j++)
                printf("%3d", mat[i][j]);
            printf("%*s", bw - s->m * 3, "");
        }
        printf("\n");
    }
    printf("\n  Available:");
    for (int j = 0; j < s->m; j++)
        printf("%3d", s->available[j]);
    printf("   (resource types");
    for (int j = 0; j < s->m; j++)
        printf(" %c", 'A' + j);
    printf(")\n");
}

/* ------------------------------------------------------------------ */
/* The safety algorithm                                                */
/* ------------------------------------------------------------------ */
/* Returns 1 if safe, 0 if unsafe. seq[] receives the processes in the order
 * they finished (complete if safe, partial if unsafe); *nseq is its length. */
static int is_safe(const banker_state_t *s, int seq[], int *nseq)
{
    int work[MAX_RES], finished[MAX_PROC] = { 0 };
    int count = 0, pass = 0, progress;
    char a[64], b[64], c[64];

    memcpy(work, s->available, sizeof work);
    printf("\nSafety algorithm: Work = Available; scan P0..P%d in index order, "
           "repeat until no progress.\n", s->n - 1);

    do {
        progress = 0;
        pass++;
        printf("  Pass %d:\n", pass);
        for (int i = 0; i < s->n; i++) {
            int fits = 1;
            if (finished[i])
                continue;
            for (int j = 0; j < s->m; j++)
                if (s->need[i][j] > work[j]) {
                    fits = 0;
                    break;
                }
            fmt_vec(a, sizeof a, s->need[i], s->m);
            fmt_vec(b, sizeof b, work, s->m);
            if (!fits) {
                printf("    P%d: Need %s > Work %s  -> not enough, skip\n", i, a, b);
                continue;
            }
            for (int j = 0; j < s->m; j++)
                work[j] += s->allocation[i][j];
            fmt_vec(c, sizeof c, work, s->m);
            printf("    P%d: Need %s <= Work %s -> can finish, releases its "
                   "Allocation, Work = %s\n", i, a, b, c);
            finished[i] = 1;
            seq[count++] = i;
            progress = 1;
        }
    } while (progress && count < s->n);

    *nseq = count;
    return count == s->n;
}

/* ------------------------------------------------------------------ */
/* CLI: ./campus banker <state_file>                                   */
/* ------------------------------------------------------------------ */
int run_banker(int argc, char **argv)
{
    banker_state_t s;
    int seq[MAX_PROC], nseq = 0;

    if (argc != 3) {
        CC_ERROR(argc < 3 ? "banker: missing state file"
                          : "banker: too many arguments");
        fprintf(stderr,
                "usage: campus banker <file>\n"
                "  Example: ./campus banker tests/banker_safe.txt\n"
                "  Exit status: 0 = safe, 1 = unsafe, 2 = error\n");
        return BANKER_ERROR;
    }
    if (load_state(argv[2], &s) < 0)
        return BANKER_ERROR;

    printf("=== Banker's safety check ===\n");
    printf("State file : %s\n", argv[2]);
    printf("Processes  : %d, resource types: %d\n", s.n, s.m);
    printf("Need = Maximum - Allocation\n");
    print_matrices(&s);

    if (is_safe(&s, seq, &nseq)) {
        printf("\nRESULT: SAFE state. Safe sequence: <");
        for (int i = 0; i < nseq; i++)
            printf("%sP%d", i ? ", " : "", seq[i]);
        printf(">\n");
        return BANKER_SAFE;
    }

    /* unsafe: list the processes that could not be guaranteed to finish */
    int done[MAX_PROC] = { 0 }, first = 1;
    for (int i = 0; i < nseq; i++)
        done[seq[i]] = 1;
    printf("\nRESULT: UNSAFE state. No safe sequence exists.\n");
    if (nseq > 0) {
        printf("        Processes that could finish before getting stuck: ");
        for (int i = 0; i < nseq; i++)
            printf("%sP%d", i ? ", " : "", seq[i]);
        printf("\n");
    }
    printf("        Processes that can never be guaranteed to finish: ");
    for (int i = 0; i < s.n; i++) {
        if (!done[i]) {
            printf("%sP%d", first ? "" : ", ", i);
            first = 0;
        }
    }
    printf(" (possible deadlock)\n");
    return BANKER_UNSAFE;
}
