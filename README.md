# Linux Shell and Scheduler
## Instructions
1) The shell code is in `simpleShell.c` and scheduler code is in `simpleScheduler.c`.
2) Use `make` on your Linux terminal to compile the programs with appropriate flags present as a command in `MakeFile`.
3) Run the shell with `./shell <NCPU> <TIME_QUANTUM>`, where NCPU is the number of CPUs available to run processes simultaneously and TIME_QUANTUM is the time slice for Round-Robin scheduling policy.
4) The files `fib.c`, `p1.c`, `p2.c` and `p3.c` are simple programs which take an execution time of about 5 seconds, intended to test the shell and scheduler.
## Shell
### Explanation
We have implemented a SimpleShell that waits for user input, executes commands provided by user including commands involving pipes, background processes and shell scripts and then repeats this 2-phase execution until terminated using ctrl-c.  
The `main` function initializes the signal handler (for this we have declared a function which sets up a signal handler for `ctrl+C` (SIGINT) to terminate the code) and enters into the shell loop, in which there is an infinite loop where the shell continuously reads the user input (using the `read_user_input` function which removes the trailing '\n' character), processes commands in `launch` and `create_process_and_run`, and waits for the command execution in `create_child_process` to complete.  
It updates the command history with execution details. At the end during termination the `termination_report` function prints a summary of executed commands, PIDs, start, end and execution times.
### Commands which cannot be executed (this list is not exhaustive)
1) cd - this command changes the directory over the terminal to essentially modify the internal state of the shell, so a running c program cannot change its directory during execution.
2) export - this command is used to set environment variables which are internal settings of the shell, so it cant be executed in the simple-shell.
3) unset - this command works very similar to export, the difference being it removes environment variables, so its execution is not possible in simple-shell.
### Limitations
We have only used static memory, so there are certain restrictions over input size (200), number of pipes (9) in a single prompt, number of words (50) in a prompt and maximum number of history records (100) in a single execution. Also we have implemented ‘&’ for background processes and not as command separator and ‘&’ can be used with pipes, so no problems with that.
## Scheduler
We have used shared memory to communicate between shell and scheduler processes. Scheduler is launched when you launch the shell. We have shared the `history` array (contains everything related to a process) between processes and used the kill API to send SIGCONT and SIGSTOP signals to processes with their PIDs after a time quantum (which is taken as input in milliseconds).  
Ready queue is a priority queue and running queue is a normal queue. For scheduling policy we have implemented a simple (naive) version of linux CFS, where we run a process from the ready queue till the specified tslice. We considered vruntime to be the comparing attribute and extract the processes with minimum vruntime to enqueue in the running queue and number of maximum processes in the running queue is also taken as input. Execution time is the CPU burst time of a process. We have used sempahores every time we access shm so it can affect time due to sem_wait API.
