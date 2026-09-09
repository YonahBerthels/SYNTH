CC      := clang
CFLAGS  := -Wall -Wextra -std=c17 $(shell pkg-config --cflags sdl2 SDL2_TTF)
LDFLAGS := $(shell pkg-config --libs sdl2 SDL2_TTF)

TARGET  := synth
SRCS    := $(wildcard *.c)
OBJS    := $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all run clean
