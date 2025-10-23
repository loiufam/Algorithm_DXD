"""
精确覆盖问题到SAT的转换器
支持Pairwise和Ladder编码, 输出DIMACS CNF格式
"""

from typing import List, Tuple, Set
import sys
import os
import time
log_file = "log.txt"

def read_exact_cover_matrix(filepath: str, read_mode: int = 1) -> Tuple[List[Set[int]], int, int]:
    """
    读取精确覆盖矩阵文件
    
    支持三种读取模式：
    read_mode = 1: 格式 "c n = cols , m = rows" 然后跳过一行
    read_mode = 2: 格式 "cols rows" 然后每行 "行号 count col1 col2 ..."
    read_mode = 3: 格式 "cols rows" 然后每行 "count col1 col2 ..."
    
    返回：(集合列表, 列数/点数)
    """
    with open(filepath, 'r') as f:
        # 读取第一行获取cols和rows
        first_line = f.readline().strip()
        tokens = first_line.replace(",", "").split()
        
        if read_mode == 1:
            # 格式: c n = cols , m = rows
            cols = int(tokens[3])
            rows = int(tokens[6])
            # 跳过第二行
            f.readline()
        else:
            # 格式: cols rows
            cols = int(tokens[0])
            rows = int(tokens[1])
        
        sets = []
        
        # 读取每一行
        for line in f:
            line = line.strip()
            if not line:
                continue
            
            tokens = line.split()
            
            if read_mode == 1:
                # 跳过第一个token（行号）
                start_idx = 1
            elif read_mode == 2:
                # 跳过前两个tokens（行号和count）
                if len(tokens) < 2:
                    continue
                count = int(tokens[1])
                if count <= 0:
                    continue
                start_idx = 2
            else:  # read_mode == 3
                # 跳过第一个token（count）
                if len(tokens) < 1:
                    continue
                count = int(tokens[0])
                if count <= 0:
                    continue
                start_idx = 1
            
            # 读取列号
            col_set = set()
            for i in range(start_idx, len(tokens)):
                col = int(tokens[i])
                if 1 <= col <= cols:
                    col_set.add(col)
            
            if col_set:
                sets.append(col_set)
        
    return sets, cols, rows


def exact_cover_to_cnf_pairwise(sets: List[Set[int]], num_points: int) -> Tuple[List[List[int]], int]:
    """
    Pairwise编码: 显式排除每对变量
    
    对于每个点v的exactly-one约束:
    - At-least-one: (x_1 ∨ x_2 ∨ ... ∨ x_n)
    - At-most-one: (¬x_i ∨ ¬x_j) for all i < j
    
    参数：
        sets: 集合列表
        num_points: 点的总数
    
    返回：(子句列表, 变量总数)
    """
    clauses = []
    num_vars = len(sets)
    
    for v in range(1, num_points + 1):
        # 找到包含点v的所有集合对应的变量
        covering_vars = [i + 1 for i, s in enumerate(sets) if v in s]
        
        if not covering_vars:
            # 无解情况
            clauses.append([])
            continue
        
        # At-least-one约束
        clauses.append(covering_vars)
        
        # At-most-one约束：排除所有变量对
        for i in range(len(covering_vars)):
            for j in range(i + 1, len(covering_vars)):
                clauses.append([-covering_vars[i], -covering_vars[j]])
    
    return clauses, num_vars


def exact_cover_to_cnf_ladder(sets: List[Set[int]], num_points: int) -> Tuple[List[List[int]], int]:
    """
    Ladder编码: 使用n-1个辅助变量
    
    辅助变量a_i表示：x_1 ∨ x_2 ∨ ... ∨ x_i为真
    构造：
    - a_2 ⇔ (x_1 ∨ x_2)
    - a_i ⇔ (a_{i-1} ∨ x_i) for i ≥ 3
    - a_n必须为真
    - At-most-one: (¬x_1 ∨ ¬x_2), (¬a_{i-1} ∨ ¬x_i) for i ≥ 3
    
    参数：
        sets: 集合列表
        num_points: 点的总数
    
    返回：(子句列表, 总变量数包括辅助变量)
    """
    clauses = []
    num_vars = len(sets)
    current_aux = num_vars + 1
    
    for v in range(1, num_points + 1):
        covering_vars = [i + 1 for i, s in enumerate(sets) if v in s]
        
        if not covering_vars:
            clauses.append([])
            continue
        
        n = len(covering_vars)
        
        if n == 1:
            # 唯一选择，直接设为真
            clauses.append([covering_vars[0]])
            continue
        
        # 分配辅助变量
        aux_vars = list(range(current_aux, current_aux + n - 1))
        current_aux += n - 1
        
        # a_2 ⇔ (x_1 ∨ x_2)
        clauses.append([-aux_vars[0], covering_vars[0], covering_vars[1]])
        clauses.append([aux_vars[0], -covering_vars[0]])
        clauses.append([aux_vars[0], -covering_vars[1]])
        
        # a_i ⇔ (a_{i-1} ∨ x_i) for i = 3 to n
        for i in range(2, n):
            if i < n - 1:
                # 中间的辅助变量
                clauses.append([-aux_vars[i-1], aux_vars[i-2], covering_vars[i]])
                clauses.append([aux_vars[i-1], -aux_vars[i-2]])
                clauses.append([aux_vars[i-1], -covering_vars[i]])
            else:
                # 最后一个：直接要求 (a_{n-1} ∨ x_n) 为真
                clauses.append([aux_vars[i-2], covering_vars[i]])
        
        # At-most-one约束
        clauses.append([-covering_vars[0], -covering_vars[1]])
        for i in range(2, n):
            clauses.append([-aux_vars[i-2], -covering_vars[i]])
        
        # 强制最后一个辅助变量为真
        if n > 2:
            clauses.append([aux_vars[-1]])
    
    return clauses, current_aux - 1


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


def matrix_to_cnf(input_file: str, output_file: str, read_mode: int = 1):
    """
    完整转换流程：精确覆盖矩阵 → DIMACS CNF
    
    参数：
        input_file: 输入文件路径（精确覆盖矩阵）
        output_file: 输出文件路径（DIMACS CNF格式）
        encoding: 编码方式，'pairwise'或'ladder'
        read_mode: 读取模式，1/2/3（见read_exact_cover_matrix函数说明）
    """
    # 读取输入
    sets, num_points, num_rows = read_exact_cover_matrix(input_file, read_mode)
    print(f"✓ 读取到 {len(sets)} 个集合（行），覆盖 {num_points} 个点（列）")
    
    encoding = 'pairwise'  # 默认编码方式
    # 根据行数选择编码方式
    if num_rows > 200:
        encoding = 'ladder'
        print(f"⚠️ 行数较多，自动切换到Ladder编码")

    # 转换为CNF
    if encoding == 'pairwise':
        clauses, num_vars = exact_cover_to_cnf_pairwise(sets, num_points)
        print(f"✓ 使用Pairwise编码")
    elif encoding == 'ladder':
        clauses, num_vars = exact_cover_to_cnf_ladder(sets, num_points)
        print(f"✓ 使用Ladder编码")
    else:
        raise ValueError(f"不支持的编码方式: {encoding}。请使用'pairwise'或'ladder'")
    
    print(f"✓ 生成 {len(clauses)} 个子句，{num_vars} 个变量")
    
    # 如果output_file是文件夹
    # if os.path.isdir(output_file):
    #     input_file_name = os.path.basename(input_file)
    #     output_file = os.path.join(output_file, f"{os.path.splitext(input_file_name)[0]}.cnf")

    # 写入DIMACS格式
    # write_dimacs_cnf(clauses, num_vars, output_file)
    # print(f"✓ CNF文件已保存到: {output_file}")
    
    # 统计信息
    clause_sizes = [len(c) for c in clauses if c]
    if clause_sizes:
        print(f"\n统计信息：")
        print(f"  - 最小子句大小: {min(clause_sizes)}")
        print(f"  - 最大子句大小: {max(clause_sizes)}")
        print(f"  - 平均子句大小: {sum(clause_sizes)/len(clause_sizes):.2f}")

def batch_convert(input_folder: str, output_folder: str, read_mode: int = 1):
    """
    批量转换文件夹中的所有精确覆盖矩阵文件为DIMACS CNF格式
    
    参数：
        input_folder: 输入文件夹路径
        output_folder: 输出文件夹路径
        read_mode: 读取模式，1/2/3（见read_exact_cover_matrix函数说明）
    """

    if not os.path.exists(output_folder):
        os.makedirs(output_folder)

    with open(log_file, "a", encoding="utf-8") as log:
        for filename in os.listdir(input_folder):
            if filename.endswith(".txt") or filename.endswith(".ec"):
                input_file = os.path.join(input_folder, filename)
                output_file = os.path.join(output_folder, f"{os.path.splitext(filename)[0]}.cnf")
                print(f"\n转换文件: {input_file} → {output_file}")

                start = time.time()
                matrix_to_cnf(input_file, output_file, read_mode)
                duration = time.time() - start

                base_name = os.path.splitext(filename)[0]

                # 写入日志
                log.write(f"{base_name} {duration:.4f}\n")
                log.flush()

                print(f"  - 转换完成，耗时: {duration:.4f} 秒")

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: python matrix_to_cnf.py <input_file> <output_file> <read_mode> [batch_mode]")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]
    read_mode = int(sys.argv[3]) if len(sys.argv) > 3 else 1

    if len(sys.argv) > 4 and sys.argv[4] == "-b":
        batch_convert(input_file, output_file, read_mode)
        print(f"✓ 批量转换完成，输出文件夹: {output_file}")
    else:
        matrix_to_cnf(input_file, output_file, read_mode)
        print(f"✓ 转换完成!")