import os
import pandas as pd


def read_first_line_dims(txt_path):
    """
    读取 txt 文件首行，解析两个整数（列数、行数），如 '581 2737'
    返回 (cols, rows) 整数元组；解析失败返回 (0, 0)
    """
    try:
        with open(txt_path, 'r', encoding='utf-8') as f:
            first_line = f.readline().strip()
        parts = first_line.split()
        if len(parts) >= 2:
            return int(parts[0]), int(parts[1])
        print(f"  ⚠ 首行格式异常: {txt_path} -> '{first_line}'")
        return 0, 0
    except Exception as e:
        print(f"  ⚠ 读取失败: {txt_path} -> {e}")
        return 0, 0


def read_excel_names(excel_path):
    """
    从 Excel 第一列读取文件名（跳过表头），返回有序列表
    Excel 已按边数、顶点数从大到小排好序
    """
    df = pd.read_excel(excel_path, header=0)
    names = df.iloc[:, 0].dropna().astype(str).str.strip().tolist()
    return names


def rename_txt_files(dataset_folder, excel_path, label):
    """
    1. 读取 dataset_folder 下所有 .txt 文件，解析首行维度
    2. 按 (cols, rows) 从大到小排序
    3. 读取 Excel 中的有序文件名列表
    4. 按顺序一一重命名
    """
    print(f"\n{'='*50}")
    print(f"[{label}]  文件夹: {dataset_folder}")
    print(f"         Excel : {excel_path}")
    print(f"{'='*50}")

    # ① 收集所有 txt 文件及其首行维度
    txt_files = sorted([
        f for f in os.listdir(dataset_folder)
        if f.lower().endswith('.txt')
    ])

    records = []
    for fname in txt_files:
        full_path = os.path.join(dataset_folder, fname)
        cols, rows = read_first_line_dims(full_path)
        records.append((fname, cols, rows))

    print(f"扫描到 txt 文件: {len(records)} 个")

    # ② 按 (cols, rows) 从大到小排序
    records_sorted = sorted(records, key=lambda x: (x[1], x[2]), reverse=True)

    # ③ 读取 Excel 文件名列表（已排好序）
    excel_names = read_excel_names(excel_path)
    print(f"Excel 文件名数量: {len(excel_names)} 个")

    # ④ 数量校验
    if len(records_sorted) != len(excel_names):
        print(f"  ⚠ 警告: txt 文件数 ({len(records_sorted)}) 与 Excel 行数 ({len(excel_names)}) 不一致！")
        count = min(len(records_sorted), len(excel_names))
        print(f"  将仅处理前 {count} 对")
    else:
        count = len(excel_names)

    # ⑤ 两阶段重命名：先改为临时名，避免名称冲突
    print("\n--- 第一阶段：重命名为临时文件名 ---")
    temp_map = {}   # old_name -> temp_name
    for i in range(count):
        old_name = records_sorted[i][0]
        temp_name = f"__tmp_rename_{i:05d}__.txt"
        old_path  = os.path.join(dataset_folder, old_name)
        temp_path = os.path.join(dataset_folder, temp_name)
        os.rename(old_path, temp_path)
        temp_map[temp_name] = excel_names[i]

    print(f"  ✔ {count} 个文件已临时重命名")

    # ⑥ 第二阶段：从临时名改为目标名
    print("--- 第二阶段：重命名为目标文件名 ---")
    for i in range(count):
        temp_name = f"__tmp_rename_{i:05d}__.txt"
        target_name = excel_names[i] + ".txt"
        temp_path   = os.path.join(dataset_folder, temp_name)
        target_path = os.path.join(dataset_folder, target_name)
        os.rename(temp_path, target_path)
        print(f"  [{i+1:>3}] {temp_map[temp_name]:<35} <- "
              f"(cols={records_sorted[i][1]}, rows={records_sorted[i][2]})")

    print(f"\n✅ [{label}] 重命名完成，共处理 {count} 个文件")


def main():
    # ──────────────────────────────────────────────
    # 请修改为实际路径
    big_excel_path    = r"big_excel.xlsx"          # big excel 文件
    big_dataset_folder = r"big_dataset(170)"       # 170 个 txt 所在文件夹

    small_excel_path    = r"small_excel.xlsx"      # small excel 文件
    small_dataset_folder = r"small_dataset(129)"   # 129 个 txt 所在文件夹
    # ──────────────────────────────────────────────

    rename_txt_files(big_dataset_folder,   big_excel_path,   label="big_dataset(170)")
    rename_txt_files(small_dataset_folder, small_excel_path, label="small_dataset(129)")

    print("\n🎉 全部重命名完成")


if __name__ == "__main__":
    main()