PROGNAME = azptool
BINDIR = bin
OBJDIR = obj

CC = clang
LIBS = zlib
LDFLAGS = $(shell pkg-config --libs $(LIBS)) -flto
CFLAGS = $(shell pkg-config --cflags $(LIBS)) -Wall -Wpedantic -Werror -std=c99 -O3

SRCS = $(wildcard *.c)
OBJS := $(patsubst %.c,%.o, $(SRCS))

.PHONY: default dirs clean all

all: $(PROGNAME)

$(OBJDIR)/%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(PROGNAME): dirs $(foreach obj,$(OBJS), $(OBJDIR)/$(obj))
	$(CC) -o $(BINDIR)/$(PROGNAME) $(foreach obj,$(OBJS), $(OBJDIR)/$(obj)) $(LDFLAGS)
	
dirs:
	mkdir -p $(OBJDIR) $(BINDIR)
	
clean:
	rm -fv $(OBJDIR)/*.o $(BINDIR)/$(PROGNAME)
