# Makefile for Lunar Viewer
# Simple build system alternative to CMake

CXX = g++
CXXFLAGS = -std=c++17 -O3 -march=native -Wall
INCLUDES = -I/usr/include
LDFLAGS = -lGL -lGLEW -lglfw -lm

TARGET = lunar_viewer
SRC = src/lunar_viewer.cpp
OBJ = $(SRC:.cpp=.o)

# Default target
all: $(TARGET)

# Build executable
$(TARGET): $(OBJ)
	@echo "Linking $(TARGET)..."
	$(CXX) $(OBJ) -o $(TARGET) $(LDFLAGS)
	@echo "Build complete! Run with: ./$(TARGET)"

# Compile source files
%.o: %.cpp
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Clean build files
clean:
	@echo "Cleaning..."
	rm -f $(OBJ) $(TARGET)
	@echo "Clean complete!"

# Run the program
run: $(TARGET)
	./$(TARGET)

# Install dependencies (Ubuntu/Debian)
install-deps:
	@echo "Installing dependencies..."
	sudo apt-get update
	sudo apt-get install -y build-essential cmake
	sudo apt-get install -y libglew-dev libglfw3-dev libglm-dev
	@echo "Dependencies installed!"

# Install dependencies (Fedora/RHEL)
install-deps-fedora:
	@echo "Installing dependencies..."
	sudo dnf install -y gcc-c++ cmake
	sudo dnf install -y glew-devel glfw-devel glm-devel
	@echo "Dependencies installed!"

# Install dependencies (Arch)
install-deps-arch:
	@echo "Installing dependencies..."
	sudo pacman -S --needed base-devel cmake
	sudo pacman -S --needed glew glfw glm
	@echo "Dependencies installed!"

.PHONY: all clean run install-deps install-deps-fedora install-deps-arch
