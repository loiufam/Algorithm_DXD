
from html import parser
import os
import subprocess
import resource
import csv
import re
import sys
import logging
import argparse
from pathlib import Path
from datetime import datetime

# =========================
# 配置
# =========================
MAIN_EXECUTABLE = "./main"
INPUT_FOLDERS = [
"../data/batch_2/dominoes_set",
"../data/batch_2/exact_cover_benchmarks",
"../data/batch_2/set_partition_benchmarks",
"../data/batch_2/graphs_set"
]
RESULTS_FOLDER = "../results/batch_results_" + datetime.now().strftime("%Y%m%d")
THREADS_FOLDER = "../results/Threads"

WRITE_INTERVAL = 2  # 每2个写一次CSV

# =========================
# 算法分组配置
# =========================
# 分组 1: ddxd-t8, ddxd-t1
GROUP1_ALGORITHMS = [
    # ("DynDXD_T8", ["ddxd", "8"], True, "DynDXD_t8_results.csv"),
    ("DynDXD_T1", ["ddxd", "1"], True, "DynDXD_t1_results.csv"),
]

# 分组 2: dxd-t8, dxd-t1
GROUP2_ALGORITHMS = [
    # ("DXD_T8", ["dxd", "8"], True, "DXD_t8_results.csv"),
    ("DXD_T1", ["dxd", "1"], True, "DXD_t1_results.csv"),
]

# 分组 3: dxz, dlx
GROUP3_ALGORITHMS = [
    # ("DXZ", ["dxz"], False, "DXZ_results.csv"),
    ("DLX", ["dlx"], False, "DLX_results.csv"),
]

GROUP4_ALGORITHMS = [
    # ("MDLX", ["mdlx", "d", "1"], True, "MDLX_D_results_T1.csv"),
    ("MDLX", ["mdlx", "s", "1"], True, "MDLX_S_results_T1.csv"),
    # ("MDLX", ["mdlx", "d", "8"], True, "MDLX_D_results_T8.csv"),
    ("MDLX", ["mdlx", "s", "8"], True, "MDLX_S_results_T8.csv"),
]

# 默认全部算法 (合并以上所有组)
ALGORITHMS = GROUP1_ALGORITHMS + GROUP2_ALGORITHMS + GROUP3_ALGORITHMS + GROUP4_ALGORITHMS
# ALGORITHMS = GROUP4_ALGORITHMS


# 多线程算法配置：(算法名, 命令参数, 支持多线程, 线程数列表, 输出文件名)
THREAD_ALGORITHMS = [
    # ("DXD_M", ["dxd"], True, [1, 2, 4, 8], "DXD_threads.csv"),
    # ("DynDXD_M", ["ddxd"], True, [1, 2, 4, 8], "DynDXD_threads.csv"),
    ("MDLX", ["mdlx"], True, [1, 2, 4, 8], "MDLX_threads.csv"),
]

# =========================
# logging
# =========================
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s"
)

# =========================
# 正则预编译
# =========================
COLS_PATTERN = re.compile(r'cols:\s*(\d+)')
ROWS_PATTERN = re.compile(r'rows:\s*(\d+)')
TIME_PATTERN = re.compile(r'Time:\s*([\d.]+)\s*s')
SOL_PATTERN = re.compile(r'Solutions:\s*([\d.eE+\-]+)')
SOL_SCI_PATTERN = re.compile(r'Solutions (scientific):\s*([\d.eE+\-]+)')
BLOCK_PATTERN = re.compile(r'Max Blocks:\s*(\d+)')
ZDD_PATTERN = re.compile(r'ZDD Size:\s*(\d+)')
DNNF_PATTERN = re.compile(r'DNNF Size:\s*(\d+)')

# =========================
# 解析输出
# =========================
def parse_log_output(output, memo_out = False):
    """解析算法输出的日志信息"""
    result = {
        'rows': None,
        'cols': None,
        'time': None,
        'solutions': None,
        'max_blocks': None,
        'zdd_size': None,
        'dnnf_size': None,
        'status': 'success',
        'timeout': False
    }

    if memo_out:
        result['timeout'] = True
        result['status'] = 'memory_out'
        return result

    if m := COLS_PATTERN.search(output):
        result['cols'] = int(m.group(1))
    if m := ROWS_PATTERN.search(output):
        result['rows'] = int(m.group(1))

    # 检测是否包含“超时”
    if '超时' in output:
        result['timeout'] = True
        result['status'] = 'timeout'
        return result

    if m := TIME_PATTERN.search(output):
        result['time'] = float(m.group(1))

    if m := SOL_PATTERN.search(output):
        result['solutions'] = m.group(1)

    if m := SOL_SCI_PATTERN.search(output):
        result['solutions'] = m.group(1)

    if m := BLOCK_PATTERN.search(output):
        result['max_blocks'] = int(m.group(1))

    if m := ZDD_PATTERN.search(output):
        result['zdd_size'] = int(m.group(1))

    if m := DNNF_PATTERN.search(output):
        result['dnnf_size'] = int(m.group(1))

    # 检查是否有求解结果为0的情况
    try:
        if result['solutions'] and float(result['solutions']) == 0:
            result['status'] = 'warning'
    except:
        pass

    return result

def limit_memory():
    """设置子进程虚拟内存上限为 20GB"""
    # 20 GB 转换为 Bytes
    max_memory_bytes = 20 * 1024 * 1024 * 1024
    # 设置虚拟内存地址空间大小限制 (软限制, 硬限制)
    resource.setrlimit(resource.RLIMIT_AS, (max_memory_bytes, max_memory_bytes))

# =========================
# 运行算法
# =========================
def run_algorithm(alg_name, algo_params, input_file, supports_thread, num_threads = 1, timeout = 1500):
    """运行单个算法"""

    cmd = [MAIN_EXECUTABLE] + ["-a", algo_params[0], "-i", input_file]

    if (alg_name == "MDLX"):
        cmd += ["-m", algo_params[1]]
    
    if supports_thread:
        cmd += ["-t", str(num_threads)]

    logging.info(f"运行: {' '.join(cmd)}")

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=timeout,
            encoding='utf-8',
            errors='replace',
            preexec_fn=limit_memory
        )

        output = result.stdout + result.stderr

        memo_out = False
        if result.returncode != 0:
            if "Memory" in output or "bad_alloc" in output or result.returncode < 0:
                print(f"  [内存超出或崩溃] returncode={result.returncode}，可能超出了 20GB 限制。")
                memo_out = True
        print(f"  输出: {output[:200]}...")  # 打印前200字符

        return parse_log_output(output, memo_out)

    except subprocess.TimeoutExpired as e:
        print(f"  [超时] 超过 {timeout} 秒限制。")
        
        # 安全解码函数：处理可能返回的 bytes 或 None
        def safe_decode(out_data):
            if out_data is None:
                return ""
            if isinstance(out_data, bytes):
                return out_data.decode('utf-8', errors='replace')
            return str(out_data)

        stdout_part = safe_decode(e.stdout)
        stderr_part = safe_decode(e.stderr)
        output = stdout_part + stderr_part
        
        print(f"  超时前输出: {output[:200]}...")  # 打印超时前捕获的内容供调试
        
        result = parse_log_output(output)
        
        result['timeout'] = True
        result['status'] = 'timeout'
        
        return result
    except Exception as e:
        logging.error(e)
        return {'status': 'error'}


# =========================
# 文件
# =========================
def get_input_files(folder):
    return sorted([str(f) for f in Path(folder).rglob('*') if f.is_file()])

def filter_input_files(input_files, list_file):

    # 读取指定的文件名（无扩展名）
    with open(list_file, 'r', encoding='utf-8') as f:
        # specified = set(line.strip() for line in f if line.strip())
        specified = set(os.path.splitext(line.strip().split(',')[0])[0] for line in f if line.strip())

    matched = []
    for file in input_files:
        base = os.path.basename(file)
        name_no_ext, _ = os.path.splitext(base)   # → 去掉扩展名
        if name_no_ext in specified:
            matched.append(file)

    return sorted(matched)

def get_skip_set(skip_file):
    """读取黑名单txt，返回需要跳过的文件名集合"""
    if not skip_file or not os.path.exists(skip_file):
        return set()

    with open(skip_file, 'r', encoding='utf-8') as f:
        # 提取文件名，确保带有后缀，去除两端空格
        return set(line.strip() for line in f if line.strip())

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

def write_csv_results(csv_path, results_data, alg_name):
    """将结果写入CSV文件"""
    # 读取现有数据
    existing_data = read_existing_csv(csv_path)

    # 确定列数
    max_cols = 8

    # 合并新结果
    for filename, result in results_data.items():
        if filename not in existing_data:
            existing_data[filename] = [''] * max_cols
            existing_data[filename][0] = filename

        # 确保行有足够的列
        while len(existing_data[filename]) < max_cols:
            existing_data[filename].append('')

        if result.get('cols') is not None:
            existing_data[filename][1] = str(result.get('cols'))
        if result.get('rows') is not None:
            existing_data[filename][2] = str(result.get('rows'))

        # 写入时间结果
        if result.get('timeout'):
            existing_data[filename][3] = 'TO'
        elif result.get('time') is not None:
            existing_data[filename][3] = f"{result.get('time'):.4f}"

        # 写入 Solutions 和 Max Blocks
        if result.get('solutions') is not None:
            existing_data[filename][4] = result.get('solutions')
        if result.get('max_blocks') is not None:
            existing_data[filename][5] = str(result.get('max_blocks'))

        # 添加警告标记
        if result.get('status') == 'warning':
            existing_data[filename][3] = f"{existing_data[filename][3]} (WARNING: 0 solutions)"

        if result.get('zdd_size') is not None:
            existing_data[filename][6] = result.get('zdd_size')

        if result.get('dnnf_size') is not None:
            existing_data[filename][7] = result.get('dnnf_size')

    # 写入文件
    with open(csv_path, 'w', encoding='utf-8', newline='') as f:
        writer = csv.writer(f)

        # 写表头
        header = ['Instance', '#cols', '#rows', alg_name, 'Solutions', 'Max Blocks', '|ZDD|', '|DNNF|']
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
                if result.get('timeout'):
                    existing_data[filename][col] = 'timeout'
                elif result.get('time') is not None:
                    time_str = f"{result.get('time'):.4f}"
                    existing_data[filename][col] = time_str

                # Solutions列
                if result.get('solutions') is not None:
                    existing_data[filename][col + 1] = result.get('solutions')

                # Max Blocks列
                if result.get('max_blocks') is not None:
                    existing_data[filename][col + 2] = str(result.get('max_blocks'))

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
        description='批量运行算法测试脚本（支持并行）',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''

        '''
    )

    parser.add_argument('-f', '--list_file', type=str, default='',
                        help='包含要处理的特定文件列表的文本文件路径（可选）')
    parser.add_argument('-s', '--skip_file', type=str, default='',
                        help='包含要跳过的文件列表（带后缀，一行一个）的文本文件路径（黑名单模式）') # 新增
    parser.add_argument('-r', '--rounds', type=int, default=1,
                        help='多线程实验中每个实例运行的轮数，取平均时间写入CSV（默认1）') # 新增
    parser.add_argument('-G', '--group', type=int, choices=[1, 2, 3, 4],
                        help='指定运行的算法分组(1: ddxd-t8/t1, 2: dxd-t8/t1, 3: dxz/dlx), 4: MDLX variants)')
    parser.add_argument('-p', '--parallel', action='store_true',
                        help='开启多线程对比实验')
    parser.add_argument('-a', '--append', action='store_true',
                        help='追加模式：寻找最新结果目录并跳过已存在的实例')
    parser.add_argument('-t', '--timeout', type=int, default=1500,
                        help="单次求解的超时时间（秒），默认 1500")


    args = parser.parse_args()

    list_file = args.list_file

    print("=" * 60)
    print("批量算法实验脚本")
    print("=" * 60)

    global RESULTS_FOLDER
    if args.append:
        if not args.parallel:
            # 单线程追加：去寻找最新的 batch_results 目录
            base_dir = Path("../results")
            if base_dir.exists():
                dirs = [d for d in base_dir.glob("batch_results_*") if d.is_dir()]
                if dirs:
                    RESULTS_FOLDER = str(max(dirs, key=lambda d: d.stat().st_mtime))
                    print(f"[*] 追加模式开启，单线程结果将追加至最新目录: {RESULTS_FOLDER}")
        else:
            # 多线程追加：直接提示使用 Threads 目录
            print(f"[*] 追加模式开启，多线程结果将追加至: {THREADS_FOLDER}")

    else:
        # 创建结果文件夹
        os.makedirs(RESULTS_FOLDER, exist_ok=True)
    
    os.makedirs(THREADS_FOLDER, exist_ok=True)

    # 获取输入文件
    input_files = []
    for input_folder in INPUT_FOLDERS:
        input_files.extend(get_input_files(input_folder))
    if not input_files:
        print("错误：没有找到输入文件")
        return

    print(f"\n找到 {len(input_files)} 个输入文件\n")

    # 从列表文件中过滤输入文件（如果提供）
    if list_file:
        input_files = filter_input_files(input_files, list_file)
        print(f"\n过滤后找到 {len(input_files)} 个输入文件\n")

    # 加载需要跳过但需要记为超时的黑名单文件
    skip_set = get_skip_set(args.skip_file)
    if skip_set:
        print(f"\n加载了 {len(skip_set)} 个黑名单文件（将直接记为超时）\n")

    # 检查可执行文件
    if not os.path.exists(MAIN_EXECUTABLE):
        print(f"错误：可执行文件不存在: {MAIN_EXECUTABLE}")
        return

    start_time = datetime.now()

    if not args.parallel:

        # 决定运行哪个算法组
        if args.group == 1:
            print("\n=== 运行分组 1 (DynDXD_T8, DynDXD_T1) ===")
            algorithms_to_run = GROUP1_ALGORITHMS
        elif args.group == 2:
            print("\n=== 运行分组 2 (DXD_T8, DXD_T1) ===")
            algorithms_to_run = GROUP2_ALGORITHMS
        elif args.group == 3:
            print("\n=== 运行分组 3 (DXZ, DLX) ===")
            algorithms_to_run = GROUP3_ALGORITHMS
        elif args.group == 4:
            print("\n=== 运行分组 4 (MDLX variants) ===")
            algorithms_to_run = GROUP4_ALGORITHMS
        else:
            print("\n=== 运行全部算法 ===")
            algorithms_to_run = ALGORITHMS

        # 不同求解器对比实验
        for algo_name, algo_params, supports_thread, output_file in algorithms_to_run:
            print(f"\n{'=' * 60}")
            print(f"运行算法: {algo_name}")

            print(f"{'=' * 60}")

            results_data = {}
            csv_path = os.path.join(RESULTS_FOLDER, output_file)

            existing_data = read_existing_csv(csv_path) if args.append else {}

            for i, input_file in enumerate(input_files, 1):
                filename = os.path.basename(input_file)

                if args.append and filename in existing_data:
                    print(f"\n[{i}/{len(input_files)}] (追加模式) 跳过已有文件: {filename}")
                    continue

                if filename in skip_set:
                    print(f"\n[{i}/{len(input_files)}] (黑名单) 跳过运行，直接记为超时: {filename}")
                    # 伪造一个超时的结果写入
                    results_data[filename] = {'timeout': True, 'status': 'timeout'}
                else:
                    print(f"\n[{i}/{len(input_files)}] 处理文件: {filename}")
                    if algo_name != "MDLX":
                        threads = int(algo_params[1]) if len(algo_params) > 1 else 1
                    else:
                        threads = int(algo_params[2]) if len(algo_params) > 2 else 1
                    result = run_algorithm(algo_name, algo_params, input_file, supports_thread, threads, args.timeout)
                    results_data[filename] = result

                if i % WRITE_INTERVAL == 0 or i == len(input_files):
                    write_csv_results(csv_path, results_data, algo_name)
                    results_data = {}  # 清空已写入的数据，继续收集新的结果

    else:
        # =====================
        # 多线程实验
        # =====================
        for algo_name, algo_params, supports_thread, thread_nums, output_file in THREAD_ALGORITHMS:
            print(f"\n{'=' * 60}")
            print(f"运行算法: {algo_name} (线程数: {thread_nums})")
            print(f"{'=' * 60}")

            results_data = {}
            csv_path = os.path.join(THREADS_FOLDER, output_file)

            existing_data = read_existing_csv(csv_path) if args.append else {}

            for i, input_file in enumerate(input_files, 1):
                filename = os.path.basename(input_file)

                if args.append and filename in existing_data:
                    print(f"\n[{i}/{len(input_files)}] (追加模式) 跳过已有文件: {filename}")
                    continue

                if filename in skip_set:
                    print(f"\n[{i}/{len(input_files)}] (黑名单) 跳过运行，所有线程数均记为超时: {filename}")
                    results_data[filename] = {}
                    for num_threads in thread_nums:
                        results_data[filename][num_threads] = {'timeout': True, 'status': 'timeout'}
                else:
                    print(f"\n[{i}/{len(input_files)}] 处理文件: {filename}")
                    results_data[filename] = {}

                    # 标识高线程是否已经超时
                    higher_thread_timeout = False

                    for num_threads in sorted(thread_nums, reverse=True):

                        # 如果高线程已经超时，直接将剩余的低线程计为超时并跳过执行
                        if higher_thread_timeout:
                            print(f"  -> 高并发已超时，直接跳过 {num_threads} 线程计为超时")
                            results_data[filename][num_threads] = {'timeout': True, 'status': 'timeout'}
                            continue

                        if args.rounds > 1:
                            times = []
                            last_result = None
                            print(f"  测试 {num_threads} 线程 (共 {args.rounds} 轮): ", end="", flush=True)
                            
                            for r in range(args.rounds):
                                result = run_algorithm(
                                    algo_name, algo_params, input_file, supports_thread, num_threads, args.timeout
                                )
                                
                                # 如果遇到超时或内存溢出，直接终止后续轮次，节省时间
                                if result.get('timeout') or result.get('status') != 'success':
                                    last_result = result
                                    print("[失败/超时中断]", end=" ")
                                    if result.get('timeout'):
                                        higher_thread_timeout = True
                                    break
                                    
                                if result.get('time') is not None:
                                    times.append(result['time'])
                                last_result = result
                                print(f"{r+1}✓ ", end="", flush=True)
                            
                            print() # 换行
                            
                            # 汇总结果：如果完整跑完了，计算平均时间覆盖回去
                            if times and len(times) == args.rounds:
                                avg_time = sum(times) / len(times)
                                last_result['time'] = avg_time
                                results_data[filename][num_threads] = last_result
                            else:
                                # 跑失败了或者被中断，直接存最后一次的异常状态
                                results_data[filename][num_threads] = last_result

                        else:
                            # 跑单轮的逻辑
                            result = run_algorithm(
                                algo_name, algo_params, input_file, supports_thread, num_threads, args.timeout
                            )
                            results_data[filename][num_threads] = result

                            if result.get('timeout'):
                                higher_thread_timeout = True

                if i % WRITE_INTERVAL == 0 or i == len(input_files):
                    write_thread_csv_results(csv_path, results_data, thread_nums)
                    results_data = {}  # 清空已写入的数据，继续收集新的结果

    end_time = datetime.now()
    elapsed = (end_time - start_time).total_seconds()

    print("\n" + "=" * 60)
    print("所有算法运行完成！")
    print(f"总耗时: {elapsed:.2f} 秒")
    print("=" * 60)

if __name__ == "__main__":
    main()