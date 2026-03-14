# Plugin Name
PLUGIN_NAME=HyprWindowShade

# Source Files
SOURCE_FILES=main.cpp

# Compiler
CXX=g++

# COMPILER FLAGS
# We are dropping GLEW completely. Native GLESv2 avoids all redeclaration conflicts.
CXXFLAGS=-shared -fPIC -O3 -std=c++23 

# INCLUDE PATHS
# pkg-config handles base paths; we manually add protocols/include for compatibility.
INCLUDES=$(shell pkg-config --cflags hyprland pixman-1 libdrm) \
         -I/usr/include/hyprland/protocols \
         -I/usr/include/hyprland/include

# LIBRARIES
# Linking against GLESv2 instead of GLEW to stop header conflicts.
LIBS=-lGLESv2 -lEGL -lGL

# Build Rule
all:
	$(CXX) $(CXXFLAGS) $(SOURCE_FILES) -o $(PLUGIN_NAME).so $(INCLUDES) $(LIBS)

# Clean Rule
clean:
	rm -f $(PLUGIN_NAME).so