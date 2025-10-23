cmake_minimum_required(VERSION 3.10)

project(AlgorithmDXD VERSION 1.0)


# 设置 C++ 标准版本
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置使用 libc++
# set(CMAKE_CXX_STANDARD_LIBRARIES "-stdlib=libc++")

# Add include directories
include_directories(${PROJECT_SOURCE_DIR}/include)

# Common sources
set(COMMON_SOURCES
    src/DancingMatrix.cpp
    src/DXZSolver.cpp
    src/DXDSolver.cpp
    src/ConnectedGraph.cpp
    src/DynamicGraph.cpp
    src/TreapETT.cpp
    src/DXDTime.cpp
)

# Add executable target
# Main target
add_executable(main src/main.cpp ${COMMON_SOURCES})

# Optional test targets (enable manually if needed)
# add_executable(test1 test/test1.cpp ${COMMON_SOURCES})
# add_executable(test2 test/test2.cpp ${COMMON_SOURCES})
# add_executable(test_ett test/test_ett.cpp ${COMMON_SOURCES})

# AddressSanitizer only in Debug mode
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Building with AddressSanitizer (Debug mode)")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
endif()

# mkdir build && cd build
# cmake .. -DCMAKE_BUILD_TYPE=Release
# make -j

# OpenMP
find_package(OpenMP REQUIRED)
if(OpenMP_CXX_FOUND)
    target_link_libraries(main PUBLIC OpenMP::OpenMP_CXX)
    # target_link_libraries(test1 PUBLIC OpenMP::OpenMP_CXX)
    # target_link_libraries(test2 PUBLIC OpenMP::OpenMP_CXX)
    # target_link_libraries(test_ett PUBLIC OpenMP::OpenMP_CXX)
endif()