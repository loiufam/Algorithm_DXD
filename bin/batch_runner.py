#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import subprocess
import csv
import re
import sys
import argparse
from pathlib import Path
from datetime import datetime

# 配置
MAIN_EXECUTABLE = "./main"
INPUT_FOLDER = "../data/comparison_test"  
RESULTS_FOLDER = "../results/Main_Threads"

# 算法配置：(算法名, 命令参数, 列ID, 是否使用ett, 输出文件名)
ALGORITHMS = [
    # ("IDXD_S", ["dxd", "ett"], 9, True, "IDXD_S_results_11_30.csv"),
    ("IDXD_M", ["mdxd", "ett"], 10, True, "IDXD_M_results_.csv"),
    # ("DXD_S", ["dxd", "ig"], 7, True, "DXD_S_results_11_30.csv"),
    ("DXD_M", ["mdxd", "ig"], 8, True, "DXD_M_results_.csv"),
    ("MDLX", ["mdlx", "ett"], 11, True, "MDLX_results_.csv"),
    # ("DLX", ["dlx"], 4, True, "DLX_results_11_30.csv"),
    # ("DXZ", ["dxz"], 5, True, "DXZ_results_11_30.csv"),
]

# 多线程算法配置：(算法名, 命令参数, 是否使用ett, 线程数列表, 输出文件名)
THREAD_ALGORITHMS = [
    ("DXD_M", ["mdxd", "ig"], True, [4, 8, 16], "DXD_M_threads.csv"),
    ("IDXD_M", ["mdxd", "ett"], True, [4, 8, 16], "IDXD_M_threads.csv"),
    ("MDLX", ["mdlx", "ett"], True, [4, 8, 16], "MDLX_threads.csv"),
]

def parse_log_output(output):
    """解析算法输出的日志信息"""
    result = {
        'time': None,
        'solutions': None,
        'max_blocks': None,
        'status': 'success',
        'timeout': False 
    }

    # 检测是否包含“超时”
    if '超时' in output:
        result['timeout'] = True
        success = False
        return result
    
    # 匹配 Time: 0.1 s
    time_match = re.search(r'Time:\s*([\d.]+)\s*s', output)
    if time_match:
        result['time'] = float(time_match.group(1))
    
    # 匹配 Solutions: 100 或科学记数法 Solutions: 1.23e+10
    solutions_match = re.search(r'Solutions:\s*([\d.eE+\-]+)', output)
    if solutions_match:
        result['solutions'] = solutions_match.group(1)
    
    # 匹配 Max Blocks: 10
    blocks_match = re.search(r'Max Blocks:\s*(\d+)', output)
    if blocks_match:
        result['max_blocks'] = int(blocks_match.group(1))
    
    # 检查是否有求解结果为0的情况
    if result['solutions'] and (result['solutions'] == '0' or float(result['solutions']) == 0):
        result['status'] = 'warning'
    
    return result

def run_algorithm(algo_name, algo_params, input_file, read_mode, num_threads=24):
    """运行单个算法"""
    if len(algo_params) == 1:
        cmd = [MAIN_EXECUTABLE] + [algo_params[0], input_file, str(read_mode)]
    else:
        cmd = [MAIN_EXECUTABLE] + [algo_params[0], input_file, str(read_mode), algo_params[1], str(num_threads)]
    
    print(f"  运行命令: {' '.join(cmd)}")
    
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            encoding='utf-8',
            errors='replace'
        )
        
        output = result.stdout + result.stderr
        print(f"  输出: {output[:200]}...")  # 打印前200字符
        
        return parse_log_output(output)
        
    except Exception as e:
        print(f"  错误: {e}")
        return {'time': 'error', 'solutions': None, 'max_blocks': None, 'status': 'error'}

def get_input_files(folder):
    """获取输入文件夹中的所有文件"""
    if not os.path.exists(folder):
        print(f"错误：输入文件夹不存在: {folder}")
        return []
    
    files = []
    for file in Path(folder).rglob('*'):
        if file.is_file():
            files.append(str(file))
    
    return sorted(files)

def filter_input_files(input_files, list_file):

    # 读取指定的文件名（无扩展名）
    with open(list_file, 'r', encoding='utf-8') as f:
        specified = set(line.strip() for line in f if line.strip())

    matched = []
    for file in input_files:
        base = os.path.basename(file)
        name_no_ext, _ = os.path.splitext(base)   # → 去掉扩展名
        if name_no_ext in specified:
            matched.append(file)

    return sorted(matched)

def read_existing_csv(csv_path):
    """读取已有的CSV文件，返回字典格式"""
    if not os.path.exists(csv_path):
        return {}
    
    data = {}
    with open(csv_path, 'r', encoding='utf-8') as f:
        reader = csv.reader(f)
        rows = list(reader)
        if len(rows) > 0:
            for row in rows[1:]:  # 跳过表头
                if len(row) > 0:
                    instanceName = row[0]
                    data[instanceName] = row
    
    return data

def write_csv_results(csv_path, results_data, col_id, include_solutions=False):
    """将结果写入CSV文件"""
    # 读取现有数据
    existing_data = read_existing_csv(csv_path)
    
    # 确定列数
    max_cols = 15 if include_solutions else 11
    
    # 合并新结果
    for filename, result in results_data.items():
        if filename not in existing_data:
            existing_data[filename] = [''] * max_cols
            existing_data[filename][0] = filename
        
        # 确保行有足够的列
        while len(existing_data[filename]) < max_cols:
            existing_data[filename].append('')
        
        # 写入时间结果
        if result['timeout']:
            existing_data[filename][col_id] = 'timeout'
        elif result['time'] is not None:
            existing_data[filename][col_id] = f"{result['time']:.4f}"
        
            # 如果需要，写入 Solutions 和 Max Blocks
            if include_solutions:
                if result['solutions'] is not None:
                    existing_data[filename][13] = result['solutions']
                if result['max_blocks'] is not None:
                    existing_data[filename][14] = str(result['max_blocks'])
        
            # 添加警告标记
            if result['status'] == 'warning':
                existing_data[filename][col_id] = f"{existing_data[filename][col_id]} (WARNING: 0 solutions)"
    
    # 写入文件
    with open(csv_path, 'w', encoding='utf-8', newline='') as f:
        writer = csv.writer(f)
        
        # 写表头
        if include_solutions:
            header = ['Instance', '#cols', '#rows', 'DLX', 'DXZ', 'D3X', 'DXD_S', 'DXD_M', 
                     'IDXD_S', 'IDXD_M', 'MDLX', 'sharpSAT-TD', 'ExactMC', 'Solutions', 'Max Blocks']
        else:
            header = ['Instance', '#cols', '#rows', 'DLX', 'DXZ', 'D3X', 'DXD_S', 'DXD_M', 
                     'IDXD_S', 'IDXD_M', 'MDLX']
        writer.writerow(header)
        
        # 写数据行
        for filename in sorted(existing_data.keys()):
            writer.writerow(existing_data[filename])

def write_thread_csv_results(csv_path, results_data, thread_nums):
    """将多线程结果写入CSV文件"""
    # 读取现有数据
    existing_data = read_existing_csv(csv_path)
    
    # 确定列数：Instance + 每个线程数1列(Time)
    num_cols = 1 + len(thread_nums) * 3  # 每个线程数占3列(Time, Solutions, Max Blocks)
    
    # 合并新结果
    for filename, thread_results in results_data.items():
        if filename not in existing_data:
            existing_data[filename] = [''] * num_cols
            existing_data[filename][0] = filename
        
        # 确保行有足够的列
        while len(existing_data[filename]) < num_cols:
            existing_data[filename].append('')
        
        # 写入每个线程数的结果
        for idx, num_threads in enumerate(thread_nums):
            if num_threads in thread_results:
                result = thread_results[num_threads]
                col = 1 + idx * 3  # 每个线程数占1列
                
                # Time列
                if result['timeout']:
                    existing_data[filename][col] = 'timeout'
                elif result['time'] is not None:
                    time_str = f"{result['time']:.4f}"
                    # if result['status'] == 'warning':
                    #     time_str += " (WARNING: 0 solutions)"
                    existing_data[filename][col] = time_str

                # Solutions列
                if result['solutions'] is not None:
                    existing_data[filename][col + 1] = result['solutions']
                
                # Max Blocks列
                if result['max_blocks'] is not None:
                    existing_data[filename][col + 2] = str(result['max_blocks'])
    
    # 写入文件
    with open(csv_path, 'w', encoding='utf-8', newline='') as f:
        writer = csv.writer(f)
        
        # 写表头
        header = ['Instance']
        for num_threads in thread_nums:
            header.extend([
                f'{num_threads}_threads_time',
                f'{num_threads}_threads_solutions',
                f'{num_threads}_threads_max_blocks'
            ])
        writer.writerow(header)
        
        # 写数据行
        for filename in sorted(existing_data.keys()):
            writer.writerow(existing_data[filename])

def main():
    """主函数"""
    # 解析命令行参数
    parser = argparse.ArgumentParser(
        description='批量运行算法测试脚本',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
示例:
  %(prog)s -r 1                    # 超时3600秒，读取模式1
  %(prog)s -r 2 -i ../inputs       # 自定义输入文件夹
        '''
    )
    
    parser.add_argument('-r', '--read-mode', type=int, default=3,
                        help='读取模式，默认为3')
    parser.add_argument('-i', '--input-folder', type=str, default=INPUT_FOLDER,
                        help=f'输入文件夹路径，默认为{INPUT_FOLDER}')
    parser.add_argument('-o', '--output-folder', type=str, default=RESULTS_FOLDER,
                        help=f'结果输出文件夹路径，默认为{RESULTS_FOLDER}')
    parser.add_argument('-f', '--list_file', type=str, default='',
                        help='包含要处理的特定文件列表的文本文件路径（可选）')
    
    args = parser.parse_args()
    
    read_mode = args.read_mode
    input_folder = args.input_folder
    results_folder = args.output_folder
    list_file = args.list_file
    
    print("=" * 60)
    print("批量算法测试脚本")
    print("=" * 60)
    print(f"读取模式: {read_mode}")
    print(f"输入文件夹: {input_folder}")
    print(f"结果文件夹: {results_folder}")
    
    # 创建结果文件夹
    os.makedirs(results_folder, exist_ok=True)
    
    # 获取输入文件
    input_files = get_input_files(input_folder)
    if not input_files:
        print("错误：没有找到输入文件")
        return
    
    print(f"\n找到 {len(input_files)} 个输入文件\n")
    
    # 检查可执行文件
    if not os.path.exists(MAIN_EXECUTABLE):
        print(f"错误：可执行文件不存在: {MAIN_EXECUTABLE}")
        return

    # 从列表文件中过滤输入文件（如果提供）
    if list_file:
        input_files = filter_input_files(input_files, list_file)
        print(f"\n过滤后找到 {len(input_files)} 个输入文件\n")

    # 对每个算法运行测试
    # for algo_name, algo_params, col_id, include_solutions, output_file in ALGORITHMS:
    #     print(f"\n{'=' * 60}")
    #     print(f"运行算法: {algo_name}")
    #     print(f"{'=' * 60}")
        
    #     results_data = {}
    #     csv_path = os.path.join(results_folder, output_file)
    #     col_id = col_id - 1

    #     for i, input_file in enumerate(input_files, 1):
    #         filename = os.path.basename(input_file)
    #         print(f"\n[{i}/{len(input_files)}] 处理文件: {filename}")
            
    #         result = run_algorithm(algo_name, algo_params, input_file, read_mode)
    #         results_data[filename] = result
            
    #         # 实时写入结果
    #         write_csv_results(csv_path, results_data, col_id, include_solutions)

    # 对每个算法运行测试
    for algo_name, algo_params, use_ett, thread_nums, output_file in THREAD_ALGORITHMS:
        print(f"\n{'=' * 60}")
        print(f"运行算法: {algo_name} (线程数: {thread_nums})")
        print(f"{'=' * 60}")
        
        results_data = {}
        csv_path = os.path.join(results_folder, output_file)

        for i, input_file in enumerate(input_files, 1):
            filename = os.path.basename(input_file)
            print(f"\n[{i}/{len(input_files)}] 处理文件: {filename}")
            
            # 存储该文件在不同线程数下的结果
            results_data[filename] = {}
            
            for num_threads in thread_nums:
                print(f"  测试 {num_threads} 线程...")
                result = run_algorithm(
                    algo_name, algo_params, input_file, read_mode, num_threads
                )
                results_data[filename][num_threads] = result
            
            # 实时写入结果
            write_thread_csv_results(csv_path, results_data, thread_nums)  

        print(f"\n算法 {algo_name} 完成，结果已保存到: {csv_path}")
    
    print("\n" + "=" * 60)
    print("所有算法运行完成！")
    print("=" * 60)

if __name__ == "__main__":
    main()