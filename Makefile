#CC=clang
CC=gcc
#CC=i686-w64-mingw32-gcc
OBJDIR=obj

SRC = $(wildcard *.c)
OBJ = $(addprefix $(OBJDIR)/, $(notdir $(SRC:.c=.o)))

CFLAGS = -std=c99 -Wall -Werror -Wno-unused -Os -g -D_GNU_SOURCE -lz -DCOUNT_TIME_TAKEN

azptool: $(OBJ)
	$(CC) $(OBJ) $(CFLAGS) -o $@

obj/%.o: %.c | obj
	$(CC) $< -c $(CFLAGS) -o $@


obj:
	mkdir $(OBJDIR)

clean:
	rm $(OBJ)
