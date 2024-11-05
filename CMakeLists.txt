# CMakeLists.txt

cmake_minimum_required(VERSION 3.10)
project(DataIngestionModule)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=x86-64 -mtune=native -pthread -g")

# Include Directories
include_directories(include)

# Add Source Files
file(GLOB SOURCES "src/*.cpp")

# Create Executable for Data Ingestion
add_executable(data_ingestion src/main.cpp src/data_ingestion.cpp src/config.cpp)

# Link Libraries
target_link_libraries(data_ingestion pthread)

# Add executable for mock server
add_executable(mock_server mock_server/mock_server.cpp)

# Link libraries
target_link_libraries(mock_server pthread)

# Add executable for benchmarking
add_executable(ingestion_benchmark benchmarks/ingestion_benchmark.cpp src/data_ingestion.cpp src/config.cpp)
target_link_libraries(ingestion_benchmark pthread)
