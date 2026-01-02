CC      := gcc
CFLAGS  := -Wall -Wextra -std=c99 -O2 -g
LDFLAGS := -lncurses

# Installation setup
PREFIX  ?= /usr/local
BINDIR  := $(PREFIX)/bin
TARGET  := prcsmgr

# Source management
SRCS    := main.c process_list.c ui.c
OBJS    := $(SRCS:.c=.o)

# --- Build Rules ---

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# --- Utility Tasks ---

clean:
	rm -f $(OBJS) $(TARGET)

run: $(TARGET)
	./$(TARGET)

# --- Installation ---

install: $(TARGET)
	mkdir -p $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

.PHONY: all clean run install uninstall