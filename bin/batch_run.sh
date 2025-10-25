#!/bin/bash 

CSV_FILE="../exp_results.csv" 
TMP_FILE="./temp/tmp.csv" 
INPUT_DIR="../data" 
LOG_FILE="../logs/experiment.log"

declare -A COL_MAP=( 
	[dlx]=4 
	[dxz]=5 
	[dxd]=7 
	[mdxd]=8 
) 

# 超时时间（秒） 
TIMEOUT=1200 # 例如：每个任务最多运行20分钟 

# 初始化日志文件 
mkdir -p "$(dirname "$LOG_FILE")" 
echo "========================================" > "$LOG_FILE" 
echo "实验开始时间: $(date '+%Y-%m-%d %H:%M:%S')" >> "$LOG_FILE" 
echo "========================================" >> "$LOG_FILE"

run_algorithm() 
{ 
	local algo="$1" 
	local file="$2"
	local read_mode="$3" 
	local base_name="$4" 

	# 在日志文件中添加分隔标记 
	echo "" >> "$LOG_FILE" 
	echo ">>> START: $base_name | $algo | $(date '+%Y-%m-%d %H:%M:%S')" >> "$LOG_FILE" 
	local exit_code=0 

	case "$algo" in 
		dlx) 
			timeout "$TIMEOUT" ./main dlx "$file" "$read_mode" >> "$LOG_FILE" 2>&1 
			exit_code=$? 
			;; 
		dxz) 
			timeout "$TIMEOUT" ./main dxz "$file" "$read_mode" >> "$LOG_FILE" 2>&1 
			exit_code=$? 
			;; 
		dxd) 
			timeout "$TIMEOUT" ./main dxd "$file" "$read_mode" >> "$LOG_FILE" 2>&1 
			exit_code=$? 
			;; 
		mdxd) 
			timeout "$TIMEOUT" ./main mdxd "$file" "$read_mode" >> "$LOG_FILE" 2>&1 
			exit_code=$? 
			;; 
		*) 
			echo "❌ 未知算法: $algo" >> "$LOG_FILE" 
			echo "<<< END: $base_name | $algo | ERROR" >> "$LOG_FILE" 
			return 1 
			;; 
		esac 
		# 添加结束标记 
		if [ $exit_code -eq 124 ]; then 
			echo "<<< END: $base_name | $algo | TIMEOUT" >> "$LOG_FILE" 
		else 
			echo "<<< END: $base_name | $algo | EXIT_CODE=$exit_code" >> "$LOG_FILE" 
		fi 
		return $exit_code 
}

parse_result() 
{ 
	local algo="$1" 
	local base_name="$2" 
	local content="" 

	# 从统一日志文件中提取指定算法的结果段 
	content=$(awk -v name="$base_name" -v algo="$algo" ' 
		/^>>> START:/ { 
		if ($3 == name && $5 == algo) { 
				capture=1 
				next 
			} 
		} 
		/^<<< END:/ { 
			if (capture) exit
		} 
		capture {print} 
		' "$LOG_FILE") 
	
	 if [ -z "$content" ]; then 
		echo ",," 
		return 1 
	 fi 

	 local time="" 
	 local sols="" 
	 local maxb="" 
	 
	 time=$(echo "$content" | grep -Eo 'Time[:=][[:space:]]*[0-9.]+(s| sec)?' | grep -Eo '[0-9.]+' | head -1) 
	 sols=$(echo "$content" | grep -Eo 'Solutions[:=][[:space:]]*[0-9]+' | grep -Eo '[0-9]+' | head -1) 
	 
	 case "$algo" in dxd|mdxd) 
	 	maxb=$(echo "$content" | grep -Eo 'Max[ _]?Blocks?[:=][[:space:]]*[0-9]+' | grep -Eo '[0-9]+' | head -1) ;; 
	 esac 

	 # 时间保留四位小数，过小则标记为 <1ms 
	 if [[ -n "$time" ]]; then 
	 	if (( $(echo "$time < 0.001" | bc -l) )); then 
	 		time="<1ms" 
		else 
			time=$(printf "%.4f" "$time") 
	 	fi 
	 fi 
	 echo "$time,$sols,$maxb" 
}

update_csv() {
	local name="$1"
	local algo="$2"
	local time="$3"
	local sols="$4"
	local maxb="$5"

	local col="${COL_MAP[$algo]}"
	[ -z "$col" ] && echo "❌ 未知算法列: $algo" && return

	mkdir -p "$(dirname "$TMP_FILE")"

	awk -F, -v OFS=',' \
		-v name="$name" \
		-v algo_col="$col" \
		-v algo="$algo" \
		-v time="$time" \
		-v sol="$sols" \
		-v maxb="$maxb" '
	NR==1 {print $0; next}
	$1 == name {
		if (time == "timeout") 
			$algo_col = "timeout";
		else if (time == "")
			$algo_col = "error";
		else 
			$algo_col = time;
		
		$11 = sol;

		if ((algo == "dxd" || algo == "mdxd") && maxb != "")
			$12 = maxb
	}
	{print $0}
	' "$CSV_FILE" > "$TMP_FILE"

	mv "$TMP_FILE" "$CSV_FILE"
}

# 遍历所有文件夹 
for dir in "$INPUT_DIR"/*/; do 
	dir_name=$(basename "$dir") 

	# 根据文件夹前缀确定 read_mode 
	case $dir_name in 
		# e*) read_mode=1 ;; 
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

	echo "==============================" 
	echo "📂 文件: $file_name" 

	for algo in dlx dxz; do 
		echo "▶️ 运行算法: $algo ..."
		# 执行算法并捕获输出 
		run_algorithm "$algo" "$input_file" "$read_mode" "$base_name" 
		exit_code=$? 

		# 判断超时或运行结果 
		if [ $exit_code -eq 124 ]; then 
			echo "⚠️ 超时: $file_name ($algo)" 
			update_csv "$file_name" "$algo" "timeout" "" "" 
			continue 
		fi 

		# 提取关键信息 
		IFS=',' read -r time sols maxb <<< "$(parse_result "$algo" "$base_name")" 

		if [ -z "$time" ]; then 
			echo "⚠️ 未能解析结果，跳过 $algo" update_csv "$file_name" "$algo" "" "" "" 
			continue 
		fi 

		# 写入CSV 
		update_csv "$base_name" "$algo" "$time" "$sols" "$maxb" 
		echo "✅ 已更新 $base_name 的 $algo 结果 (time=$time, sols=$sols, maxb=$maxb" 
		done 
	done 
done 

echo "========================================" >> "$LOG_FILE" 
echo "🎯 所有实验数据已更新到 $CSV_FILE" 
echo "📋 完整日志已保存到 $LOG_FILE"