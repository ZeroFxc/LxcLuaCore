CC=gcc
CFLAGS=-Wall -O2 -I./lua
LDFLAGS=

SRC=main.c translate.c dump.c
OBJ=$(SRC:.c=.o)
LUA_SRC=$(wildcard lua/*.c)
LUA_OBJ=$(LUA_SRC:.c=.o)

TARGET=translator

all: $(TARGET)

$(TARGET): $(OBJ) $(LUA_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(LUA_OBJ) $(TARGET)
