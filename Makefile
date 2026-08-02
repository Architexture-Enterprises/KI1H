# If RACK_DIR is not defined when calling the Makefile, default to two directories above
RACK_DIR ?= ../..

# FLAGS will be passed to both the C and C++ compiler
FLAGS +=
CFLAGS +=
CXXFLAGS +=

# Careful about linking to shared libraries, since you can't assume much about the user's environment and library search path.
# Static libraries are fine, but they should be added to this plugin's build system.
LDFLAGS +=

# Add .cpp files to the build
SOURCES += $(wildcard src/*.cpp)

# Add files to the ZIP package when running `make dist`
# The compiled plugin and "plugin.json" are automatically added.
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += $(wildcard presets)

# Include the Rack plugin Makefile framework
include $(RACK_DIR)/plugin.mk

# ============================================================================
# UNIT TESTS
# ============================================================================
# Tests for the pure DSP in src/dsp.hpp. Kept out of SOURCES so they never end
# up in the plugin, and declared after the include above so `all` stays the
# default goal.
#
#   make test RACK_DIR=/path/to/Rack-SDK
#
# tests/rack_stubs.cpp supplies the two NanoVG symbols that rack.hpp's
# static-init color constants need; nothing else from libRack is required, so
# the suite links and runs without the Rack binary.
TEST_SOURCES := tests/test_dsp.cpp tests/rack_stubs.cpp
TEST_BINARY := tests/run_tests

$(TEST_BINARY): $(TEST_SOURCES) src/dsp.hpp src/plugin.hpp
	$(CXX) -std=c++11 -g -O1 -Wall -Wextra -Wno-unused-parameter \
		-Isrc -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include \
		-o $@ $(TEST_SOURCES)

test: $(TEST_BINARY)
	./$(TEST_BINARY)

cleantest:
	rm -f $(TEST_BINARY)

.PHONY: test cleantest
