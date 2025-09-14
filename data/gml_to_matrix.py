"""
将 GML 文件转换为矩阵。
"""
#!/usr/bin/env python3
#!/usr/bin/env python3

import argparse
import os
import re
from collections import defaultdict

def parse_gml_file(path: str):
    nodes = set()
    edges = set()
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()

    # 提取 node 块中的 id
    for m in re.finditer(r'node\s*\[(.*?)\]', content, flags=re.S):
        block = m.group(1)
        id_match = re.search(r'id\s+(-?\d+)', block)
        if id_match:
            nid = int(id_match.group(1))
            nodes.add(nid)

    # 提取 edge 块中的 source/target
    for m in re.finditer(r'edge\s*\[(.*?)\]', content, flags=re.S):
        block = m.group(1)
        s_match = re.search(r'source\s+(-?\d+)', block)
        t_match = re.search(r'target\s+(-?\d+)', block)
        if s_match and t_match:
            s = int(s_match.group(1))
            t = int(t_match.group(1))
            if s == t:
                continue  # 忽略自环
            edge = (min(s, t), max(s, t))  # 无向边去重
            edges.add(edge)
            nodes.add(s)
            nodes.add(t)

    return sorted(nodes), edges

def gml_to_matrix_edges(input_gml: str, output_txt: str):
    nodes, edges = parse_gml_file(input_gml)
    node_index = {node: i+1 for i, node in enumerate(nodes)}  # 映射到 1..N

    adj = defaultdict(set)
    for u, v in edges:
        adj[u].add(v)
        adj[v].add(u)

    with open(output_txt, 'w') as f:
        f.write(f"{len(nodes)} {len(nodes)}\n")
        for u in nodes:
            neighbors = sorted(node_index[v] for v in adj[u])
            row = [len(neighbors)] + neighbors
            f.write(" ".join(map(str, row)) + "\n")

    print(f"Converted {input_gml} -> {output_txt}")

def batch_convert(input_folder: str, output_folder: str):
    if not os.path.exists(output_folder):
        os.makedirs(output_folder, exist_ok=True)
    for fname in os.listdir(input_folder):
        if fname.endswith(".gml"):
            inpath = os.path.join(input_folder, fname)
            outpath = os.path.join(output_folder, os.path.splitext(fname)[0] + ".txt")
            gml_to_matrix_edges(inpath, outpath)


if __name__ == '__main__':
    input_dir = '/Users/luoyaohui/Documents/研究课题Exact Cover/DataSet/dataset_d3x/topology-zoo/dataset/'
    outbut_dir = './dataset_d3x/dataset_matrix/'
    input_file = 'UsSignal.gml'
    output_file = 'UsSignal.txt'
    
    gml_to_matrix_edges(os.path.join(input_dir, input_file), os.path.join(outbut_dir, output_file))