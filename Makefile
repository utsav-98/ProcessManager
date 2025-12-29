# simple makefile for task manager

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g
LDFLAGS = -lncurses
OS = $(shell uname -s)

TARGET = process_manager
SRCS = main.c ui.c

ifeq ($(OS),Darwin)
	SRCS += process_list_macos.c
	CFLAGS += -D__APPLE__
else ifeq ($(OS),Linux)
	SRCS += process_list_linux.c
else
	$(error Unsupported OS: $(OS))
endif

OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

# run with sudo if you want to kill other users' processes
run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
