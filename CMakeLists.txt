cmake_minimum_required(VERSION 3.10)

project(AlgorithmDXD VERSION 1.0)


# 设置 C++ 标准版本
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 设置使用 libc++
# set(CMAKE_CXX_STANDARD_LIBRARIES "-stdlib=libc++")

# Add include directories
include_directories(${PROJECT_SOURCE_DIR}/include)

# 启用 AddressSanitizer（调试内存错误）
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")

# Add source files
set(SOURCES0
    test/testUnionFind.cpp
)

set(MAIN_SOURCES
    src/main.cpp
    src/DancingMatrix.cpp
    src/DXZSolver.cpp
    src/DXDSolver.cpp
    src/ConnectedGraph.cpp 
    src/DynamicGraph.cpp  
    src/DXDTime.cpp
)

set(TEST_SOURCES1
    test/test1.cpp
    src/DancingMatrix.cpp
    src/DXZSolver.cpp
    src/ConnectedGraph.cpp 
    src/DXDTime.cpp 
)
set(TEST_SOURCES2
    test/test2.cpp
    src/DancingMatrix.cpp
    src/DXDSolver.cpp
    src/ConnectedGraph.cpp  
    src/DynamicGraph.cpp
    src/DXDTime.cpp
)
set(TEST_SOURCES3
    test/test_cg.cpp
    src/DancingMatrix.cpp
    src/DXDSolver.cpp
    src/DynamicGraph.cpp 
    src/ConnectedGraph.cpp   
    src/DXDTime.cpp
)
# Add executable target
# add_executable(main ${MAIN_SOURCES})
# 1 : DLX and DXZ 2 : singleDXD and MultiDXD 3 : UnionFind
# add_executable(test1 ${TEST_SOURCES1})
add_executable(test2 ${TEST_SOURCES2})
# add_executable(test ${TEST_SOURCES3})
# find_package(Threads REQUIRED)
# target_link_libraries(test PRIVATE Threads::Threads)