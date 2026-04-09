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
    src/DynamicGraph.cpp
    # src/TreapETT.cpp
    src/DXDTime.cpp
    src/ComponentDetector.cpp
    src/SplayTree.cpp
)

# Add executable target
# Main target
add_executable(main src/main.cpp ${COMMON_SOURCES})
# add_executable(main_dxd src/main_dxd.cpp ${COMMON_SOURCES})
# add_executable(main_base src/main_base.cpp ${COMMON_SOURCES})

# Optional test targets (enable manually if needed)
# add_executable(test1 test/test1.cpp ${COMMON_SOURCES})
# add_executable(test2 test/test2.cpp ${COMMON_SOURCES})
# add_executable(test3 test/test3.cpp ${COMMON_SOURCES})
# add_executable(test_ test/test_matrix_to_graph.cpp ${COMMON_SOURCES})

# AddressSanitizer only in Debug mode
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Building with AddressSanitizer (Debug mode)")
    target_compile_options(main PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(main PRIVATE -fsanitize=address -static-libasan)
endif()

# mkdir build && cd build
# cmake .. -DCMAKE_BUILD_TYPE=Release
# make -j

# OpenMP
find_package(OpenMP REQUIRED)
if(OpenMP_CXX_FOUND)
    target_link_libraries(main PUBLIC OpenMP::OpenMP_CXX)
    # target_link_libraries(main_dxd PUBLIC OpenMP::OpenMP_CXX)
    # target_link_libraries(test1 PUBLIC OpenMP::OpenMP_CXX)
    # target_link_libraries(test2 PUBLIC OpenMP::OpenMP_CXX)
    # target_link_libraries(test3 PUBLIC OpenMP::OpenMP_CXX)
    # target_link_libraries(test_ PUBLIC OpenMP::OpenMP_CXX)
endif()