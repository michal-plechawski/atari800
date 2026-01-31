CC ?= cc
AR ?= ar

CFLAGS ?= -O2 -std=c99 -Wall -Wextra
CPPFLAGS ?= -I include -I src

SRCS = \
	src/artifacting.c \
	src/artifacting_filters.c \
	src/artifacting_pal_scalar.c \
	src/palettegenerator.c \
	src/gtiatables.c

OBJS = $(SRCS:src/%.c=build/%.o)

LIB = libartifacting_c.a

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) rcs $@ $(OBJS)

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build $(LIB)

.PHONY: all clean
