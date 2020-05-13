MAJOR = 0
MINOR = 1
NAME = libpathtrap
VERSION = $(MAJOR).$(MINOR)
LIB = $(NAME).so.$(VERSION)

$(LIB): $(NAME).c vma_addr.c
	$(CC) -Wall -Wextra -Werror -g -fPIC -shared -Wl,-soname,$(NAME).so.$(MAJOR) -ldl $^ -o $@

.PHONY: all
all: build

build: $(LIB)

.PHONY: install
install:
	$(INSTALL_PROG) -D $(LIB) $(DESTDIR)$(LIBDIR)/$(LIB)

.PHONY: clean
clean:
	rm -f $(LIB)
