import os

# 定义多米诺骨牌 (2个单元格)
DOMINOES = {
    'D': [(0, 0), (1, 0)]
}

# 定义四格骨牌 (4个单元格, 经典的 I, O, T, L, S)
TETROMINOES = {
    'I': [(0, 0), (1, 0), (2, 0), (3, 0)],
    'O': [(0, 0), (1, 0), (0, 1), (1, 1)],
    'T': [(0, 0), (1, 0), (2, 0), (1, 1)],
    'L': [(0, 0), (1, 0), (2, 0), (0, 1)],
    'S': [(0, 0), (1, 0), (1, 1), (2, 1)]
}

def normalize(cells):
    """将形状平移到左上角 (0,0) 为边界的规范化坐标"""
    min_x = min(x for x, y in cells)
    min_y = min(y for x, y in cells)
    return tuple(sorted((x - min_x, y - min_y) for x, y in cells))

def get_unique_orientations(cells):
    """获取一个骨牌的所有唯一旋转和翻转形态"""
    orientations = set()
    current = cells
    for _ in range(2): # 翻转 (镜像)
        for _ in range(4): # 旋转 90 度
            orientations.add(normalize(current))
            current = [(y, -x) for x, y in current]
        current = [(y, x) for x, y in current] # 对角线转置实现翻转
    return list(orientations)

def cell_index(x, y, width):
    """将 (x, y) 二维坐标映射为 1 开始的 1D 列索引"""
    return y * width + x + 1

def generate_exact_cover_matrix(width, height, piece_set):
    """生成精确覆盖矩阵"""
    columns = width * height
    matrix_rows = []
    
    for piece_name, cells in piece_set.items():
        orientations = get_unique_orientations(cells)
        
        for orient in orientations:
            max_x = max(x for x, y in orient)
            max_y = max(y for x, y in orient)
            
            # 在棋盘上平移滑动该形状
            for dy in range(height - max_y):
                for dx in range(width - max_x):
                    row = []
                    for x, y in orient:
                        row.append(cell_index(dx + x, dy + y, width))
                    # 排序以保证列索引从小到大
                    matrix_rows.append(sorted(row))
                    
    return columns, matrix_rows

def export_matrix_to_file(filename, cols, rows):
    """按照指定格式导出到文件"""
    with open(filename, 'w', encoding='utf-8') as f:
        # 第一行: <列数> <行数>
        f.write(f"{cols} {len(rows)}\n")
        # 随后的行: <count> col1 col2 ... coln (从1开始)
        for row in rows:
            f.write(f"{len(row)} " + " ".join(map(str, row)) + "\n")
    print(f"✅ 成功生成: {filename} (列: {cols}, 行: {len(rows)})")

def main():
    # 任务配置：名称，宽度，高度，使用的骨牌集合
    tasks = [
        ("dominoes_set/dominoes_8x10.txt", 8, 10, DOMINOES),
        ("dominoes_set/dominoes_10x12.txt", 10, 12, DOMINOES),
        ("dominoes_set/dominoes_12x12.txt", 12, 12, DOMINOES),
        ("dominoes_set/tetrominoes_4x5.txt", 4, 5, TETROMINOES), 
        ("dominoes_set/tetrominoes_6x8.txt", 6, 8, TETROMINOES),
        ("dominoes_set/tetrominoes_4x7.txt", 4, 7, TETROMINOES),
    ]
    
    for filename, w, h, pieces in tasks:
        cols, rows = generate_exact_cover_matrix(w, h, pieces)
        export_matrix_to_file(filename, cols, rows)

if __name__ == "__main__":
    main()