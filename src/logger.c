/*
 * logger.c - FR3: concurrent activity logger (Person B)
 *
 * A bounded circular queue is shared by producer threads (they "submit job"
 * events) and consumer threads (they write the events to logs/campus.log).
 *
 *   - one mutex protects the queue (head, tail, count, done flag)
 *   - condition variable not_full  : producers sleep here while the queue is full
 *   - condition variable not_empty : consumers sleep here while the queue is empty
 *   - every wait is inside a while-loop (guards against spurious wake-ups)
 *   - shutdown: after all producers are joined, 'done' is set and not_empty is
 *     broadcast; a consumer exits only when the queue is empty AND done is set,
 *     so nothing is lost.
 *
 * Usage: campus logger [producers 1-8] [consumers 1-4] [items-per-producer 1-100]
 */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "campus.h"

#define QCAP     5      /* small on purpose, so "full" and "empty" really happen */
#define TEXT_LEN 96

typedef struct {
    int    seq;
    int    producer;
    time_t ts;
    char   text[TEXT_LEN];
} LogMsg;

typedef struct {
    LogMsg slot[QCAP];
    int head, tail, count;
    int next_seq;       /* sequence numbers are handed out under the lock */
    int done;           /* set once every producer has finished */
    pthread_mutex_t lock;
    pthread_cond_t  not_full;
    pthread_cond_t  not_empty;
} BoundedQueue;

typedef struct { int id; int items; } ProducerArg;
typedef struct { int id; int consumed; } ConsumerArg;

static BoundedQueue Q;
static FILE *logfp;

/* ---------- queue operations ---------- */

static void queue_init(void)
{
    memset(&Q, 0, sizeof Q);
    pthread_mutex_init(&Q.lock, NULL);
    pthread_cond_init(&Q.not_full, NULL);
    pthread_cond_init(&Q.not_empty, NULL);
}

static void queue_destroy(void)
{
    pthread_cond_destroy(&Q.not_full);
    pthread_cond_destroy(&Q.not_empty);
    pthread_mutex_destroy(&Q.lock);
}

static void queue_push(LogMsg *m, int producer_id)
{
    pthread_mutex_lock(&Q.lock);
    while (Q.count == QCAP) {                       /* wait while FULL */
        printf("[producer-%d] queue FULL (%d/%d) -> waiting\n", producer_id, Q.count, QCAP);
        pthread_cond_wait(&Q.not_full, &Q.lock);
    }
    m->seq = ++Q.next_seq;
    Q.slot[Q.tail] = *m;
    Q.tail = (Q.tail + 1) % QCAP;
    Q.count++;
    printf("[producer-%d] produced seq=%d (queue %d/%d)\n", producer_id, m->seq, Q.count, QCAP);
    pthread_cond_signal(&Q.not_empty);
    pthread_mutex_unlock(&Q.lock);
}

/* Returns 1 and fills *out, or 0 when the queue is empty and production is over. */
static int queue_pop(LogMsg *out, int consumer_id)
{
    pthread_mutex_lock(&Q.lock);
    while (Q.count == 0 && !Q.done) {               /* wait while EMPTY */
        printf("[consumer-%d] queue EMPTY -> waiting\n", consumer_id);
        pthread_cond_wait(&Q.not_empty, &Q.lock);
    }
    if (Q.count == 0 && Q.done) {                   /* nothing left, never will be */
        pthread_mutex_unlock(&Q.lock);
        return 0;
    }
    *out = Q.slot[Q.head];
    Q.head = (Q.head + 1) % QCAP;
    Q.count--;
    printf("[consumer-%d] consumed seq=%d (queue %d/%d)\n", consumer_id, out->seq, Q.count, QCAP);
    pthread_cond_signal(&Q.not_full);
    pthread_mutex_unlock(&Q.lock);
    return 1;
}

static void queue_finish(void)
{
    pthread_mutex_lock(&Q.lock);
    Q.done = 1;
    pthread_cond_broadcast(&Q.not_empty);           /* wake every sleeping consumer */
    pthread_mutex_unlock(&Q.lock);
}

/* ---------- thread bodies ---------- */

static void *producer_thread(void *arg)
{
    ProducerArg *a = arg;
    for (int i = 1; i <= a->items; i++) {
        LogMsg m;
        memset(&m, 0, sizeof m);
        m.producer = a->id;
        m.ts = time(NULL);
        snprintf(m.text, TEXT_LEN, "job J%d%02d submitted by user%d", a->id, i, a->id);
        queue_push(&m, a->id);
    }
    return NULL;
}

static void write_log_entry(const LogMsg *m, int consumer_id)
{
    struct tm tmv;
    char stamp[32];
    localtime_r(&m->ts, &tmv);
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tmv);
    /* fprintf locks the FILE for the duration of one call, so lines never mix */
    fprintf(logfp, "%s [producer-%d] [consumer-%d] seq=%d %s\n",
            stamp, m->producer, consumer_id, m->seq, m->text);
    fflush(logfp);
}

static void *consumer_thread(void *arg)
{
    ConsumerArg *a = arg;
    LogMsg m;
    while (queue_pop(&m, a->id)) {
        write_log_entry(&m, a->id);
        a->consumed++;
        usleep(20000);          /* slow consumer: lets the queue fill up */
    }
    printf("[consumer-%d] finished, wrote %d entries\n", a->id, a->consumed);
    return NULL;
}

/* ---------- helpers ---------- */

static int parse_range(const char *s, int lo, int hi, int *out)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || *end != '\0' || end == s || v < lo || v > hi)
        return -1;
    *out = (int)v;
    return 0;
}

static int count_lines(const char *path)
{
    FILE *f = fopen(path, "r");
    if (f == NULL)
        return -1;
    int n = 0;
    char line[512];
    while (fgets(line, sizeof line, f) != NULL)
        n++;
    fclose(f);
    return n;
}

static int logger_usage(void)
{
    fprintf(stderr, "usage: campus logger [producers 1-8] [consumers 1-4] [items 1-100]\n");
    return 1;
}

/* ---------- command entry point ---------- */

int run_logger(int argc, char **argv)
{
    int np = 3, nc = 1, items = 5;

    if (argc > 5)
        return logger_usage();
    if (argc > 2 && parse_range(argv[2], 1, 8, &np) != 0) {
        fprintf(stderr, "logger: invalid producer count '%s'\n", argv[2]);
        return logger_usage();
    }
    if (argc > 3 && parse_range(argv[3], 1, 4, &nc) != 0) {
        fprintf(stderr, "logger: invalid consumer count '%s'\n", argv[3]);
        return logger_usage();
    }
    if (argc > 4 && parse_range(argv[4], 1, 100, &items) != 0) {
        fprintf(stderr, "logger: invalid item count '%s'\n", argv[4]);
        return logger_usage();
    }

    if (mkdir("logs", 0755) == -1 && errno != EEXIST) {
        perror("logger: mkdir logs");
        return 1;
    }
    logfp = fopen(LOG_PATH, "w");
    if (logfp == NULL) {
        perror("logger: fopen " LOG_PATH);
        return 1;
    }

    pthread_t   *pt = calloc((size_t)np, sizeof *pt);
    pthread_t   *ct = calloc((size_t)nc, sizeof *ct);
    ProducerArg *pa = calloc((size_t)np, sizeof *pa);
    ConsumerArg *ca = calloc((size_t)nc, sizeof *ca);
    if (!pt || !ct || !pa || !ca) {
        perror("logger: calloc");
        free(pt); free(ct); free(pa); free(ca);
        fclose(logfp);
        return 1;
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    queue_init();
    printf("Logger: %d producer(s), %d consumer(s), %d item(s) each, queue capacity %d\n",
           np, nc, items, QCAP);

    int failed = 0, pstarted = 0, cstarted = 0;

    /* consumers first, so the "queue EMPTY" wait is visible */
    for (int i = 0; i < nc; i++) {
        ca[i].id = i + 1;
        int rc = pthread_create(&ct[i], NULL, consumer_thread, &ca[i]);
        if (rc != 0) {
            fprintf(stderr, "logger: pthread_create(consumer): %s\n", strerror(rc));
            failed = 1;
            break;
        }
        cstarted++;
    }
    for (int i = 0; i < np && !failed; i++) {
        pa[i].id = i + 1;
        pa[i].items = items;
        int rc = pthread_create(&pt[i], NULL, producer_thread, &pa[i]);
        if (rc != 0) {
            fprintf(stderr, "logger: pthread_create(producer): %s\n", strerror(rc));
            failed = 1;
            break;
        }
        pstarted++;
    }

    /* orderly termination: producers first, then tell consumers no more is coming */
    for (int i = 0; i < pstarted; i++)
        pthread_join(pt[i], NULL);
    printf("All producers joined.\n");
    queue_finish();
    for (int i = 0; i < cstarted; i++)
        pthread_join(ct[i], NULL);
    printf("All consumers joined.\n");

    int produced = Q.next_seq, consumed = 0;
    for (int i = 0; i < cstarted; i++)
        consumed += ca[i].consumed;

    fclose(logfp);
    int lines = count_lines(LOG_PATH);
    int expected = np * items;

    printf("Summary: expected=%d produced=%d consumed=%d log_lines=%d  -> %s\n",
           expected, produced, consumed, lines,
           (!failed && produced == expected && consumed == expected && lines == expected)
               ? "PASS" : "FAIL");
    printf("Log file: %s\n", LOG_PATH);

    int ok = !failed && produced == expected && consumed == expected && lines == expected;
    queue_destroy();
    free(pt); free(ct); free(pa); free(ca);
    return ok ? 0 : 1;
}
