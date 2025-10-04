import sys

def verify_zdd(zdd_file, matrix_file):
    """验证ZDD和矩阵的一致性"""
    
    # 读取原始矩阵
    with open(matrix_file, 'r') as f:
        lines = f.readlines()
        cols, rows = map(int, lines[0].split())
        
        matrix_sets = []
        for line in lines[1:]:
            if line.strip():
                parts = list(map(int, line.split()))
                count = parts[0]
                row_set = set(parts[1:count+1])
                matrix_sets.append(row_set)
    
    # 读取ZDD
    nodes = {}
    with open(zdd_file, 'r') as f:
        for line in f:
            parts = line.strip().split()
            if len(parts) == 4:
                node_id, level, lo, hi = parts
                nodes[node_id] = {
                    'level': int(level),
                    'lo': lo,
                    'hi': hi
                }
    
    # 提取ZDD表示的集合
    def extract_paths(node_id, current_set):
        if node_id == 'T':
            return [current_set]
        if node_id == 'B':
            return []
        
        if node_id not in nodes:
            return []
        
        node = nodes[node_id]
        paths = []
        
        # Low边
        paths.extend(extract_paths(node['lo'], current_set))
        
        # High边
        new_set = current_set | {node['level']}
        paths.extend(extract_paths(node['hi'], new_set))
        
        return paths
    
    # 找根节点（未被引用的节点）
    referenced = set()
    for node in nodes.values():
        if node['lo'] not in ['T', 'B']:
            referenced.add(node['lo'])
        if node['hi'] not in ['T', 'B']:
            referenced.add(node['hi'])
    
    roots = set(nodes.keys()) - referenced
    if len(roots) != 1:
        print(f"Error: Expected 1 root, found {len(roots)}")
        return False
    
    root = roots.pop()
    zdd_sets = [frozenset(s) for s in extract_paths(root, set())]
    matrix_sets_frozen = [frozenset(s) for s in matrix_sets]
    
    # 验证
    if set(zdd_sets) == set(matrix_sets_frozen):
        print("✓ Verification PASSED")
        print(f"  Matrix rows: {len(matrix_sets)}")
        print(f"  ZDD paths: {len(zdd_sets)}")
        print(f"  ZDD nodes: {len(nodes)}")
        return True
    else:
        print("✗ Verification FAILED")
        print(f"  Matrix rows: {len(matrix_sets)}")
        print(f"  ZDD paths: {len(zdd_sets)}")
        missing = set(matrix_sets_frozen) - set(zdd_sets)
        extra = set(zdd_sets) - set(matrix_sets_frozen)
        if missing:
            print(f"  Missing from ZDD: {missing}")
        if extra:
            print(f"  Extra in ZDD: {extra}")
        return False

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: python3 verify_zdd.py <zdd_file> <matrix_file>")
        sys.exit(1)
    
    success = verify_zdd(sys.argv[1], sys.argv[2])
    sys.exit(0 if success else 1)