#ifndef SYSUTIL_H
#define SYSUTIL_H

#include <stddef.h>

/* Turn a waitpid() status into readable text.
 * Returns 0 = exited with code 0, 1 = exited with non-zero code,
 *         2 = killed by a signal, 3 = unknown. */
int describe_status(int status, char *buf, size_t n);

/* Number of entries in /dev/shm that belong to THIS process
 * (names contain "campus_" and end in "_<our pid>").  -1 if /dev/shm is unreadable. */
int count_own_ipc_objects(void);

/* Number of threads in this process (from /proc/self/status).  -1 if unavailable. */
int current_thread_count(void);

#endif
