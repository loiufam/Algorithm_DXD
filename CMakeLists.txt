cmake_minimum_required(VERSION 3.16)

project(AlgorithmDXD VERSION 1.0)


# 设置 C++ 标准版本
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─────────────────────────────────────────────
# macOS / Homebrew 友好：自动补全 GMP / libomp / TBB 搜索路径
# ─────────────────────────────────────────────
if(APPLE)
    # 检测 Homebrew 安装根目录（Apple Silicon: /opt/homebrew，Intel: /usr/local）
    if(EXISTS "/opt/homebrew")
        set(HOMEBREW_PREFIX "/opt/homebrew" CACHE PATH "Homebrew prefix")
    elseif(EXISTS "/usr/local")
        set(HOMEBREW_PREFIX "/usr/local" CACHE PATH "Homebrew prefix")
    endif()

    if(DEFINED HOMEBREW_PREFIX)
        list(APPEND CMAKE_PREFIX_PATH
            "${HOMEBREW_PREFIX}"
            "${HOMEBREW_PREFIX}/opt/libomp"
            "${HOMEBREW_PREFIX}/opt/tbb"
            "${HOMEBREW_PREFIX}/opt/gmp"
        )
        include_directories(SYSTEM "${HOMEBREW_PREFIX}/include")
        link_directories("${HOMEBREW_PREFIX}/lib")

        # AppleClang 不会默认启用 OpenMP；这里通过 libomp 手工接入
        if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            set(OpenMP_CXX_FLAGS "-Xpreprocessor -fopenmp -I${HOMEBREW_PREFIX}/opt/libomp/include"
                CACHE STRING "" FORCE)
            set(OpenMP_CXX_LIB_NAMES "omp" CACHE STRING "" FORCE)
            set(OpenMP_omp_LIBRARY "${HOMEBREW_PREFIX}/opt/libomp/lib/libomp.dylib"
                CACHE FILEPATH "" FORCE)
        endif()
    endif()
endif()

# Add include directories
include_directories(${PROJECT_SOURCE_DIR}/include)

# Common sources
set(COMMON_SOURCES
    src/DancingMatrix.cpp
    src/DXZSolver.cpp
    src/DXDSolver.cpp
    # DynamicGraph.cpp 通过 #include 在 DancingMatrix.cpp 中引入，故不再单独参与编译
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
    # macOS clang 不支持 static-libasan
    if(APPLE)
        target_link_options(main PRIVATE -fsanitize=address)
    else()
        target_link_options(main PRIVATE -fsanitize=address -static-libasan)
    endif()
endif()

# ==========================================
# 4. P1 修复回归测试
# ==========================================
add_executable(test_p1_fixes test/test_p1_fixes.cpp ${COMMON_SOURCES})
if(GMP_LIB AND GMPXX_LIB)
    target_link_libraries(test_p1_fixes PUBLIC ${GMPXX_LIB} ${GMP_LIB})
endif()
if(OpenMP_CXX_FOUND)
    target_link_libraries(test_p1_fixes PUBLIC OpenMP::OpenMP_CXX)
endif()
if(TBB_FOUND)
    target_link_libraries(test_p1_fixes PUBLIC TBB::tbb)
endif()

enable_testing()
add_test(NAME p1_fixes COMMAND test_p1_fixes)

# mkdir build && cd build
# cmake .. -DCMAKE_BUILD_TYPE=Release
# make -j