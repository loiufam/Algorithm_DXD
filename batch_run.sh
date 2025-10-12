#!/bin/bash

# 输出CSV文件
OUTPUT="../multi_dxd.csv"

echo "文件名,线程数,算法耗时(秒),求解个数,最大分块数" > "$OUTPUT"

# 定义线程池大小列表
THREADS=(8 16 24)

# 固定输入文件夹路径（请根据你的项目路径修改）
BASE_DIR="../data"

# cd "$BASE_DIR" || { echo "❌ 无法进入目录 $BASE_DIR"; exit 1; }

# 遍历所有文件夹
for dir in "$BASE_DIR"/*/; do
    dir_name=$(basename "$dir")

    # 根据文件夹前缀确定 read_mode
    case $dir_name in
        e*) read_mode=1 ;;
        s*) read_mode=2 ;;
        g*) read_mode=3 ;;
        *) echo "[WARN] 未识别的文件夹: $dir_name 跳过"; continue ;;
    esac

    echo "[INFO] 当前文件夹: $dir_name (read_mode=$read_mode)"

  # 遍历每个输入文件（.txt 或 .ec）
  for input_file in "$dir"*.{txt,ec}; do
    [[ -e "$input_file" ]] || continue
    file_name=$(basename "$input_file")
    base_name="${file_name%.*}" # 去掉后缀

    for poolSize in "${THREADS[@]}"; do
      echo "运行文件: $file_name, 模式: $read_mode, 线程数: $poolSize"
      
      # 执行算法并捕获输出
      output=$(./main "$input_file" dxd_p "$read_mode" "$poolSize" 2>&1)

      # 提取关键信息
      time=$(echo "$output" | grep -oE '多线程算法计时: [0-9.]+' | grep -oE '[0-9.]+')
      sols=$(echo "$output" | grep -oE '多线程DXD搜索解个数: [0-9]+' | grep -oE '[0-9]+')
      blocks=$(echo "$output" | grep -oE '最大分块数为: [0-9]+' | grep -oE '[0-9]+')

      # 写入CSV
      echo "$base_name,$poolSize,$time,$sols,$blocks" >> "$OUTPUT"
    done
  done
  cd ..
done

echo "所有运行完成，结果已保存到 $OUTPUT"
