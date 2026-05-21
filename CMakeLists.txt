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

# ==========================================
# 1. 配置 GMP 高精度库链接 (处理超大模型计数)
# ==========================================
# 查找系统的 gmp 和 gmpxx 动态/静态库
find_library(GMP_LIB NAMES gmp REQUIRED)
find_library(GMPXX_LIB NAMES gmpxx REQUIRED)

if(GMP_LIB AND GMPXX_LIB)
    message(STATUS "Found GMP: ${GMP_LIB}")
    message(STATUS "Found GMPXX: ${GMPXX_LIB}")
    # 必须先链接 gmpxx (C++封装)，再链接 gmp (C底层库)
    target_link_libraries(main PUBLIC ${GMPXX_LIB} ${GMP_LIB})
endif()

# ==========================================
# 2. 配置 OpenMP (支持并行计算)
# ==========================================
find_package(OpenMP REQUIRED)
if(OpenMP_CXX_FOUND)
    target_link_libraries(main PUBLIC OpenMP::OpenMP_CXX)
endif()

# ==========================================
# 2.5 配置 TBB (支持任务并行)
# ==========================================
find_package(TBB REQUIRED)
if(TBB_FOUND)
    message(STATUS "Found TBB: ${TBB_VERSION}")
    target_link_libraries(main PUBLIC TBB::tbb)
endif()

# ==========================================
# 3. 配置 AddressSanitizer (仅 Debug 模式)
# ==========================================
# AddressSanitizer only in Debug mode
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Building with AddressSanitizer (Debug mode)")
    target_compile_options(main PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(main PRIVATE -fsanitize=address -static-libasan)
endif()

# mkdir build && cd build
# cmake .. -DCMAKE_BUILD_TYPE=Release
# make -j