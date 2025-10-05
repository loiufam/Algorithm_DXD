#!/bin/bash

# 批量转换脚本

if [ $# -lt 3 ]; then
    echo "Usage: $0 <input_folder> <output_folder> <read_mode>"
    exit 1
fi

INPUT_FOLDER=$1
OUTPUT_FOLDER=$2
READ_MODE=$3

# 创建输出文件夹
# mkdir -p "$OUTPUT_FOLDER"

# 日志文件
LOG_FILE="zdd_compile_log.txt"
echo "Batch conversion started at $(date)" > "$LOG_FILE"
echo "" >> "$LOG_FILE"

# 计数器
count=0
success=0

# 遍历输入文件夹中的所有文件
for input_file in "$INPUT_FOLDER"/*; do
    if [ -f "$input_file" ]; then
        filename=$(basename "$input_file")
        name="${filename%.*}"
        output_file="$OUTPUT_FOLDER/${name}.zdd"
        
        echo "Processing: $name" >> "$LOG_FILE"
        
        # 记录开始时间
        # start_time=$(date +%s.%N)
        
        # 执行转换
        if ./cudd_matrix_compiler "$input_file" "$output_file" "$READ_MODE" 3>&1 | tee -a "$LOG_FILE"; then
            # end_time=$(date +%s.%N)
            # elapsed=$(echo "$end_time - $start_time" | bc)
            # echo "$filename: SUCCESS ($elapsed seconds)" >> "$LOG_FILE"
            ((success++))
        else
            echo "$name: FAILED" >> "$LOG_FILE"
        fi
        echo "===========================================" >> "$LOG_FILE"
        
        ((count++))
    fi
done

echo "" >> "$LOG_FILE"
echo "Conversion completed at $(date)" >> "$LOG_FILE"
echo "Total files: $count, Success: $success, Failed: $((count - success))" >> "$LOG_FILE"

cat "$LOG_FILE"
