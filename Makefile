#FLAGS = `pkg-config --cflags --libs libavcodec libavformat`
#FLAGS = `pkg-config --cflags --libs libavcodec libavutil libdrm`
FLAGS = $(shell pkg-config --cflags --libs libavcodec libavformat libdrm)
# libkms`
FLAGS+=-Wall -Wextra -O0 -g -ggdb
FLAGS+=-D_FILE_OFFSET_BITS=64

#all:
#	gcc -o v4l2_test v4l2_test.c $(FLAGS)

CC = gcc

OBJECTS = main.o v4l2.o stream.o video.o
#SOURCES = $(OBJECTS:.o=.c)
#SOURCES = v4l2_test.c stream.c
#SOURCES = v4l2_test.c
EXEC = v4l2_test
#CFLAGS = -Wall -g -fPIC

#all: $(EXEC)

$(EXEC): $(OBJECTS)
	$(CC) $(OBJECTS) $(FLAGS) -o $(EXEC)
#	$(CC) $(CFLAGS) $(LDFLAGS) -shared -o $(EXEC) $(OBJECTS) -pthread
#	$(CC) $(CFLAGS) $(LDFLAGS) -shared $(OBJECTS) $(FLAGS) -o $@

%.o: %.c
	$(CC) $(FLAGS) -c $<

clean:
	rm -f *.o $(EXEC)

#install:

#.PHONY: clean all
