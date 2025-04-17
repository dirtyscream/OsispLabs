
#include <stdio.h>      
#include <stdlib.h>     
#include <stdint.h>     
#include <fcntl.h>      
#include <sys/mman.h>   
#include <sys/stat.h>   
#include <unistd.h>     
#include <time.h>       
#include <errno.h>      
#include <string.h>     
#include "index.h"      


#define MIN_MJD 15020   
#define MAX_MJD 61000   

int main(int argc, char *argv[]) { 
    if (argc != 3) { 
        fprintf(stderr, "Usage: %s <record_count> <filename>\n", argv[0]); 
        return EXIT_FAILURE; 
    }

    char *endptr; 
    errno = 0;    
    uint64_t record_count = strtoull(argv[1], &endptr, 10);
    
    if (errno != 0 || *endptr != '\0' || record_count == 0) {
        fprintf(stderr, "Error: Invalid record count '%s'\n", argv[1]); 
        return EXIT_FAILURE; 
    }

    const char *filename = argv[2]; 

    
    int fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, (mode_t)0644);
    if (fd == -1) { 
        perror("Error opening file for writing"); 
        return EXIT_FAILURE; 
    }

    
    size_t file_size = sizeof(struct index_hdr_s) + record_count * sizeof(struct index_s);

    
    if (ftruncate(fd, file_size) == -1) { 
        perror("Error setting file size (ftruncate)"); 
        close(fd); 
        return EXIT_FAILURE; 
    }

    
    void *map_addr = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map_addr == MAP_FAILED) { 
        perror("Error mapping file to memory (mmap)"); 
        close(fd); 
        return EXIT_FAILURE; 
    }

    
    struct index_hdr_s *header = (struct index_hdr_s *)map_addr;
    header->records = record_count; 

    
    
    srand(time(NULL));

    
    printf("Generating %llu records into %s...\n", (unsigned long long)record_count, filename);

    
    for (uint64_t i = 0; i < record_count; ++i) {
        
        
        int int_part = MIN_MJD + rand() % (MAX_MJD - MIN_MJD + 1);
        
        double frac_part = (double)rand() / RAND_MAX;
        
        header->idx[i].time_mark = (double)int_part + frac_part;

        
        
        header->idx[i].recno = i + 1;
    }
    

    
    if (msync(map_addr, file_size, MS_SYNC) == -1) { 
        perror("Error syncing memory to disk (msync)"); 
        
    }

    
    if (munmap(map_addr, file_size) == -1) { 
        perror("Error unmapping memory (munmap)"); 

    }

    
    if (close(fd) == -1) { 
        perror("Error closing file"); 
        return EXIT_FAILURE; 
    }

    printf("Done.\n"); 
    return EXIT_SUCCESS; 
}