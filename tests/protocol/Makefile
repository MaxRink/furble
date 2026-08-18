ROOT := $(abspath $(CURDIR)/../..)
BUILD := $(CURDIR)/build
STUBS := $(CURDIR)/stubs
INCLUDES := -I$(STUBS) -I$(ROOT)/include -I$(ROOT)/lib/preferences
COMMON_DEPS := $(CURDIR)/protocol_common.h \
	$(ROOT)/include/FurbleCompanion.h \
	$(ROOT)/include/FurbleSettings.h \
	$(ROOT)/include/FurbleSpinValue.h \
	$(ROOT)/include/interval.h \
	$(ROOT)/src/FurbleCompanion.cpp \
	$(ROOT)/src/FurbleSettings.cpp

CXX ?= c++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Wpedantic

.PHONY: all generate test clean format-check

all: $(BUILD)/protocol_test $(BUILD)/generate_fixtures

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/protocol_test: protocol_test.cpp $(COMMON_DEPS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(BUILD)/generate_fixtures: generate_fixtures.cpp $(COMMON_DEPS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

generate: $(BUILD)/generate_fixtures
	$(BUILD)/generate_fixtures $(ROOT) $(CURDIR)/golden

test: $(BUILD)/protocol_test
	$(BUILD)/protocol_test $(ROOT)

format-check:
	clang-format --dry-run --Werror --style=file \
		generate_fixtures.cpp protocol_common.h protocol_test.cpp \
		stubs/*.h stubs/freertos/FreeRTOS.h

clean:
	rm -rf $(BUILD)
