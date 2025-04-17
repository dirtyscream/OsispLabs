
#ifndef INDEX_H
#define INDEX_H

#include <stdint.h> 






struct index_s {
    double time_mark; 
    uint64_t recno;   
};


struct index_hdr_s {
    uint64_t records; 
    struct index_s idx[]; 
                          
};






static inline int compare_index_s(const void *a, const void *b) {
    const struct index_s *ia = (const struct index_s *)a;
    const struct index_s *ib = (const struct index_s *)b;
    if (ia->time_mark < ib->time_mark) return -1;
    if (ia->time_mark > ib->time_mark) return 1;
    return 0; 
}

#endif 