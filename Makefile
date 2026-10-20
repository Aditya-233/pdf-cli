CC ?= gcc
CFLAGS ?= -std=c23 -O3 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
INCLUDES = -Iinclude -Idepends/go-fitz/include -Idepends/libsixel/include -Idepends/yyjson/src
LIBS = depends/libsixel/libsixel.a \
       depends/go-fitz/libs/libmupdf_linux_amd64.a \
       depends/go-fitz/libs/libmupdfthird_linux_amd64.a \
       -lm -lpthread

BINARY ?= pdf-cli
PREFIX ?= $(HOME)/.local/bin
BUILD_DIR ?= build

SRCS = src/main.c \
       src/viewer.c \
       src/doc_engine.c \
       src/img_util.c \
       src/term_raw.c \
       src/term_gfx.c \
       src/utf8_util.c \
       src/fuzzy_match.c \
       src/file_search.c \
       src/menu.c \
       src/picker.c \
       src/toc_modal.c \
       src/config.c \
       src/ctl_proto.c \
       src/ctl_server.c \
       src/ctl_client.c \
       src/cache.c \
       src/prefetch.c \
       src/file_watcher.c \
       src/arena.c \
       depends/yyjson/src/yyjson.c

OBJS = $(patsubst %.c, $(BUILD_DIR)/%.o, $(SRCS))

.PHONY: all build clean install test

all: build

build: $(BINARY) dvctl

$(BINARY): $(OBJS) $(LIBS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OBJS) $(LIBS) -o $@

dvctl: $(BINARY)
	ln -sf $(BINARY) dvctl

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

test: $(BINARY)
	@mkdir -p $(BUILD_DIR)/test
	$(CC) $(CFLAGS) $(INCLUDES) test/test_all.c \
		src/utf8_util.c src/fuzzy_match.c src/file_search.c src/arena.c \
		src/cache.c src/doc_engine.c src/img_util.c src/term_raw.c \
		src/term_gfx.c src/config.c src/ctl_proto.c depends/yyjson/src/yyjson.c \
		$(LIBS) -o $(BUILD_DIR)/test/test_all
	$(BUILD_DIR)/test/test_all
	$(CC) $(CFLAGS) $(INCLUDES) test/test_ipc_config.c \
		src/config.c src/ctl_proto.c src/ctl_server.c src/ctl_client.c \
		depends/yyjson/src/yyjson.c -o $(BUILD_DIR)/test/test_ipc
	$(BUILD_DIR)/test/test_ipc

install: build
	mkdir -p $(PREFIX)
	cp $(BINARY) $(PREFIX)/$(BINARY).new
	mv -f $(PREFIX)/$(BINARY).new $(PREFIX)/$(BINARY)
	ln -sf $(PREFIX)/$(BINARY) $(PREFIX)/dvctl

clean:
	rm -rf $(BUILD_DIR) $(BINARY) dvctl /tmp/test_all /tmp/test_ipc
