import csv
import math

def is_valid_number(value):
    """
    判断给定的值是否为有效的数字。
    包含对空值、非数字字符串、NaN（非数字）和无穷大的排除。
    """
    # 检查是否为空或只有空格
    if not value or value.strip() == "":
        return False
    try:
        num = float(value)
        # 排除 NaN 和 Inf 等无效数学数值
        if math.isnan(num) or math.isinf(num):
            return False
        return True
    except ValueError:
        # 转换 float 失败，说明包含字母或其他非数字字符
        return False

def process_csv(input_file, output_file):
    total_scanned = 0
    total_written = 0
    
    try:
        with open(input_file, mode='r', encoding='utf-8') as csv_file, \
             open(output_file, mode='w', encoding='utf-8') as txt_file:
            
            # 使用 DictReader 自动识别列头
            reader = csv.DictReader(csv_file)
            
            # 校验列头是否存在
            if reader.fieldnames is None:
                print("错误: 这是一个空文件。")
                return
            if 'Instance' not in reader.fieldnames or 'DynDXD_T8' not in reader.fieldnames:
                print(f"错误: 找不到指定的列头。当前列头为: {reader.fieldnames}")
                return
            
            # 遍历每一行数据
            for row in reader:
                total_scanned += 1
                instance = row['Instance']
                matrix_time = row['DynDXD_T8']
                
                # 如果不是有效数值，将 Instance 写入 txt，并换行
                if not is_valid_number(matrix_time):
                    txt_file.write(f"{instance}\n")
                    total_written += 1
                    
        print(f"--- 处理完成 ---")
        print(f"总共扫描的数量: {total_scanned}")
        print(f"写入txt的数量: {total_written}")
        
    except FileNotFoundError:
        print(f"错误: 找不到文件 '{input_file}'，请检查文件路径。")
    except Exception as e:
        print(f"发生错误: {e}")

if __name__ == "__main__":
    # --- 请在这里修改为你的实际文件路径 ---
    INPUT_CSV = "./batch_results_20260511/DynDXD_t8_results.csv" 
    OUTPUT_TXT = "except_list_ddxd.txt"
    
    process_csv(INPUT_CSV, OUTPUT_TXT)