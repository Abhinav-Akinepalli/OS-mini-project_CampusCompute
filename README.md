# CampusCompute - Intelligent Job Scheduling and Resource Management

UE25CS242A - Operating Systems, Level 3 Mini-Project

Team:
- Person A: A B Sharvesh - PES1UG25CS001
- Person B: Abhinav Akinepalli - PES1UG25CS013

CampusCompute is a command-line prototype, written in C for Linux, that
demonstrates CPU scheduling, a concurrent logger, inter-process communication,
deadlock avoidance, worker-failure detection and process resource limits.

## Prerequisites
- Ubuntu or WSL Ubuntu
- gcc and make:  sudo apt install build-essential

## Quick start
    make
    ./campus help
    ./campus all      # runs every feature and prints a PASS/FAIL summary

## Build and clean
    make          # builds ./campus
    make clean    # removes ./campus and logs/*.log

## Commands
    ./campus help                                              usage
    ./campus sched fcfs|rr|priority|compare <jobfile> [-q N]   CPU scheduling (FR2)
    ./campus logger [producers] [consumers] [items]            producer-consumer logger (FR3)
    ./campus ipc                                               shared memory + named semaphores (FR4)
    ./campus banker <file>                                     Banker's safety algorithm (FR5)
    ./campus worker normal|fail|signal                         fork/waitpid worker detection (FR6)
    ./campus rlimit [nofile|mem|cpu|all]                       setrlimit demonstrations (FR7)
    ./campus all                                               integrated demo with cleanup checks

Invalid commands or arguments print an error and a usage message and return a
non-zero exit status. Each command can be run on its own.

## Input file formats
- Jobs file, one job per line:  id arrival burst priority
- Banker file: 'n m', then Available, then Allocation (n rows), then Max (n rows)
- Sample data is in tests/ (jobs.txt, jobs_bad.txt, banker_safe.txt, banker_unsafe.txt)

## Design decisions
- Tie-break rule: earlier arrival first, then smaller job ID
- Round Robin time quantum: 2 by default; change it with -q N
- Priority scheduling: lower number = higher priority, non-preemptive
- Banker's exit status: 0 = safe, 1 = unsafe, 2 = input error
- Logger: bounded queue of 5 entries, one mutex, and two condition variables
  (not_full, not_empty). Producers are joined first, then the consumers are told
  that production is finished, so no entry is lost.
- IPC: a writer and a forked reader pass messages through one POSIX shared-memory
  slot, coordinated by two named semaphores (empty and full).

## Simulated failures
The failures in 'worker fail' and 'worker signal' are triggered on purpose by the
program (exit code 42, or the worker sends itself SIGTERM). They are simulations,
not real operating-system failures. The parent detects them with waitpid().

## Resource limits
Each demo runs in a forked child, so the parent is not affected.
- RLIMIT_NOFILE = 16: open() fails with EMFILE once the free descriptors are used up
- RLIMIT_AS = 256 MB: malloc() returns NULL with ENOMEM
- RLIMIT_CPU = 1 s: the child receives SIGXCPU and is terminated

## Output files and cleanup
- The logger writes logs/campus.log.
- Threads are joined, child processes are reaped, and the shared memory and
  semaphores (named with the process ID) are unlinked before the program exits.
- Only logs/campus.log is left behind; 'make clean' removes it.
- Manual check that nothing is left running or allocated:
      ps -ef | grep '[c]ampus'
      ls /dev/shm | grep campus

## Files
- src/main.c: command-line parsing and dispatch
- src/job.c, src/sched.c: job input and CPU scheduling
- src/banker.c: Banker's safety algorithm
- src/logger.c: producer-consumer logger
- src/ipc.c: shared memory and named semaphores
- src/worker.c: fork/waitpid worker demos
- src/rlimit.c: setrlimit demos
- src/integrate.c, src/sysutil.c: integrated run and helper functions
- include/: header files
- tests/: repeatable input files
- logs/: log output
