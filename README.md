# OS-mini-project_CampusCompute
# CampusCompute - Part B sections (paste into the shared README)

## Prerequisites
Ubuntu or WSL Ubuntu with `gcc` and `make` (`sudo apt install build-essential`).
Run all commands from the project folder (the one containing the Makefile).

## Build and clean
    make            # builds ./campus
    make clean      # removes ./campus and logs/*.log

## Commands (Part B)
| Command | What it demonstrates |
|---|---|
| `./campus logger [producers] [consumers] [items]` | Bounded producer-consumer queue (mutex + condition variables), log file `logs/campus.log`. Defaults 3 / 1 / 5. |
| `./campus ipc` | Writer and reader processes exchanging messages through POSIX shared memory and two named semaphores; objects are created, used and removed. |
| `./campus worker normal` | Three forked workers all exit with code 0; parent collects them with `waitpid()`. |
| `./campus worker fail` | Worker 2 SIMULATES a failure by exiting with code 42; parent detects it. |
| `./campus worker signal` | Worker 2 SIMULATES a failure by sending itself SIGTERM; parent detects it. |
| `./campus rlimit [nofile\|mem\|cpu\|all]` | `setrlimit()` demos, each inside a forked child. |
| `./campus all` | Runs every feature and prints a PASS/FAIL summary plus cleanup checks. |

All "failures" in the worker demos are simulated on purpose by the program; they are not real operating-system faults.

## Resource limits and their observed effect
- `RLIMIT_NOFILE` = 16: `open()` fails with `EMFILE` after the free descriptors are used up.
- `RLIMIT_AS` = 256 MB: `malloc()` returns NULL with `ENOMEM`.
- `RLIMIT_CPU` = 1 s: the kernel sends `SIGXCPU`; the child is terminated and the parent reports the signal.

## Cleanup
The program removes everything it creates:
- threads are joined before the logger returns;
- child processes are reaped with `waitpid()`;
- shared memory and semaphores use names containing the process ID (`/campus_..._<pid>`) and are unlinked before `ipc` exits;
- `logs/campus.log` is the only file left behind; remove it with `make clean`.

Manual check:

    ps -ef | grep [c]ampus
    ls /dev/shm | grep campus
