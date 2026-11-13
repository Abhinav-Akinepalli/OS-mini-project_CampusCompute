#ifndef CAMPUS_H
#define CAMPUS_H

/*
 * One entry point per feature.  argc/argv are main()'s own, unchanged:
 *   argv[0] = "campus", argv[1] = command name, extra arguments from argv[2].
 *
 * Return-code contract (agreed by Person A and Person B):
 *   run_banker : 0 = state is SAFE, 1 = state is UNSAFE, 2 = error (bad input)
 *   all others : 0 = success, non-zero = failure
 */
int run_sched(int argc, char **argv);    /* Person A */
int run_banker(int argc, char **argv);   /* Person A */
int run_logger(int argc, char **argv);   /* Person B */
int run_ipc(int argc, char **argv);      /* Person B */
int run_worker(int argc, char **argv);   /* Person B */
int run_rlimit(int argc, char **argv);   /* Person B */
int run_all(int argc, char **argv);      /* Person B */

#define LOG_PATH "logs/campus.log"

#endif
