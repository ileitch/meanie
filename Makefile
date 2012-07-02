SHELL := /bin/bash
PREFIX_DIR = $(PWD)/built
PCRE_DIR = $(PWD)/vendor/pcre-8.30
LIBGIT2_DIR = $(PWD)/vendor/libgit2-0.17.0
FILES = util.c git.c search.c main.c

all: pcre libgit2 meanie

clean: clean_pcre clean_libgit2 clean_meanie
	rm -rf $(PREFIX_DIR)

meanie: clean_meanie
	gcc `pkg-config --libs --cflags glib-2.0` -L$(PREFIX_DIR)/lib -I$(PWD)/valgrind -I$(PREFIX_DIR)/include -Wall -O0 -g $(FILES) -lgit2 -lpcre -lpthread -o meanie

clean_meanie:
	rm -f ./*.o
	rm -rf ./*.dSYM

pcre:
	cd $(PCRE_DIR); \
	./configure --enable-jit --prefix=$(PREFIX_DIR)
	cd $(PCRE_DIR); \
	make
	cd $(PCRE_DIR); \
	make install

clean_pcre:	
	cd $(PCRE_DIR); \
	make distclean

libgit2:
	cd $(LIBGIT2_DIR); \
	mkdir -p build
	cd $(LIBGIT2_DIR)/build; \
	cmake .. -DCMAKE_INSTALL_PREFIX=$(PREFIX_DIR)
	cd $(LIBGIT2_DIR)/build; \
	cmake --build . --target install

clean_libgit2:
	rm -rf $(LIBGIT2_DIR)/build