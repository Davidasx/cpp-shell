CC=g++
CFLAGS=-c -Wall -std=c++11
SOURCES=shell.cpp
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=shell

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@

.cpp.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f *.o $(EXECUTABLE)