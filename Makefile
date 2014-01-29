CXXFLAGS := -std=c++11 -O
PREFIX := /usr/local

all: tldd

tldd: tldd.cc

install: tldd
	install -D tldd $(PREFIX)/bin/tldd
	install -D tldd.1 $(PREFIX)/share/man/man1/tldd.1

clean:
	rm -f tldd
