#include <stdio.h>      
#include <stdlib.h>     
#include <stdint.h>     
#include <fcntl.h>      
#include <sys/mman.h>   
#include <sys/stat.h>   
#include <unistd.h>     
#include <errno.h>      
#include <string.h>     
#include "index.h"      

int main(int argc, char *argv[]) { 
    
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]); 
        return EXIT_FAILURE; 
    }

    const char *filename = argv[1]; 

    
    int fd = open(filename, O_RDONLY);
    if (fd == -1) { 
        perror("Error opening file for reading"); 
        return EXIT_FAILURE; 
    }

    
    struct stat sb; 
    if (fstat(fd, &sb) == -1) { 
        perror("Error getting file size (fstat)"); 
        close(fd); 
        return EXIT_FAILURE; 
    }
    size_t file_size = sb.st_size; 

    
    if (file_size < sizeof(struct index_hdr_s)) {
         fprintf(stderr, "Error: File size (%zu bytes) is smaller than header size (%zu bytes).\n",
                file_size, sizeof(struct index_hdr_s)); 
         close(fd); 
         return EXIT_FAILURE; 
    }

    
    void *map_addr = mmap(NULL, file_size, PROT_READ, MAP_SHARED, fd, 0);
    if (map_addr == MAP_FAILED) { 
        perror("Error mapping file to memory (mmap)"); 
        close(fd); 
        return EXIT_FAILURE; 
    }

    
    if (close(fd) == -1) {
        perror("Warning: Error closing file descriptor after mmap"); 
        
    }

    
    struct index_hdr_s *header = (struct index_hdr_s *)map_addr;
    
    uint64_t record_count = header->records;

    
    size_t expected_size = sizeof(struct index_hdr_s) + record_count * sizeof(struct index_s);
    if (file_size != expected_size) { 
        fprintf(stderr, "Warning: File size (%zu) does not match expected size based on header (%zu).\n",
                file_size, expected_size); 
        
        if (expected_size > file_size) {
             
             uint64_t max_possible_records = (file_size - sizeof(struct index_hdr_s)) / sizeof(struct index_s);
             fprintf(stderr, "Processing only %llu records based on actual file size.\n", (unsigned long long)max_possible_records);
             record_count = max_possible_records; 
        }
         
    }

    
    printf("Index File: %s\n", filename); 
    printf("Total Records (from header): %llu\n", (unsigned long long)header->records); 
    printf("----------------------------------------\n");
    printf("%-20s | %s\n", "Time Mark (MJD)", "Record Number"); 
    printf("----------------------------------------\n");

    
    for (uint64_t i = 0; i < record_count; ++i) {
        
        printf("%-20.10f | %llu\n", header->idx[i].time_mark, (unsigned long long)header->idx[i].recno);
    }
    printf("----------------------------------------\n"); 

    
    if (munmap(map_addr, file_size) == -1) { 
        perror("Error unmapping memory (munmap)"); 
        return EXIT_FAILURE; 
    }

    return EXIT_SUCCESS; 
}