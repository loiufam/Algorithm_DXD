# CUDD路径配置
CUDD_DIR = /usr/local
CUDD_INCLUDE = $(CUDD_DIR)/include
CUDD_LIB = $(CUDD_DIR)/lib

# 编译器设置
CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall
INCLUDES = -I$(CUDD_INCLUDE)
LDFLAGS = -L$(CUDD_LIB)
LIBS = -lcudd -lm

# 目标文件
TARGET = cudd_matrix_compiler
SOURCE = matrix_to_zdd_cudd.cpp

# 默认目标
all: $(TARGET)

# 编译主程序
$(TARGET): $(SOURCE)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $< $(LDFLAGS) $(LIBS)

# 静态链接版本（更便携）
static: $(SOURCE)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(TARGET) $< \
		$(CUDD_LIB)/libcudd.a -lm -static-libstdc++

# 测试
test: $(TARGET)
	@echo "Creating test matrix..."
	@echo "5 3" > test_matrix.txt
	@echo "3 1 2 3" >> test_matrix.txt
	@echo "2 2 4" >> test_matrix.txt
	@echo "2 3 5" >> test_matrix.txt
	@echo "Running compiler..."
	./$(TARGET) test_matrix.txt test_output.zdd 3 test_output.dot
	@echo "Test completed!"

# 可视化测试结果
visualize: test
	dot -Tpng test_output.dot -o test_output.png
	@echo "Visualization saved to test_output.png"

# 清理
clean:
	rm -f $(TARGET) *.o test_matrix.txt test_output.zdd test_output.dot test_output.png

# 帮助信息
help:
	@echo "CUDD Matrix to ZDD Compiler - Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  make            - 编译程序"
	@echo "  make static     - 编译静态链接版本"
	@echo "  make test       - 运行测试"
	@echo "  make visualize  - 测试并生成可视化"
	@echo "  make clean      - 清理生成文件"
	@echo "  make install    - 安装CUDD库（需要sudo）"
	@echo ""
	@echo "Configuration:"
	@echo "  CUDD_DIR = $(CUDD_DIR)"
	@echo ""
	@echo "Usage:"
	@echo "  ./$(TARGET) <input_matrix> <output_zdd> <read_mode> [output_dot]"

# 安装CUDD
install-cudd:
	@echo "Installing CUDD..."
	@if [ ! -d "cudd" ]; then \
		git clone https://github.com/ivmai/cudd.git; \
	fi
	cd cudd && \
		./configure --prefix=$(CUDD_DIR) --enable-silent-rules --enable-obj && \
		make && \
		sudo make install
	@echo "CUDD installed to $(CUDD_DIR)"

# 检查CUDD是否已安装
check-cudd:
	@if [ ! -f "$(CUDD_INCLUDE)/cudd.h" ]; then \
		echo "Error: CUDD not found at $(CUDD_DIR)"; \
		echo "Please install CUDD or update CUDD_DIR in Makefile"; \
		echo "Run 'make install-cudd' to install CUDD automatically"; \
		exit 1; \
	fi
	@echo "CUDD found at $(CUDD_DIR)"

.PHONY: all static test visualize clean help install-cudd check-cudd