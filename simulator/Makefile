CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -I../common
BUILD_DIR ?= build
TARGET := $(BUILD_DIR)/meshcore_tdeck_sim
SOURCES := main_sim.cpp ../common/app/app_controller.cpp ../common/app/app_types.cpp ../common/app/mock_data.cpp
HEADERS := ../common/app/app_controller.h ../common/app/app_types.h ../common/app/mock_data.h ../common/app/navigation.h

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

run: $(TARGET)
	$(TARGET)

clean:
	rm -rf $(BUILD_DIR)
