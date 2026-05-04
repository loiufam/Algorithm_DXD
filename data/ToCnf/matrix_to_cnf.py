"""
精确覆盖问题到SAT的转换器
支持四种编码: Pairwise, Ladder, Bitwise, Matrix
输出DIMACS CNF格式，支持命令行参数批量处理
"""

import os
import time
import math
import argparse
import csv
from pathlib import Path
from typing import List, Tuple, Set

TIME_COLUMN_INDEX = 16  
INSTANCE_COLUMN = "Instance"
TIME_COLUMN = "cnf_compile(s)"
log_file = "log.txt"

def read_exact_cover_matrix(filepath: str) -> Tuple[List[Set[int]], int, int]:
    """
    自动识别并读取精确覆盖矩阵文件
    
    自动识别的三种模式：
    模式 1: 首行含 "=" 或以 "c " 开头，如 "c n = cols , m = rows"，跳过第二行，数据行跳过 1 个 id。
    模式 2: 首行 "cols rows"，数据行 "id count col1 col2 ..."
    模式 3: 首行 "cols rows"，数据行 "count col1 col2 ..."
    
    返回：(集合列表, 列数/点数, 行数)
    """
    with open(filepath, 'r') as f:
        # 读取第一行并去首尾空白
        first_line = f.readline().strip()
        while not first_line:  # 防御性跳过文件开头的空行
            first_line = f.readline().strip()
            
        read_mode = -1
        start_idx = -1
        
        # 1. 探测模式 1
        if "=" in first_line or first_line.startswith("c "):
            read_mode = 1
            tokens = first_line.replace(",", "").split()
            cols = int(tokens[3])
            rows = int(tokens[6])
            f.readline()  # 模式 1 固定跳过第二行
            start_idx = 1 # 模式 1 数据行固定跳过1个 token (row_id)
        else:
            # 首行是 "cols rows" (模式 2 或 3)
            tokens = first_line.split()
            cols = int(tokens[0])
            rows = int(tokens[1])
            
        sets = []
        
        # 2. 逐行读取与模式探测
        for line in f:
            line = line.strip()
            if not line:
                continue
                
            tokens = line.split()
            if not tokens:
                continue
                
            # 探测第一行有效数据以区分模式 2 和模式 3
            if read_mode == -1:
                # 模式 3: [count] [col1] [col2] ... (总 token 数等于 count + 1)
                if len(tokens) == int(tokens[0]) + 1:
                    read_mode = 3
                    start_idx = 1
                # 模式 2: [id] [count] [col1] [col2] ... (总 token 数等于 count + 2)
                elif len(tokens) >= 2 and len(tokens) == int(tokens[1]) + 2:
                    read_mode = 2
                    start_idx = 2
                else:
                    print(f"警告: 无法自动识别数据行格式，默认跳过1个标识符。当前行: {line}")
                    read_mode = 3
                    start_idx = 1
            
            # 对模式 2 和 3 进行 count 校验
            if read_mode == 2:
                count = int(tokens[1])
                if count <= 0: continue
            elif read_mode == 3:
                count = int(tokens[0])
                if count <= 0: continue
                
            # 读取列号
            col_set = set()
            for i in range(start_idx, len(tokens)):
                col = int(tokens[i])
                if 1 <= col <= cols:
                    col_set.add(col)
                    
            if col_set:
                sets.append(col_set)
                
    return sets, cols, rows

def matrix_encoding(n: int, vars: List[int], current_aux: int) -> Tuple[List[List[int]], int]:
    clauses = []

    p = math.ceil(math.sqrt(n))
    q = math.ceil(n / p)
    
    clauses.append(list(vars))  # At-least-one
    
    u_vars = list(range(current_aux, current_aux + p))
    current_aux += p
    v_vars = list(range(current_aux, current_aux + q))
    current_aux += q
    
    # U Exactly-One (Pairwise)
    clauses.append(list(u_vars))
    for i in range(p):
        for j in range(i + 1, p):
            clauses.append([-u_vars[i], -u_vars[j]])
            
    # V Exactly-One (Pairwise)
    clauses.append(list(v_vars))
    for i in range(q):
        for j in range(i + 1, q):
            clauses.append([-v_vars[i], -v_vars[j]])
            
    # Linking clauses
    for k_idx, var in enumerate(vars):
        i = k_idx // q
        j = k_idx % q
        clauses.append([-var, u_vars[i]])
        clauses.append([-var, v_vars[j]]) 

    return clauses, current_aux

def encode_exactly_one(vars: List[int], current_aux: int, encoding: str) -> Tuple[List[List[int]], int]:
    """
    对一组变量进行Exactly-One约束编码
    
    参数：
        vars: 参与约束的布尔变量集合
        current_aux: 当前可用的辅助变量起始编号
        encoding: 编码方式 ('pairwise', 'ladder', 'bitwise', 'matrix')
        
    返回：
        (子句列表, 更新后的下一个可用辅助变量编号)
    """
    n = len(vars)
    if n == 0:
        return [[]], current_aux
    if n == 1:
        return [[vars[0]]], current_aux

    clauses = []

    if encoding == 'pairwise':
        if n > 10:
            return matrix_encoding(n, vars, current_aux)
        
        # At-least-one
        clauses.append(list(vars))
        # At-most-one
        for i in range(n):
            for j in range(i + 1, n):
                clauses.append([-vars[i], -vars[j]])

    elif encoding == 'ladder':
        aux_vars = list(range(current_aux, current_aux + n - 1))
        current_aux += n - 1
        
        clauses.append([-aux_vars[0], vars[0], vars[1]])
        clauses.append([aux_vars[0], -vars[0]])
        clauses.append([aux_vars[0], -vars[1]])
        
        for i in range(2, n):
            if i < n - 1:
                clauses.append([-aux_vars[i-1], aux_vars[i-2], vars[i]])
                clauses.append([aux_vars[i-1], -aux_vars[i-2]])
                clauses.append([aux_vars[i-1], -vars[i]])
            else:
                clauses.append([aux_vars[i-2], vars[i]])
        
        clauses.append([-vars[0], -vars[1]])
        for i in range(2, n):
            clauses.append([-aux_vars[i-2], -vars[i]])
            
        if n > 2:
            clauses.append([aux_vars[-1]])

    elif encoding == 'bitwise':
        k = math.ceil(math.log2(n))
        clauses.append(list(vars))  # At-least-one
        if k > 0:
            aux_vars = list(range(current_aux, current_aux + k))
            current_aux += k
            
            for idx, var in enumerate(vars):
                for j in range(k):
                    bit = (idx >> j) & 1
                    if bit == 1:
                        clauses.append([-var, aux_vars[j]])
                    else:
                        clauses.append([-var, -aux_vars[j]])

    elif encoding == 'matrix':
        return matrix_encoding(n, vars, current_aux)

    return clauses, current_aux

def exact_cover_to_cnf(sets: List[Set[int]], num_points: int, encoding: str) -> Tuple[List[List[int]], int]:
    """
    将整个精确覆盖实例转换为CNF。
    """
    all_clauses = []
    num_vars = len(sets)
    current_aux = num_vars + 1
    
    for v in range(1, num_points + 1):
        covering_vars = [i + 1 for i, s in enumerate(sets) if v in s]
        
        clauses, current_aux = encode_exactly_one(covering_vars, current_aux, encoding)
        all_clauses.extend(clauses)
            
    return all_clauses, current_aux - 1

def write_dimacs_cnf(clauses: List[List[int]], num_vars: int, output_file: str):
    """
    将CNF子句写入DIMACS格式文件
    
    DIMACS格式:
    - 注释行以'c'开头
    - 问题行: p cnf <变量数> <子句数>
    - 子句行: 每个文字用空格分隔，以'0'结尾
    """
    with open(output_file, 'w') as f:
    
        # 写入问题行
        f.write(f"p cnf {num_vars} {len(clauses)}\n")
        
        # 写入所有子句
        for clause in clauses:
            if clause:
                f.write(' '.join(map(str, clause)) + ' 0\n')
            else:
                f.write('0\n')  # 空子句

def main():
    parser = argparse.ArgumentParser(description="精确覆盖问题到SAT(CNF)转换器 - 支持单文件与批量四种编码")
    parser.add_argument('-i', '--input', type=str, required=True, help="输入路径 (单个文件或包含多个实例的文件夹)")
    parser.add_argument('-o', '--output', type=str, required=True, help="输出基准文件夹的路径")
    args = parser.parse_args()

    input_path = Path(args.input)
    output_folder = Path(args.output)
    
    if not input_path.exists():
        print(f"错误：输入路径 '{input_path}' 不存在。")
        return
        
    encodings = ['pairwise', 'ladder', 'bitwise', 'matrix']
    
    # 创建各编码的子目录
    for enc in encodings:
        (output_folder / enc).mkdir(parents=True, exist_ok=True)
        
    csv_file = output_folder / "conversion_times.csv"
    # 追加模式打开CSV，如果文件不存在或为空，则写入表头
    write_header = not csv_file.exists() or csv_file.stat().st_size == 0
    with open(csv_file, 'a', newline='') as f:
        writer = csv.writer(f)
        if write_header:
            writer.writerow(["Instance", "Pairwise_Time(s)", "Ladder_Time(s)", "Bitwise_Time(s)", "Matrix_Time(s)"])

    # 判断是单文件还是文件夹，收集需要处理的文件列表
    if input_path.is_file():
        files_to_process = [input_path]
        print(f"单文件模式：准备转换 {input_path.name}...\n")
    elif input_path.is_dir():
        files_to_process = [f for f in input_path.iterdir() if f.is_file() and f.suffix in ['.txt', '.ec', '']]
        print(f"批量模式：找到 {len(files_to_process)} 个文件，开始批量转换...\n")
    else:
        print(f"错误：不支持的路径格式。")
        return
    
    for file_path in files_to_process:
        print(f"正在处理: {file_path.name}")
        sets, num_points, num_rows = read_exact_cover_matrix(str(file_path))
        
        times = []
        for enc in encodings:
            start_time = time.time()

            out_file = output_folder / enc / f"{file_path.stem}.cnf"
            if os.path.exists(out_file):
                print(f"  - {enc.capitalize():<10} 输出文件已存在，跳过转换。")
                continue
            
            clauses, total_vars = exact_cover_to_cnf(sets, num_points, enc)

            write_dimacs_cnf(clauses, total_vars, str(out_file))
            
            elapsed = round(time.time() - start_time, 4)
            times.append(elapsed)
            
            print(f"  - {enc.capitalize():<10} 耗时: {elapsed:.4f}s | 变量数: {total_vars} | 子句数: {len(clauses)}")
            
        with open(csv_file, 'a', newline='') as f:
            writer = csv.writer(f)
            writer.writerow([file_path.stem] + times)
            
        print("-" * 60)
        
    print(f"转换全部完成！各编码CNF已存储至 {output_folder}，耗时报表已保存至 {csv_file}")

if __name__ == "__main__":
    main()