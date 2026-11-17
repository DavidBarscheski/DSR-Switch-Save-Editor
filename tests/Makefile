CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -I../include -I../source

SRCS := test_core.cpp ../source/save.cpp ../source/persist.cpp ../source/catalog.cpp ../source/ui.cpp ../source/md5.cpp
DEPS := $(wildcard ../include/*.h) $(wildcard ../source/*.inc) $(wildcard ../source/*.h) $(SRCS)

# `make` (or `make test`) builds the test program when anything it depends on changed, and ALWAYS runs it.
test: test_core
	./test_core

test_core: $(DEPS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

clean:
	rm -f test_core

.PHONY: test clean
