.PHONY: all clean test
SHELL = /bin/bash
CXX = g++ -std=c++17
LIBS = -lusb-1.0 -lm
#CFLAGS = -O0 -g3 -march=native
CFLAGS = -g0 -O3 -march=native -fomit-frame-pointer -DNDEBUG
# sanitizers when installed (libasan-devel, libubsan-devel)
SANITIZE := $(shell echo 'int main(){}' | $(CXX) -x c++ -fsanitize=address,undefined - -o /dev/null 2>/dev/null \
    && echo -fsanitize=address,undefined -fno-sanitize-recover=all)
TEST_CFLAGS = -O0 -g -fno-omit-frame-pointer -D_GLIBCXX_ASSERTIONS $(SANITIZE)

LIB_SRCS = protocol.cpp session.cpp trace.cpp log.cpp
TEST_SRCS = tests/check.cpp tests/test_protocol.cpp tests/test_session.cpp

all: accuchek
	@echo done.

accuchek: .objs/main.o $(LIB_SRCS:%.cpp=.objs/%.o)
	@echo lnk -- $@
	@$(CXX) $(CFLAGS) -o $@ $^ $(LIBS)

.objs/%.o: %.cpp Makefile
	@echo c++ -- $<
	@mkdir -p $(dir $@) .deps/$(dir $<)
	@$(CXX) -MMD -MF .deps/$*.d $(CFLAGS) -I. -c $< -o $@

# tests: built with sanitizers, run against the real binary for CLI checks
# -------------------------------------------------------------------------

.objs/test/%.o: %.cpp Makefile
	@echo c++ test -- $<
	@mkdir -p $(dir $@) .deps/test/$(dir $<)
	@$(CXX) -MMD -MF .deps/test/$*.d $(TEST_CFLAGS) -I. -c $< -o $@

.objs/test/run_tests: $(LIB_SRCS:%.cpp=.objs/test/%.o) $(TEST_SRCS:%.cpp=.objs/test/%.o)
	@echo lnk -- $@
	@$(CXX) $(TEST_CFLAGS) -o $@ $^ $(LIBS)

test: accuchek .objs/test/run_tests
	@ACCUCHEK_BIN="$(CURDIR)/accuchek" .objs/test/run_tests

clean:
	rm -r -f accuchek
	rm -r -f .deps .objs

-include $(shell find .deps -name '*.d' 2>/dev/null)
