#define _GNU_SOURCE             
#include <stdio.h>              
#include <stdlib.h>             
#include <stdint.h>             
#include <fcntl.h>              
#include <sys/mman.h>           
#include <sys/stat.h>           
#include <unistd.h>             
#include <errno.h>              
#include <string.h>             
#include <pthread.h>            
#include <limits.h>             
#include "index.h"              


typedef struct {
    int thread_id;              
    int total_threads;          
    struct index_s *data_map;   
    uint64_t total_records;     
    size_t memsize;             
    int blocks;                 
    size_t records_per_block;   
    pthread_barrier_t *barrier; 
    void *merge_buffer;         
} thread_args_t;


typedef struct {
    struct index_s record;      
    int block_index;            
} heap_node_t;



int compare_heap_nodes(const void *a, const void *b) {
    const heap_node_t *ha = (const heap_node_t *)a; 
    const heap_node_t *hb = (const heap_node_t *)b; 
    
    return compare_index_s(&(ha->record), &(hb->record));
}


void sift_down(heap_node_t *heap, int start_node, int heap_size) {
    int root = start_node; 
    
    while ((root * 2 + 1) < heap_size) {
        int child = root * 2 + 1; 
        int swap = root;          
        
        if (compare_heap_nodes(&heap[swap], &heap[child]) > 0) swap = child;
        
        if (child + 1 < heap_size && compare_heap_nodes(&heap[swap], &heap[child + 1]) > 0) swap = child + 1;
        
        if (swap != root) {
            
            heap_node_t temp = heap[root]; heap[root] = heap[swap]; heap[swap] = temp;
            root = swap; 
        } else break; 
    }
}


void merge_blocks_in_chunk(struct index_s *chunk_start_ptr, int num_blocks_in_chunk,
                           size_t records_per_block, void *temp_buffer)
{
    
    if (num_blocks_in_chunk <= 1) return;

    
    size_t total_records_in_chunk = num_blocks_in_chunk * records_per_block;
    
    struct index_s *output_ptr = (struct index_s *)temp_buffer;

    
    heap_node_t *heap = (heap_node_t *)malloc(num_blocks_in_chunk * sizeof(heap_node_t));
    
    struct index_s **current_pos = (struct index_s **)malloc(num_blocks_in_chunk * sizeof(struct index_s *));
    
    struct index_s **end_pos = (struct index_s **)malloc(num_blocks_in_chunk * sizeof(struct index_s *));

    
    if (!heap || !current_pos || !end_pos) {
        perror("Failed malloc in merge"); 
        
        free(heap); free(current_pos); free(end_pos);
        return; 
    }

    int heap_size = 0; 
    
    for (int i = 0; i < num_blocks_in_chunk; ++i) {
        
        struct index_s *block_start = chunk_start_ptr + i * records_per_block;
        
        current_pos[i] = block_start;
        
        end_pos[i] = block_start + records_per_block;
        
        if (current_pos[i] < end_pos[i]) {
            
            heap[heap_size].record = *current_pos[i]++; 
            heap[heap_size].block_index = i;
            heap_size++; 
        }
    }

    
    
    for (int i = (heap_size / 2) - 1; i >= 0; i--) sift_down(heap, i, heap_size);

    uint64_t output_count = 0; 
    
    while (heap_size > 0 && output_count < total_records_in_chunk) {
        
        heap_node_t min_node = heap[0];
        
        output_ptr[output_count++] = min_node.record;
        
        int block_idx = min_node.block_index;

        
        if (current_pos[block_idx] < end_pos[block_idx]) {
            
            heap[0].record = *current_pos[block_idx]++; 
            heap[0].block_index = block_idx; 
        } else {
            
            
            heap[0] = heap[--heap_size];
        }
        
        if(heap_size > 0) sift_down(heap, 0, heap_size);
    }

    
    memcpy(chunk_start_ptr, temp_buffer, total_records_in_chunk * sizeof(struct index_s));
    
    free(heap); free(current_pos); free(end_pos);
}


void *thread_sort_func(void *arg) {
    
    thread_args_t *args = (thread_args_t *)arg;
    
    int tid = args->thread_id;                     
    int nthreads = args->total_threads;            
    size_t records_per_memchunk = args->memsize / sizeof(struct index_s); 

    
    if (records_per_memchunk == 0) return NULL; 

    
    
    for (uint64_t chunk_offset_records = 0; chunk_offset_records < args->total_records; chunk_offset_records += records_per_memchunk)
    {
        
        struct index_s *chunk_start_ptr = args->data_map + chunk_offset_records;
        
        uint64_t remaining_records_in_file = args->total_records - chunk_offset_records;
        
        uint64_t records_in_this_chunk = (remaining_records_in_file < records_per_memchunk) ? remaining_records_in_file : records_per_memchunk;
        
        int num_blocks_in_chunk = records_in_this_chunk / args->records_per_block;
        
        size_t last_block_extra_records = records_in_this_chunk % args->records_per_block;

        
        if (num_blocks_in_chunk == 0 && last_block_extra_records == 0) {
              
              pthread_barrier_wait(args->barrier); 
              if (nthreads > 1) pthread_barrier_wait(args->barrier); 
              continue; 
        }

        
        
        for (int block_idx = tid; block_idx < num_blocks_in_chunk; block_idx += nthreads) {
            
            qsort(chunk_start_ptr + block_idx * args->records_per_block, 
                  args->records_per_block,                                
                  sizeof(struct index_s),                                 
                  compare_index_s);                                       
        }
        
        if (last_block_extra_records > 0 && tid == 0) {
            
            qsort(chunk_start_ptr + num_blocks_in_chunk * args->records_per_block, 
                  last_block_extra_records,                                       
                  sizeof(struct index_s),                                         
                  compare_index_s);                                               
             
        }

        
        pthread_barrier_wait(args->barrier);

        
        
        if (tid == 0 && num_blocks_in_chunk > 1) { 
            
            if (args->merge_buffer == NULL) {
                
                args->merge_buffer = malloc(args->memsize);
                
                if (!args->merge_buffer) {
                     perror("FATAL: Failed to allocate merge buffer");
                     pthread_exit((void*)EXIT_FAILURE); 
                }
            }
            
            if(args->merge_buffer) {
                
                merge_blocks_in_chunk(chunk_start_ptr, num_blocks_in_chunk, args->records_per_block, args->merge_buffer);
            }
        }

        
        
        
        if (nthreads > 1) { 
            pthread_barrier_wait(args->barrier);
        }
    } 

    
    if (tid == 0 && args->merge_buffer != NULL) {
        free(args->merge_buffer);
        args->merge_buffer = NULL; 
    }
    return NULL; 
}


int is_power_of_two(int n) {
    return (n > 0) && ((n & (n - 1)) == 0); 
}


void die(const char *msg) {
    perror(msg);        
    exit(EXIT_FAILURE); 
}


int main(int argc, char *argv[]) { 
    
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <memsize> <blocks> <threads> <filename>\n", argv[0]);
        return EXIT_FAILURE;
    }

    
    char *endptr; 
    
    size_t memsize = strtoull(argv[1], &endptr, 10);
    
    if (errno || *endptr != '\0' || memsize == 0) { fprintf(stderr, "Invalid memsize\n"); return EXIT_FAILURE; }

    
    long blocks_long = strtol(argv[2], &endptr, 10);
    
    if (errno || *endptr != '\0' || blocks_long <= 0 || blocks_long > INT_MAX) { fprintf(stderr, "Invalid blocks\n"); return EXIT_FAILURE; }
    int blocks = (int)blocks_long; 

    
    long threads_long = strtol(argv[3], &endptr, 10);
    
    if (errno || *endptr != '\0' || threads_long <= 0 || threads_long > INT_MAX) { fprintf(stderr, "Invalid threads\n"); return EXIT_FAILURE; }
    int threads = (int)threads_long; 

    const char *filename = argv[4]; 

    
    
    long page_size_l = sysconf(_SC_PAGESIZE); if (page_size_l < 0) die("sysconf(_SC_PAGESIZE)");
    size_t page_size = (size_t)page_size_l; 
    
    long core_count_l = sysconf(_SC_NPROCESSORS_ONLN); if (core_count_l <= 0) core_count_l = 1; 
    int k = (int)core_count_l; 
    
    size_t records_per_memchunk = memsize / sizeof(struct index_s); 
    
    size_t records_per_block = records_per_memchunk / blocks; 

    
    if ((memsize % page_size != 0) ||           
        !is_power_of_two(blocks) ||             
        (threads < k) ||                        
        (threads > 8 * k) ||                    
        (blocks < threads * 4) ||               
        (memsize < sizeof(struct index_s)) ||   
        (records_per_memchunk < (size_t)blocks) || 
        (records_per_memchunk % blocks != 0))      
    {
        
        fprintf(stderr, "Error: Invalid arguments combination.\n");
        fprintf(stderr, "  Check: memsize multiple of page_size(%zu), blocks power_of_2, k(%d)<=threads<=8k, blocks>=4*threads, records_per_chunk(%zu)>=blocks, records_per_chunk divisible by blocks.\n",
                page_size, k, records_per_memchunk);
        return EXIT_FAILURE; 
    }

    

    
    
    int fd = open(filename, O_RDWR); if (fd == -1) die("open"); 
    
    struct stat sb; if (fstat(fd, &sb) == -1) { close(fd); die("fstat"); } 
    size_t file_size = sb.st_size; 
    
    if (file_size < sizeof(struct index_hdr_s)) {
        fprintf(stderr, "Error: File too small\n"); close(fd); return EXIT_FAILURE; 
    }
    
    void *map_addr = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    
    if (map_addr == MAP_FAILED) { close(fd); die("mmap"); } 

    
    struct index_hdr_s *header = (struct index_hdr_s *)map_addr;
    
    uint64_t total_records = header->records;
    
    struct index_s *data_start = header->idx;

    
    size_t expected_size = sizeof(struct index_hdr_s) + total_records * sizeof(struct index_s);
    if (file_size != expected_size) {
        
        fprintf(stderr, "Warning: File size mismatch (using header count or file limit).\n");
        
        if (expected_size > file_size) {
            total_records = (file_size - sizeof(struct index_hdr_s)) / sizeof(struct index_s);
        } 
    }
    
    if (total_records == 0) {
        printf("No records to sort.\n");
        
        munmap(map_addr, file_size); close(fd); return EXIT_SUCCESS;
    }

    
    pthread_barrier_t barrier; 
    
    if (pthread_barrier_init(&barrier, NULL, threads) != 0) {
        
        munmap(map_addr, file_size); close(fd); die("pthread_barrier_init");
    }

    
    pthread_t *thread_ids = malloc(threads * sizeof(pthread_t));
    
    thread_args_t *thread_args = malloc(threads * sizeof(thread_args_t));
    
    if (!thread_ids || !thread_args) {
        
        free(thread_ids); free(thread_args);
        pthread_barrier_destroy(&barrier); munmap(map_addr, file_size); close(fd);
        die("malloc for threads");
    }

    
    
    for (int i = 0; i < threads; ++i) {
        
        thread_args[i] = (thread_args_t){ .thread_id = i, .total_threads = threads, .data_map = data_start,
                                           .total_records = total_records, .memsize = memsize, .blocks = blocks,
                                           .records_per_block = records_per_block, .barrier = &barrier, .merge_buffer = NULL };
        
        if (pthread_create(&thread_ids[i], NULL, thread_sort_func, &thread_args[i]) != 0) {
            
            perror("pthread_create failed");
            
            free(thread_ids); free(thread_args); 
            pthread_barrier_destroy(&barrier); munmap(map_addr, file_size); close(fd); 
            exit(EXIT_FAILURE); 
        }
    }

    
    
    for (int i = 0; i < threads; ++i) {
        
        pthread_join(thread_ids[i], NULL); 
    }
    
    printf("Sorting and merging complete.\n");

    
    free(thread_ids);   
    free(thread_args);  
    pthread_barrier_destroy(&barrier); 

    
    printf("Syncing changes to disk...\n"); 
    
    if (msync(map_addr, file_size, MS_SYNC) == -1) perror("Warning: msync failed"); 
    
    if (munmap(map_addr, file_size) == -1) perror("Warning: munmap failed"); 
    
    if (close(fd) == -1) perror("Warning: close failed"); 

    

    printf("Done.\n"); 
    return EXIT_SUCCESS; 
}