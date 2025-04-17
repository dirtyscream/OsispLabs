all: gen view sort_index

gen: gen.c index.h       
	gcc -Wall -Wextra -O2 gen.c -o gen 

view: view.c index.h      
	gcc -Wall -Wextra -O2 view.c -o view 

sort_index: sort_index.c index.h 
	gcc -Wall -Wextra -O2 sort_index.c -o sort_index -pthread -lm 

clean:                    
	rm -f gen view sort_index *.o 

