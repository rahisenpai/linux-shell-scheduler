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
#define MAX_SUBMIT 20

//struct to store commands info
struct Process{
    int pid, priority;
    bool submit,queue,completed;
    char command[MAX_SIZE + 1]; //+1 to accomodate \n or \0
    struct timeval start;
    unsigned long execution_time, wait_time, vruntime;
};

struct history_struct {
    int history_count,ncpu,tslice;
    sem_t mutex;
    struct Process history[MAX_HISTORY];
};

struct queue{
    int head,tail,capacity,curr;
    struct Process **table;
};

struct pqueue{
    int size,capacity;
    struct Process **heap;
};

//function declarations
void scheduler(int ncpu, int tslice);
static void my_handler(int signum);
void start_time(struct timeval *start);
unsigned long end_time(struct timeval *start);
bool queue_empty(struct queue *q);
int next_head(struct queue *q);
int next_tail(struct queue *q);
bool queue_full(struct queue *q);
void enqueue(struct queue *q, struct Process *proc);
void dequeue(struct queue *q);
bool pqueue_empty(struct pqueue *pq);
bool pqueue_full(struct pqueue *pq);
void swap(struct Process* a, struct Process* b);
void heapifyUp(struct pqueue* pq, int index);
void heapifyDown(struct pqueue* pq, int index);
void penqueue(struct pqueue *pq, struct Process *proc);
struct Process* pdequeue(struct pqueue *pq);

//global variables
int shm_fd;
struct history_struct *process_table;
struct queue *running_q;
struct pqueue *ready_q;

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

    ready_q = (struct pqueue *) (malloc(sizeof(struct pqueue)));
    ready_q->size = 0;
    ready_q->capacity = MAX_SUBMIT;
    ready_q->heap = (struct Process **) malloc(ready_q->capacity * sizeof(struct Process));
    for (int i = 0; i < ready_q->capacity; i++) {
        ready_q->heap[i] = (struct Process *)malloc(sizeof(struct Process));
    }
    running_q = (struct queue *) (malloc(sizeof(struct queue)));
    running_q->head = running_q->tail = running_q->curr = 0;
    running_q->capacity = ncpu+1;
    running_q->table = (struct Process **) malloc(running_q->capacity * sizeof(struct Process));
    for (int i = 0; i < running_q->capacity; i++) {
        running_q->table[i] = (struct Process *)malloc(sizeof(struct Process));
    }

    sem_init(&process_table->mutex, 1, 1);
    if(daemon(1, 1)){
        perror("daemon");
        exit(1);
    }

    scheduler(ncpu, tslice);

    free(running_q->table);
    free(running_q);
    free(ready_q->heap);
    free(ready_q);
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
        sem_wait(&process_table->mutex);
        for (int i=0; i<process_table->history_count; i++){
            if (process_table->history[i].submit==true && process_table->history[i].completed==false && process_table->history[i].queue==false){
                if (ready_q->size+ncpu < ready_q->capacity-1){
                    process_table->history[i].queue=true;
                    penqueue(ready_q, &process_table->history[i]);
                }
                else{
                    break;
                }
            }
        }
        if (!queue_empty(running_q)){
            for (int i=0; i<ncpu; i++){
                if (!queue_empty(running_q)){
                    if (!running_q->table[running_q->head]->completed){
                        penqueue(ready_q, running_q->table[running_q->head]);
                        running_q->table[running_q->head]->execution_time += end_time(&running_q->table[running_q->head]->start);
                        running_q->table[running_q->head]->vruntime += running_q->table[running_q->head]->execution_time *running_q->table[running_q->head]->priority;
                        kill(running_q->table[running_q->head]->pid, SIGSTOP);
                        start_time(&running_q->table[running_q->head]->start);
                        dequeue(running_q);
                    }
                    else{
                        dequeue(running_q);
                    }
                }
            }
        }
        if (!pqueue_empty(ready_q)){
            for (int i=0; i<ncpu; i++){
                if (!pqueue_empty(ready_q)){
                    struct Process *proc = pdequeue(ready_q);
                    enqueue(running_q, proc);
                    running_q->table[running_q->tail]->wait_time += end_time(&running_q->table[running_q->tail]->start);
                    kill(proc->pid, SIGCONT);
                    start_time(&running_q->table[running_q->head]->start);
                }
            }
        }
        sem_post(&process_table->mutex);
    }
}

//signal handler
static void my_handler(int signum){
    if(signum == SIGINT){
        printf("\nCaught SIGINT signal for termination\n");
        printf("Terminating simple scheduler...\n");
        free(running_q->table);
        free(running_q);
        free(ready_q->heap);
        free(ready_q);
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

//queue methods
bool queue_empty(struct queue *q){
    return q->head == q->tail;
}

int next_head(struct queue *q){
    if (q->head == q->capacity-1){
        return 0;
    }
    return q->head+1;
}

int next_tail(struct queue *q){
    if (q->tail == q->capacity-1){
        return 0;
    }
    return q->tail+1;
}

bool queue_full(struct queue *q){
    return next_tail(q) == q->head;
}

void enqueue(struct queue *q, struct Process *proc){
    if (queue_full(q)){
        printf("queue overflow, upper cap of 20 jobs at once\n");
        return;
    }
    q->curr++;
    q->table[q->tail] = proc;
    q->tail = next_tail(q);
}

void dequeue(struct queue *q){
    if (queue_empty(q)){
        printf("queue underflow\n");
        return;
    }
    q->curr--;
    q->head = next_head(q);
}

//pqueue methods
bool pqueue_empty(struct pqueue *pq){
    return pq->size == 0;
}

bool pqueue_full(struct pqueue *pq){
    return pq->size == pq->capacity;
}

void swap(struct Process* a, struct Process* b){
    struct Process temp = *a;
    *a = *b;
    *b = temp;
}

void heapifyUp(struct pqueue* pq, int index){
    while (index>0){
        int parent = (index-1)/2;
        if (pq->heap[index]->vruntime < pq->heap[parent]->vruntime){
            swap(pq->heap[index], pq->heap[parent]);
            index = parent;
        }
        else{
            break;
        }
    }
}

void heapifyDown(struct pqueue* pq, int index){
    int leftChild = 2*index + 1;
    int rightChild = 2*index + 2;
    int smallest = index;

    if (leftChild<pq->size && pq->heap[leftChild]->vruntime < pq->heap[smallest]->vruntime){
        smallest = leftChild;
    }

    if (rightChild<pq->size && pq->heap[rightChild]->vruntime < pq->heap[smallest]->vruntime){
        smallest = rightChild;
    }

    if (smallest != index){
        swap(pq->heap[index], pq->heap[smallest]);
        heapifyDown(pq, smallest);
    }
}

void penqueue(struct pqueue *pq, struct Process *proc){
    if (pq->size < pq->capacity){
        pq->heap[pq->size] = proc;
        heapifyUp(pq, pq->size);
        pq->size++;
    }
}

struct Process* pdequeue(struct pqueue *pq){
    if (pq->size>0){
        struct Process* removed = pq->heap[0];
        pq->heap[0] = pq->heap[pq->size - 1];
        pq->size--;
        heapifyDown(pq, 0);
        return removed;
    }
    return NULL;
}