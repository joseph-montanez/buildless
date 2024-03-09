cmake_minimum_required(VERSION 3.10) # Specify the minimum version for CMake

project(ServerProject VERSION 1.0 DESCRIPTION "Simple Server Project") # Define the project name and version

set(CMAKE_C_STANDARD 99) # Set C standard to C99

# Add executable target with source files
add_executable(server server.c)

# Optional: specify include directories
# target_include_directories(server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
