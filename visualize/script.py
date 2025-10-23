def zdd_to_dot(zdd_filename, dot_filename):
    """
    将ZDD文件转换为DOT格式
    
    ZDD文件格式：节点id level 左子节点id 右子节点id
    - B 表示指向 0 终端节点（False）
    - T 表示指向 1 终端节点（True）
    """
    
    # 读取ZDD文件
    nodes = {}
    with open(zdd_filename, 'r') as f:
        for line in f:
            parts = line.strip().split()
            if len(parts) < 3:
                continue
            
            node_id = int(parts[0])
            level = int(parts[1])
            
            # 解析左子节点
            if parts[2] == 'B':
                lo = 'F'  # 0终端节点
            elif parts[2] == 'T':
                lo = 'T'  # 1终端节点
            else:
                lo = int(parts[2])
            
            # 解析右子节点
            if len(parts) > 3:
                if parts[3] == 'B':
                    hi = 'F'
                elif parts[3] == 'T':
                    hi = 'T'
                else:
                    hi = int(parts[3])
            else:
                # 如果只有3个参数，说明没有右子节点（这种情况较少见）
                hi = lo
                lo = 'F'
            
            nodes[node_id] = {
                'level': level,
                'lo': lo,
                'hi': hi
            }
    
    # 找到根节点（level = 1）
    root_nodes = [nid for nid, data in nodes.items() if data['level'] == 1]
    
    # 生成DOT文件
    with open(dot_filename, 'w') as out:
        out.write("digraph ZDD {\n")
        out.write("  rankdir=TB;\n")
        out.write("  node [shape=circle];\n")
        out.write("  edge [arrowhead=none];\n\n")
        
        # 终端节点
        out.write("  F [shape=box, label=\"0\"];\n")
        out.write("  T [shape=box, label=\"1\"];\n\n")
        
        # 输出所有节点
        for node_id, data in sorted(nodes.items()):
            out.write(f"  n{node_id} [label=\"{data['level']}\"];\n")
        
        out.write("\n")
        
        # 输出所有边
        for node_id, data in sorted(nodes.items()):
            lo = data['lo']
            hi = data['hi']
            
            # 左边（low edge）- 虚线红色
            if lo == 'F' or lo == 'T':
                out.write(f"  n{node_id} -> {lo} [style=dashed, color=red];\n")
            else:
                out.write(f"  n{node_id} -> n{lo} [style=dashed, color=red];\n")
            
            # 右边（high edge）- 实线蓝色
            if hi == 'F' or hi == 'T':
                out.write(f"  n{node_id} -> {hi} [style=solid, color=blue];\n")
            else:
                out.write(f"  n{node_id} -> n{hi} [style=solid, color=blue];\n")
        
        out.write("}\n")
    
    print(f"DOT文件已生成: {dot_filename}")
    print(f"总节点数: {len(nodes)}")
    print(f"根节点: {root_nodes}")
    print(f"\n使用以下命令生成图片:")
    print(f"  dot -Tpng {dot_filename} -o output.png")
    print(f"  dot -Tsvg {dot_filename} -o output.svg")


def graph_to_dot(in_filename, dot_filename):
    """
    将图的.in文件转换为DOT格式
    
    .in文件格式：
    第一行：节点数 边数
    其余行：源顶点 目标顶点
    """
    
    with open(in_filename, 'r') as f:
        lines = f.readlines()
    
    # 读取第一行：节点数和边数
    num_nodes, num_edges = map(int, lines[0].strip().split())
    
    # 读取所有边
    edges = []
    for i in range(1, len(lines)):
        line = lines[i].strip()
        if line:
            src, dst = map(int, line.split())
            if src <= 16 and dst <= 16:
                edges.append((src, dst))
    
    # 生成DOT文件
    with open(dot_filename, 'w') as out:
        out.write("graph G {\n")
        out.write("  rankdir=TB;\n")
        out.write("  node [shape=circle, style=filled, fillcolor=lightblue];\n")
        out.write("  edge [color=gray60];\n\n")
        
        # 输出所有节点
        out.write("  // Nodes\n")
        for i in range(num_nodes):
            out.write(f"  {i} [label=\"{i}\"];\n")
        
        out.write("\n  // Edges\n")
        
        # 输出所有边
        for src, dst in edges:
            out.write(f"  {src} -- {dst};\n")
        
        out.write("}\n")
    
    print(f"DOT文件已生成: {dot_filename}")
    print(f"节点数: {num_nodes}")
    print(f"边数: {num_edges} (实际读取: {len(edges)})")
    print(f"\n使用以下命令生成图片:")
    print(f"  dot -Tpng {dot_filename} -o output.png")
    print(f"  neato -Tpng {dot_filename} -o output.png  # 使用neato布局")
    print(f"  fdp -Tpng {dot_filename} -o output.png    # 使用fdp布局")
    print(f"  sfdp -Tpng {dot_filename} -o output.png   # 使用sfdp布局（适合大图）")



# 使用示例
if __name__ == "__main__":
    # zdd_to_dot("Missouri.zdd", "Missouri.dot")
    graph_to_dot("Missouri.in", "Missouri_gml.dot")