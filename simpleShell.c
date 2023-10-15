//header files
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>

//definitions
#define MAX_SIZE 50
#define MAX_HISTORY 75
#define MAX_WORDS 10
#define MAX_COMMANDS 5

//struct to store commands info
struct Process{
    int pid;
    bool submit,queue,completed;
    char command[MAX_SIZE + 1]; //+1 to accomodate \n or \0
    struct timeval start;
    unsigned long execution_time, wait_time;;
};

struct history_struct {
    int history_count,ncpu,tslice;
    sem_t mutex;
    struct Process history[MAX_HISTORY];
};

//function declarations
static void sigint_handler(int signum);
static void sigchld_handler(int signum, siginfo_t *info, void *context);
void termination_report();
void shell_loop();
char* read_user_input();
int launch(char* command);
int create_process_and_run(char* command);
int create_child_process(char *command, int input_fd, int output_fd);
void start_time(struct timeval *start);
unsigned long end_time(struct timeval *start);
int submit_process(char *command);

//global variables
int shm_fd, scheduler_pid;
struct history_struct *process_table;

int main(int argc, char** argv){
    if (argc != 3){
        printf("Usage: %s <NCPU> <TIME_QUANTUM>\n",argv[0]);
        exit(1);
    }

    shm_fd = shm_open("shm", O_CREAT|O_RDWR, 0666);
    if (shm_fd == -1){
        perror("shm_open");
        exit(1);
    }
    if (ftruncate(shm_fd, sizeof(struct history_struct)) == -1){
        perror("ftruncate");
        exit(1);
    }
    process_table = mmap(NULL, sizeof(struct history_struct), PROT_READ|PROT_WRITE, MAP_SHARED, shm_fd,0);
    if (process_table == MAP_FAILED){
        perror("mmap");
        exit(1);
    }
    process_table->history_count=0;
    process_table->ncpu = atoi(argv[1]);
    if (process_table->ncpu == 0){
        printf("invalid argument for number of CPU\n");
        exit(1);
    }
    process_table->tslice = atoi(argv[2]);
    if (process_table->tslice == 0){
        printf("invalid argument for time quantum\n");
        exit(1);
    }
    sem_init(&process_table->mutex, 1, 1);

    printf("Initializing simple scheduler...\n");
    pid_t pid;
    if ((pid= fork())<0){
        printf("fork() failed.\n");
        perror("fork");
        exit(1);
    }
    if (pid == 0){
        if (execvp("./scheduler",("./scheduler",NULL)) == -1) {
            printf("Couldn't initiate scheduler.\n");
            exit(1);
        }
        if (munmap(process_table, sizeof(struct history_struct)) < 0){
            printf("Error unmapping\n");
            perror("munmap");
            exit(1);
        }
        close(shm_fd);
        exit(0);
    }
    else{
        scheduler_pid = pid;
    }

    //signal part to handle ctrl c (from lecture 7)
    struct sigaction s_int;
    if (memset(&s_int, 0, sizeof(s_int)) == 0){
        perror("memset");
        exit(1);
    }
    s_int.sa_handler = sigint_handler;
    if (sigaction(SIGINT, &s_int, NULL) == -1){
        perror("sigaction");
        exit(1);
    }

    printf("Initializing simple shell...\n");
    shell_loop();
    printf("Exiting simple shell...\n");
    termination_report();

    sem_destroy(&process_table->mutex);
    if (munmap(process_table, sizeof(struct history_struct)) < 0){
        printf("Error unmapping\n");
        perror("munmap");
        exit(1);
    }
    close(shm_fd);
    shm_unlink("shm");
    return 0;
}

//signal handler
static void sigint_handler(int signum) {
    if(signum == SIGINT) {
        printf("\nCaught SIGINT signal for termination\n");
        printf("Terminating simple scheduler\n");
        kill(scheduler_pid, SIGINT);
        printf("Exiting simple shell...\n");
        termination_report();
        sem_destroy(&process_table->mutex);
        if (munmap(process_table, sizeof(struct history_struct)) < 0){
            printf("Error unmapping\n");
            perror("munmap");
            exit(1);
        }
        close(shm_fd);
        shm_unlink("shm");
        exit(0);
    }
}

//the function called upon termination to print command details
//in here we are formatting time and printing iterating over the global array
void termination_report(){
    sem_wait(&process_table->mutex);
    if (process_table->history_count>0){
        //PID is -1 if a command was not executed through process creation
        printf("\nCommand  PID  Execution_time Waiting_time\n");
        for (int i=0; i<process_table->history_count; i++){
            printf("%s  %d  %ldms  %ldms\n",process_table->history[i].command,process_table->history[i].pid,process_table->history[i].execution_time,process_table->history[i].wait_time);
        }
    }
    sem_post(&process_table->mutex);
}

//infinite loop for the shell
//we take user input, pass it over to launch and wait for execution and update time fields
void shell_loop(){
    int status;
    do{
        //this prints the output in magenta colour
        printf("\033[1;35mos@shell:~$\033[0m ");
        char* command = read_user_input();
        sem_wait(&process_table->mutex);
        process_table->history[process_table->history_count].pid = -1;
        process_table->history[process_table->history_count].submit = false;
        process_table->history[process_table->history_count].wait_time = process_table->history[process_table->history_count].execution_time = 0;
        start_time(&process_table->history[process_table->history_count].start);
        sem_post(&process_table->mutex);
        status = launch(command);
        sem_wait(&process_table->mutex);
        if(!process_table->history[process_table->history_count].submit){
            process_table->history[process_table->history_count].execution_time = end_time(&process_table->history[process_table->history_count].start);
        }
        process_table->history_count++;
        sem_post(&process_table->mutex);
    } while(status);
}

//here we take input and remove trailing \n and update global array
char* read_user_input(){
    static char input[MAX_SIZE+1];
    if (fgets(input,MAX_SIZE+1,stdin) == NULL){
        perror("fgets");
        exit(1);
    }
    int input_len = strlen(input);
    if (input_len>0 && input[input_len-1]=='\n'){
        input[input_len-1] = '\0';
    }
    sem_wait(&process_table->mutex);
    strcpy(process_table->history[process_table->history_count].command,input);
    sem_post(&process_table->mutex);
    return input;
}

//here we execute custom commands which dont require process creation and their pids are -1
int launch(char* command){
    //removing leading whitespaces
    while(*command==' ' || *command=='\t'){
        command++;
    }
    //removing trailing whitespaces
    char *end = command + strlen(command)-1;
    while (end>=command && (*end==' ' || *end=='\t')){
        *end = '\0';
        end--;
    }
    sem_wait(&process_table->mutex);
    if (strncmp(command, "submit", 6) == 0) {
        // Check if the priority is specified
        process_table->history[process_table->history_count].submit = true;
        process_table->history[process_table->history_count].completed = false;
        process_table->history[process_table->history_count].pid = submit_process(command);
        start_time(&process_table->history[process_table->history_count].start);
        sem_post(&process_table->mutex);
        return 1;
    }

    if (strcmp(command,"history") == 0){
        for (int i=0; i<process_table->history_count+1; i++){
            printf("%s\n",process_table->history[i].command);
        }
        sem_post(&process_table->mutex);
        return 1;
    }
    if (strcmp(command, "") == 0){
        process_table->history_count--;
        sem_post(&process_table->mutex);
        return 1;
    }
    if (strcmp(command,"exit") == 0){
        sem_post(&process_table->mutex);
        return 0;
    }
    sem_post(&process_table->mutex);
    int status;
    status = create_process_and_run(command);
    return status;
}

//here we check for the pipe and & in the commands and create child process after that in other function
int create_process_and_run(char* command){
    //separating pipe commands (|)
    int command_count = 0;
    char* commands[MAX_COMMANDS];
    char* token = strtok(command, "|");
    while (token != NULL){
        commands[command_count++] = token;
        token = strtok(NULL, "|");
    }
    if (command_count>MAX_COMMANDS){
        printf("you have used more than 4 pipes, try again");
        return 1;
    }

    //executing if pipe is present in command input except the last one 
    int i, prev_read = STDIN_FILENO;
    int pipes[2], child_pids[command_count];
    //we iterate and execute every command through process creation and keep updating read and write ends of pipe
    for (i=0; i < command_count-1; i++){
        if (pipe(pipes) == -1){
            perror("pipe");
            exit(1);
        }

        if ((child_pids[i]=create_child_process(commands[i], prev_read, pipes[1])) < 0){
            perror("create_child_process");
            exit(1);
        }

        if (close(pipes[1]) == -1){
            perror("close");
            exit(1);
        }
        prev_read = pipes[0];
    }

    //the last command whose output is to be displayed on STDOUT
    //checking if it a background process
    bool background_process = 0;
    if (commands[i][strlen(commands[i]) - 1] == '&') {
        // Remove the '&' symbol
        commands[i][strlen(commands[i]) - 1] = '\0';
        background_process = 1;
    }
    if ((child_pids[i]=create_child_process(commands[i], prev_read, STDOUT_FILENO)) < 0){
        perror("create_child_process");
        exit(1);
    }
    
    //updating global array for pids
    sem_wait(&process_table->mutex);
    process_table->history[process_table->history_count].pid = child_pids[i];
    sem_post(&process_table->mutex);

    if (!background_process) {
        //wait for child process if command is not background
        for (i = 0; i < command_count; i++) {
            int ret;
            int pid = waitpid(child_pids[i], &ret, 0);
            if (pid < 0) {
                perror("waitpid");
                exit(1);
            }
            if (!WIFEXITED(ret)){
                printf("Abnormal termination of %d\n", pid);
            }
        }
    }
    else{
        //print pid and command if it is being executed in background
        printf("%d %s\n", child_pids[command_count-1],command);
    }
}

int create_child_process(char *command, int input_fd, int output_fd){
    int status = fork();
    if (status < 0){
        printf("fork() failed.\n");
        exit(1);
    }
    else if (status == 0){
        //child process
        //updating/copying I/O descriptors
        if (input_fd != STDIN_FILENO)
        {
            if (dup2(input_fd, STDIN_FILENO) == -1){
                perror("dup2");
                exit(1);
            }
            if (close(input_fd) == -1){
                perror("close");
                exit(1);
            }
        }
        if (output_fd != STDOUT_FILENO)
        {
            if (dup2(output_fd, STDOUT_FILENO) == -1){
                perror("dup2");
                exit(1);
            }
            if (close(output_fd) == -1){
                perror("close");
                exit(1);
            }
        }

        //creating an array of indiviudal command and its arguments
        char* arguments[MAX_WORDS+1]; //+1 to accomodate NULL
        int argument_count = 0;
        char* token = strtok(command, " ");
        while (token != NULL){
            arguments[argument_count++] = token;
            token = strtok(NULL, " ");
        }
        arguments[argument_count] = NULL;

        //exec to execute command (actual part of child process)
        if (execvp(arguments[0],arguments) == -1) {
            perror("execvp");
            printf("Not a valid/supported command.\n");
            exit(1);
        }
        exit(0);
    }
    else{
        //parent process
        return status;
    }
}

void start_time(struct timeval *start){
  gettimeofday(start, 0);
}

unsigned long end_time(struct timeval *start){
  struct timeval end;
  unsigned long t;

  gettimeofday(&end, 0);
  t = ((end.tv_sec*1000000) + end.tv_usec) - ((start->tv_sec*1000000) + start->tv_usec);
  return t/1000;
}

int submit_process(char *command){
    int priority, status;
    //creating an array of indiviudal command and its arguments
    char* arguments[MAX_WORDS+1]; //+1 to accomodate NULL
    int argument_count = 0;
    char* token = strtok(command, " "); //remove submit keyword from command
    token = strtok(NULL, " ");
    while (token != NULL){
        arguments[argument_count++] = token;
        token = strtok(NULL, " ");
    }
    arguments[argument_count] = NULL;
    status = fork();
    if (status < 0){
        printf("fork() failed.\n");
        exit(1);
    }
    else if (status == 0){
        //exec to execute command (actual part of child process)
        if (execvp(arguments[0],arguments) == -1) {
            perror("execvp");
            printf("Not a valid/supported command.\n");
            exit(1);
        }
        exit(0);
    }
    else{
        //parent process returns pid
        kill(status, SIGSTOP);
        return status;
    }
}