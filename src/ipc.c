/*
 * ipc.c - FR4: POSIX shared memory + named semaphores (Person B)
 *
 * Two cooperating processes (parent = writer, forked child = reader) pass
 * "job summary" messages through ONE shared-memory slot.
 *
 *   sem_empty (initial 1): slot is free, writer may fill it
 *   sem_full  (initial 0): slot holds a message, reader may take it
 *
 * Writer: wait(empty) -> fill slot -> post(full)
 * Reader: wait(full)  -> read slot -> post(empty)
 * After N messages the writer sends a slot with done=1.
 *
 * Every object name contains the parent's PID, so we can never collide with
 * (or delete) anybody else's IPC objects.  The child only closes/unmaps; ONLY
 * the parent unlinks, exactly once, in cleanup.
 *
 * Usage: campus ipc
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "campus.h"
#include "sysutil.h"

#define N_MSGS       5
#define SEM_TIMEOUT  5      /* seconds; prevents a hang if the peer dies */

typedef struct {
    int  seq;
    int  done;
    char text[96];
} Slot;

typedef struct {
    char shm[64];
    char empty[64];
    char full[64];
} IpcNames;

/* sem_wait with a timeout, restarted on EINTR. Returns 0 or -1 (errno set). */
static int sem_wait_timeout(sem_t *s)
{
    struct timespec dl;
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_sec += SEM_TIMEOUT;
    while (sem_timedwait(s, &dl) == -1) {
        if (errno != EINTR)
            return -1;
    }
    return 0;
}

/* Runs in the child: opens the objects BY NAME (a genuinely separate process). */
static int reader_process(const IpcNames *nm)
{
    int status = 0;
    int fd = shm_open(nm->shm, O_RDWR, 0);
    if (fd == -1) {
        perror("[reader] shm_open");
        return 1;
    }
    Slot *slot = mmap(NULL, sizeof(Slot), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);                                  /* the mapping stays valid */
    if (slot == MAP_FAILED) {
        perror("[reader] mmap");
        return 1;
    }
    sem_t *empty = sem_open(nm->empty, 0);
    sem_t *full  = sem_open(nm->full, 0);
    if (empty == SEM_FAILED || full == SEM_FAILED) {
        perror("[reader] sem_open");
        munmap(slot, sizeof(Slot));
        return 1;
    }

    int expected = 1, received = 0;
    for (;;) {
        if (sem_wait_timeout(full) == -1) {
            perror("[reader] sem_wait(full)");
            status = 1;
            break;
        }
        if (slot->done)
            break;
        printf("[reader pid %d] received seq=%d: %s\n", (int)getpid(), slot->seq, slot->text);
        fflush(stdout);
        if (slot->seq != expected) {
            fprintf(stderr, "[reader] out-of-order message (got %d, wanted %d)\n",
                    slot->seq, expected);
            status = 1;
        }
        expected++;
        received++;
        sem_post(empty);
    }
    printf("[reader pid %d] done: received %d message(s)\n", (int)getpid(), received);
    fflush(stdout);
    if (received != N_MSGS)
        status = 1;

    sem_close(empty);
    sem_close(full);
    munmap(slot, sizeof(Slot));
    return status;
}

static void report_objects(const char *when, int expect)
{
    int n = count_own_ipc_objects();
    if (n < 0)
        printf("IPC objects %-14s: (/dev/shm not readable here, check skipped)\n", when);
    else
        printf("IPC objects %-14s: %d (expected %d)\n", when, n, expect);
}

int run_ipc(int argc, char **argv)
{
    (void)argv;
    if (argc > 2) {
        fprintf(stderr, "usage: campus ipc\n");
        return 1;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);

    IpcNames nm;
    int me = (int)getpid();
    snprintf(nm.shm,   sizeof nm.shm,   "/campus_shm_%d", me);
    snprintf(nm.empty, sizeof nm.empty, "/campus_sem_empty_%d", me);
    snprintf(nm.full,  sizeof nm.full,  "/campus_sem_full_%d", me);

    int     rc = 1, fd = -1, child_reaped = 1, child_ok = 0;
    int     shm_made = 0, empty_made = 0, full_made = 0;
    Slot   *slot = NULL;
    sem_t  *empty = SEM_FAILED, *full = SEM_FAILED;
    pid_t   child = -1;

    report_objects("(before)", 0);

    /* start from a clean slate: remove stale objects with OUR OWN names only */
    shm_unlink(nm.shm);
    sem_unlink(nm.empty);
    sem_unlink(nm.full);

    fd = shm_open(nm.shm, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd == -1) { perror("ipc: shm_open"); goto cleanup; }
    shm_made = 1;
    if (ftruncate(fd, sizeof(Slot)) == -1) { perror("ipc: ftruncate"); goto cleanup; }
    slot = mmap(NULL, sizeof(Slot), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    fd = -1;
    if (slot == MAP_FAILED) { slot = NULL; perror("ipc: mmap"); goto cleanup; }
    memset(slot, 0, sizeof *slot);

    empty = sem_open(nm.empty, O_CREAT | O_EXCL, 0600, 1);
    if (empty == SEM_FAILED) { perror("ipc: sem_open(empty)"); goto cleanup; }
    empty_made = 1;
    full = sem_open(nm.full, O_CREAT | O_EXCL, 0600, 0);
    if (full == SEM_FAILED) { perror("ipc: sem_open(full)"); goto cleanup; }
    full_made = 1;

    printf("Created shared memory %s and semaphores %s, %s\n", nm.shm, nm.empty, nm.full);
    report_objects("(running)", 3);

    fflush(NULL);                               /* no duplicated stdio buffers in the child */
    child = fork();
    if (child == -1) { perror("ipc: fork"); goto cleanup; }
    if (child == 0)
        _exit(reader_process(&nm));             /* child never unlinks anything */
    child_reaped = 0;

    /* ---- writer side (parent) ---- */
    int write_ok = 1;
    for (int i = 1; i <= N_MSGS && write_ok; i++) {
        if (sem_wait_timeout(empty) == -1) { perror("[writer] sem_wait(empty)"); write_ok = 0; break; }
        slot->seq = i;
        slot->done = 0;
        snprintf(slot->text, sizeof slot->text, "job J%d: burst=%d priority=%d",
                 i, 2 + (i * 3) % 7, 1 + (i * 5) % 4);
        printf("[writer pid %d] wrote   seq=%d: %s\n", me, slot->seq, slot->text);
        sem_post(full);
    }
    if (write_ok && sem_wait_timeout(empty) == 0) {
        slot->done = 1;                         /* tell the reader we are finished */
        sem_post(full);
    } else {
        write_ok = 0;
    }

    if (!write_ok)
        kill(child, SIGTERM);                   /* don't leave a stuck reader behind */
    int st = 0;
    if (waitpid(child, &st, 0) == -1) {
        perror("ipc: waitpid");
    } else {
        char desc[96];
        describe_status(st, desc, sizeof desc);
        printf("[parent] reader process %d %s\n", (int)child, desc);
        child_ok = write_ok && WIFEXITED(st) && WEXITSTATUS(st) == 0;
    }
    child_reaped = 1;

cleanup:
    if (child > 0 && !child_reaped) {           /* error path: never leave a child running */
        kill(child, SIGTERM);
        waitpid(child, NULL, 0);
    }
    if (fd != -1)
        close(fd);
    if (slot != NULL)
        munmap(slot, sizeof(Slot));
    if (full != SEM_FAILED)
        sem_close(full);
    if (empty != SEM_FAILED)
        sem_close(empty);
    if (full_made)
        sem_unlink(nm.full);
    if (empty_made)
        sem_unlink(nm.empty);
    if (shm_made)
        shm_unlink(nm.shm);
    printf("Cleanup done: unmapped, closed and unlinked our shared memory and semaphores.\n");

    int after = count_own_ipc_objects();
    report_objects("(after)", 0);
    if (child_ok && (after == 0 || after == -1))
        rc = 0;
    printf("IPC result: %s\n", rc == 0 ? "PASS" : "FAIL");
    return rc;
}
