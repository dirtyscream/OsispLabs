#include <stdio.h>         
#include <stdlib.h>        
#include <string.h>        
#include <unistd.h>        
#include <signal.h>        
#include <pthread.h>       
#include <errno.h>         
#include <time.h>          
#include <termios.h>       
#include <stdatomic.h>     
#include <stdint.h>        


#define INITIAL_QUEUE_SIZE 10

#define MAX_THREADS 100

typedef struct {
    unsigned char type;     
    unsigned short hash;    
    unsigned char size;     
    char data[256];         
} Message;


typedef struct {
    Message **buffer;       
    int head;               
    int tail;               
    int max_size;           
    atomic_int count;       
    int added_count;        
    int removed_count;      
    int num_producers;      
    int num_consumers;      
} MessageQueue;

MessageQueue queue;              
pthread_mutex_t mutex;         
pthread_cond_t cond_empty;     
pthread_cond_t cond_full;      
pthread_t consumer_threads[MAX_THREADS]; 
int consumer_count = 0;        
volatile sig_atomic_t terminate_flag = 0; 

int getch() {
    struct termios old_termios, new_termios; 
    int ch = EOF;                            
    if (tcgetattr(STDIN_FILENO, &old_termios) == 0) { 
        new_termios = old_termios;                  
        new_termios.c_lflag &= ~((tcflag_t)ICANON | (tcflag_t)ECHO); 
        if (tcsetattr(STDIN_FILENO, TCSANOW, &new_termios) == 0) { 
            ch = getchar(); 
            tcsetattr(STDIN_FILENO, TCSANOW, &old_termios); 
        } else {
             tcsetattr(STDIN_FILENO, TCSANOW, &old_termios); 
        }
    }
    return ch; 
}

unsigned short calculate_hash(const char *data, int size) {
    unsigned short hash = 0;            
    for (int i = 0; i < size; ++i) {    
        hash += (unsigned char)data[i]; 
        hash = (hash << 1) | (hash >> 15); 
    }
    return hash; 
}

Message *generate_message(unsigned int *seed) {
    Message *msg = (Message *)malloc(sizeof(Message)); 
    if (!msg) { 
        perror("malloc (message)");
        return NULL;
    }
    msg->type = 1; 
    msg->size = (unsigned char)(rand_r(seed) % 256); 
    int max_payload_size = sizeof(msg->data); 
    
    int required_data_len = ((msg->size + 1 + 3) / 4) * 4;
    
    int actual_data_len = (required_data_len <= max_payload_size) ? required_data_len : max_payload_size;

    
    for (int i = 0; i < actual_data_len; ++i) {
        msg->data[i] = (char)(rand_r(seed) % 256);
    }
    
     for (int i = actual_data_len; i < required_data_len; ++i) {
         if (i < max_payload_size) msg->data[i] = 0;
    }

    msg->hash = 0; 
    
    msg->hash = calculate_hash((char *)msg + sizeof(msg->type) + sizeof(msg->hash), msg->size + 1);
    return msg; 
}

int enqueue(Message *msg) {
    pthread_mutex_lock(&mutex); 
    
    while (atomic_load(&queue.count) == queue.max_size && !terminate_flag) {
        
        pthread_cond_wait(&cond_empty, &mutex);
        
    }
    
    if (terminate_flag) {
        pthread_mutex_unlock(&mutex); 
        
        pthread_cond_broadcast(&cond_empty);
        return -1; 
    }
    
    queue.buffer[queue.tail] = msg; 
    queue.tail = (queue.tail + 1) % queue.max_size; 
    atomic_fetch_add(&queue.count, 1); 
    queue.added_count++;               
    int current_added = queue.added_count; 
    pthread_cond_signal(&cond_full); 
    pthread_mutex_unlock(&mutex);    
    return current_added;            
}


Message *dequeue() {
    pthread_mutex_lock(&mutex); 
    
    while (atomic_load(&queue.count) == 0 && !terminate_flag) {
        
        pthread_cond_wait(&cond_full, &mutex);
        
    }
    
    if (terminate_flag) {
        pthread_mutex_unlock(&mutex); 
        
        pthread_cond_broadcast(&cond_full);
        return NULL; 
    }
    
    Message *msg = queue.buffer[queue.head]; 
    queue.buffer[queue.head] = NULL;         
    queue.head = (queue.head + 1) % queue.max_size; 
    atomic_fetch_sub(&queue.count, 1); 
    queue.removed_count++;             
    pthread_cond_signal(&cond_empty); 
    pthread_mutex_unlock(&mutex);    
    return msg;                      
}

void *producer_thread_single_shot(void *arg) {
    (void)arg; 
    unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)pthread_self(); 
    Message *msg = generate_message(&seed); 
    if (!msg) { 
        fprintf(stderr, "Producer: Failed to generate message\n");
        pthread_exit((void*)0); 
    }
    int added_count_local = enqueue(msg); 
    if (added_count_local <= 0) { 
        free(msg); 
        fprintf(stderr, "Producer: Failed to enqueue message\n");
        pthread_exit((void*)0); 
    }
    
    pthread_exit((void*)(intptr_t)added_count_local);
    
}

void *consumer_thread(void *arg) {
    int thread_id = *((int*)arg); 
    free(arg);                    
    unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)pthread_self(); 
    printf("Consumer thread %d started.\n", thread_id); 
    while (!terminate_flag) { 
        Message *msg = dequeue(); 
        if (!msg) { 
            if (terminate_flag) break; 
            else continue; 
        }
        
        unsigned short expected_hash = msg->hash;
        msg->hash = 0;
        unsigned short calculated_hash = calculate_hash((char *)msg + sizeof(msg->type) + sizeof(msg->hash), msg->size + 1);
        msg->hash = expected_hash;
        
        pthread_mutex_lock(&mutex);
        int current_removed = queue.removed_count;
        pthread_mutex_unlock(&mutex);
        
        printf("Consumer %d: Message dequeued (Hash %s), total removed = %d\n",
               thread_id, (calculated_hash == expected_hash) ? "OK" : "Mismatch!", current_removed);
        free(msg); 
        
        struct timespec ts = {0, (rand_r(&seed) % 700 + 200) * 1000000};
        nanosleep(&ts, NULL);
    }
    printf("Consumer thread %d finished.\n", thread_id); 
    
    pthread_mutex_lock(&mutex);
    queue.num_consumers--;
    pthread_mutex_unlock(&mutex);
    pthread_exit(NULL); 
}

void increase_queue_size() {
    pthread_mutex_lock(&mutex); 
    int old_size = queue.max_size; 
    int new_size = old_size + 1;   
    
    if (new_size > INITIAL_QUEUE_SIZE * 4) {
        printf("Queue size limit reached (%d).\n", INITIAL_QUEUE_SIZE * 4);
        pthread_mutex_unlock(&mutex);
        return;
    }
    
    queue.max_size = new_size;
    
    pthread_cond_signal(&cond_empty);
    printf("Queue logical size increased to %d\n", new_size);
    pthread_mutex_unlock(&mutex); 
}

void decrease_queue_size() {
    pthread_mutex_lock(&mutex); 
    int old_size = queue.max_size; 
    if (old_size <= 1) { 
        printf("Queue size cannot be decreased further.\n");
        pthread_mutex_unlock(&mutex);
        return;
    }
    
    if (atomic_load(&queue.count) >= old_size) {
         printf("Cannot decrease queue size now (queue is full).\n");
         pthread_mutex_unlock(&mutex);
         return;
    }
    
    queue.max_size--;
    printf("Queue logical size decreased to %d\n", queue.max_size);
    
    pthread_cond_signal(&cond_full);
    pthread_cond_signal(&cond_empty);
    pthread_mutex_unlock(&mutex); 
}

void signal_handler(int sig) {
    (void)sig; 
    printf("\nSIGINT received, initiating shutdown...\n"); 
    terminate_flag = 1; 
    
    pthread_cond_broadcast(&cond_empty);
    pthread_cond_broadcast(&cond_full);
}

int main() {
    
    queue.buffer = (Message **)malloc(INITIAL_QUEUE_SIZE * 4 * sizeof(Message *));
    if (!queue.buffer) { perror("malloc queue buffer"); return 1; }
    
     for (int i = 0; i < INITIAL_QUEUE_SIZE * 4; ++i) queue.buffer[i] = NULL;
    queue.head = 0; 
    queue.tail = 0; 
    queue.max_size = INITIAL_QUEUE_SIZE; 
    atomic_init(&queue.count, 0);        
    queue.added_count = 0;               
    queue.removed_count = 0;
    queue.num_producers = 0;
    queue.num_consumers = 0;

    if (pthread_mutex_init(&mutex, NULL) != 0) { perror("Mutex init"); free(queue.buffer); return 1; }
    if (pthread_cond_init(&cond_empty, NULL) != 0) { perror("Cond 'empty' init"); pthread_mutex_destroy(&mutex); free(queue.buffer); return 1; }
    if (pthread_cond_init(&cond_full, NULL) != 0) { perror("Cond 'full' init"); pthread_cond_destroy(&cond_empty); pthread_mutex_destroy(&mutex); free(queue.buffer); return 1; }    
    signal(SIGINT, signal_handler);
    printf("Main process started (Lab 5.2 - Condition Variables).\n");
    printf("Press 'p' [+]prod(1), 'c' [+]cons, '+' increase, '-' decrease, 's' status, 'q' quit.\n");

    char choice; 
    while (!terminate_flag) { 
        printf("> ");       
        fflush(stdout);    
        choice = (char)getch(); 
        if (choice == EOF || terminate_flag) break; 
        printf("%c\n", choice); 

        int *thread_arg = NULL;        
        pthread_t temp_producer_tid; 
        void *producer_result;      

        switch (choice) { 
            case 'p': 
                
                if (pthread_create(&temp_producer_tid, NULL, producer_thread_single_shot, NULL) != 0) {
                    perror("pthread_create producer");
                } else {
                    
                    if (pthread_join(temp_producer_tid, &producer_result) != 0) {
                        perror("pthread_join producer");
                    } else {
                        
                        intptr_t added_count_res = (intptr_t)producer_result;
                        if (added_count_res > 0) { 
                            printf("Producer thread finished: Message enqueued, total added = %ld\n", (long)added_count_res);
                             pthread_mutex_lock(&mutex); 
                             queue.num_producers++;
                             pthread_mutex_unlock(&mutex);
                        } else { 
                            printf("Producer thread failed to enqueue message.\n");
                        }
                    }
                }
                break; 
            case 'c': 
                 if (consumer_count < MAX_THREADS) { 
                    thread_arg = malloc(sizeof(int)); 
                     if(!thread_arg) { perror("malloc arg"); break; }
                    *thread_arg = consumer_count;      
                    
                    if (pthread_create(&consumer_threads[consumer_count], NULL, consumer_thread, thread_arg) != 0) {
                        perror("pthread_create consumer"); free(thread_arg); 
                    } else {
                         printf("Consumer thread %d created.\n", consumer_count); 
                         pthread_mutex_lock(&mutex); 
                         queue.num_consumers++;
                         pthread_mutex_unlock(&mutex);
                         consumer_count++;           
                    }
                } else { printf("Max consumer threads reached.\n"); } 
                break; 
            case '+': 
                increase_queue_size();
                break;
            case '-': 
                decrease_queue_size();
                break;
            case 's': 
                pthread_mutex_lock(&mutex); 
                printf("--- Queue Status ---\n");
                printf("  Max Size (Logical): %d\n", queue.max_size);        
                printf("  Occupied:           %d\n", atomic_load(&queue.count)); 
                printf("  Free (Logical):     %d\n", queue.max_size - atomic_load(&queue.count)); 
                printf("  Total Added:        %d\n", queue.added_count);       
                printf("  Total Removed:      %d\n", queue.removed_count);     
                 printf("  Producer Runs:      %d\n", queue.num_producers);     
                 printf("  Active Consumers:   %d\n", queue.num_consumers);     
                printf("--------------------\n");
                pthread_mutex_unlock(&mutex); 
                break; 
            case 'q': 
                printf("Initiating quit...\n");
                kill(getpid(), SIGINT); 
                break; 
            default: 
                printf("Invalid command.\n");
        } 
    } 

    printf("Waiting for CONSUMER threads to finish...\n"); 
    
    for (int i = 0; i < consumer_count; ++i) {
        pthread_join(consumer_threads[i], NULL);
    }

    printf("Cleaning up resources...\n"); 
    
    pthread_cond_destroy(&cond_empty);
    pthread_cond_destroy(&cond_full);
    pthread_mutex_destroy(&mutex);

    
     for (int i = 0; i < INITIAL_QUEUE_SIZE * 4; ++i) { 
        if (queue.buffer[i] != NULL) {
            free(queue.buffer[i]); 
            queue.buffer[i] = NULL; 
        }
    }
    free(queue.buffer); =

    printf("Program finished.\n"); 
    return 0;
}