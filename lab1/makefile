CC=gcc
CFLAGS=-c -Wall
LDFLAGS=
SOURCES=dirwalk.c
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=dirwalk

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJECTS) $(EXECUTABLE)