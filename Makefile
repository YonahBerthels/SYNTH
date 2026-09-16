CC         := clang

SRC_DIR    := src
VENDOR_DIR := vendor
BUILD_DIR  := build
OBJ_DIR    := $(BUILD_DIR)/obj

CFLAGS  := -Wall -Wextra -std=c17 -I$(SRC_DIR) -I$(VENDOR_DIR) \
           $(shell pkg-config --cflags sdl2 SDL2_TTF)
LDFLAGS := $(shell pkg-config --libs sdl2 SDL2_TTF)

TARGET  := $(BUILD_DIR)/synth

SRCS := $(wildcard $(SRC_DIR)/*.c) $(wildcard $(VENDOR_DIR)/*.c)
# src/viz.c -> build/obj/src/viz.o, so the two source trees cannot collide
OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

# -MMD -MP writes a .d file listing the headers each object depends on, so
# editing theme.h rebuilds everything that includes it instead of nothing.
$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all run clean
