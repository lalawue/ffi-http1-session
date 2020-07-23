
CC=gcc
SRC=src

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S), Darwin)
	CFLAGS=-O3 -bundle -undefined dynamic_lookup
else
        CFLAGS=-O3 -shared -fPIC
endif

all: hyperparser.so

hyperparser.so: src/http_parser.c src/pull_style_api.c
	$(CC) $^ -o $@ -I$(SRC) $(CFLAGS)

standalone: src/http_parser.c src/pull_style_api.c
	$(CC) $^ -o hyperparser -I$(SRC) $(CFLAGS) -DTEST_PULL_STYLE_API
clean:
	rm -rf *.so hyperparser
