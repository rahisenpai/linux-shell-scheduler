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
#define MAX_SUBMIT 10

//struct to store commands info
struct Process{
    int pid, priority;
    bool submit,queue,completed;
    char command[MAX_SIZE + 1]; //+1 to accomodate \n or \0
    struct timeval start;
    unsigned long execution_time, wait_time;
};

struct history_struct {
    int history_count,ncpu,tslice;
    sem_t mutex;
    struct Process history[MAX_HISTORY];
};

//function declarations
void scheduler(int ncpu, int tslice);
static void my_handler(int signum);
void start_time(struct timeval *start);
unsigned long end_time(struct timeval *start);

//global variables
int shm_fd;
struct history_struct *process_table;

int main(){
    //signal part to handle ctrl c (from lecture 7)
    struct sigaction sig;
    if (memset(&sig, 0, sizeof(sig)) == 0){
        perror("memset");
        exit(1);
    }
    sig.sa_handler = my_handler;
    if (sigaction(SIGINT, &sig, NULL) == -1){
        perror("sigaction");
        exit(1);
    }
    shm_fd = shm_open("shm", O_RDWR, 0666);
    if (shm_fd == -1){
        perror("shm_open");
        exit(1);
    }
    process_table = mmap(NULL, sizeof(struct history_struct), PROT_READ|PROT_WRITE, MAP_SHARED, shm_fd,0);
    if (process_table == MAP_FAILED){
        perror("mmap");
        exit(1);
    }
    int ncpu = process_table->ncpu;
    int tslice = process_table->tslice;

    sem_init(&process_table->mutex, 1, 1);
    if(daemon(1, 1)){
        perror("daemon");
        exit(1);
    }

    scheduler(ncpu, tslice);

    if (munmap(process_table, sizeof(struct history_struct)) < 0){
        printf("Error unmapping\n");
        perror("munmap");
        exit(1);
    }
    close(shm_fd);
    return 0;
}

void scheduler(int ncpu, int tslice){
    while(true){
        sleep(tslice/1000);
    }
}

//signal handler
static void my_handler(int signum){
    if(signum == SIGINT){
        printf("\nCaught SIGINT signal for termination\n");
        printf("Terminating simple scheduler...\n");
        if (munmap(process_table, sizeof(struct history_struct)) < 0){
            printf("Error unmapping\n");
            perror("munmap");
            exit(1);
        }
        close(shm_fd);
        exit(0);
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