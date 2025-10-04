from collections import OrderedDict

def matrix_to_zdd(input_file_path, output_file_path):
    """
    将稀疏矩阵格式转换为ZDD格式
    
    输入格式:
    第一行: max_level(列数) row_count(行数)
    后续行: count level1 level2 ... (每行的非零元素列索引)
    
    输出格式:
    每行: node_id level lo_id hi_id
    """
    
    # Step 1: 读取稀疏矩阵
    with open(input_file_path, "r") as f:
        lines = f.readlines()
    
    first_line = lines[0].strip().split()
    max_level = int(first_line[0])
    row_count = int(first_line[1])
    
    # 解析每一行的集合
    row_sets = []
    for i in range(1, len(lines)):
        line = lines[i].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) > 0:
            count = int(parts[0])
            if count > 0:
                col_set = set(int(parts[j]) for j in range(1, count + 1))
                row_sets.append(col_set)
    
    # Step 2: 初始化ZDD构建器
    builder = ZDDBuilder(max_level + row_count)
    
    # Step 3: 为每一行构建ZDD并执行Union操作
    result_zdd = None
    for row_set in row_sets:
        row_zdd = builder.build_zdd_from_set(row_set)
        if result_zdd is None:
            result_zdd = row_zdd
        else:
            result_zdd = builder.union(result_zdd, row_zdd)
    
    # Step 4: 输出ZDD到文件
    builder.output_zdd(result_zdd, output_file_path)


class ZDDBuilder:
    def __init__(self, start_num):
        # 终端节点
        self.T = "T"
        self.F = "B"
        
        # 唯一表: (level, lo_id, hi_id) -> node_id
        self.unique_table = {}

        # level -> [node_id]
        self.level_nodes = {}

        # node_id -> [path : set(int)]
        self.node_paths = {}
        
        # 节点信息: node_id -> {"level": int, "lo": str, "hi": str}
        self.nodes = {}
        
        # Union操作缓存
        self.union_cache = {}
        
        # 节点ID计数器
        self.node_counter = start_num

        # 遍历节点表
        self.traverse_node = {}
    
    def get_node_id(self, level, lo_id, hi_id):
        """生成唯一的节点ID"""
        key = (level, lo_id, hi_id)
        
        # ZDD约简规则: 如果hi指向F，则消除该节点
        if hi_id == self.F:
            return lo_id
        
        # 检查唯一表
        if key in self.unique_table:
            return self.unique_table[key]
        
        # 确保level_nodes字典中有该level的列表
        if level in self.level_nodes:
            for node_id in self.level_nodes[level]:
                node = self.nodes[node_id]
                if node["lo"] == self.F and node["hi"] == hi_id:
                    node["lo"] = lo_id
                    old_key = (level, self.F, hi_id)
                    del self.unique_table[old_key]
                    self.unique_table[key] = node_id
                    
                    # for path in self.node_paths.get(lo_id, []):
                    #     self.node_paths[node_id].append(path)
                    
                    return node_id

        
        # 创建新节点
        node_id = str(self.node_counter)
        self.node_counter += 1
        
        self.unique_table[key] = node_id
        self.level_nodes.setdefault(level, []).append(node_id)
        self.nodes[node_id] = {
            "level": level,
            "lo": lo_id,
            "hi": hi_id
        }

        # 新建节点构建路径列表
        # if (hi_id == self.T and lo_id == self.F):
        #     self.node_paths[node_id].append({level})
        # elif (hi_id == self.T and lo_id == self.T):
        #     self.node_paths[node_id] = [{level}, {}]
        # elif (hi_id == self.T and lo_id in self.nodes):
        #     self.node_paths[node_id] = (
        #         [{level}] +
        #         self.node_paths.get(lo_id, [])
        #     )
        # elif (hi_id in self.nodes and lo_id == self.F):
        #     self.node_paths[node_id] = [
        #         path.union({level}) for path in self.node_paths.get(hi_id, [])
        #     ]
        # elif (hi_id in self.nodes and lo_id in self.nodes):
        #     self.node_paths[node_id] = (
        #         [path.union({level}) for path in self.node_paths.get(hi_id, [])] +
        #         self.node_paths.get(lo_id, [])
        #     )
        # else:
        #     self.node_paths[node_id] = [
        #         path.union({level}) for path in self.node_paths.get(hi_id, [])
        #     ]
        #     self.node_paths[node_id].append({})
        
        return node_id
    
    def build_zdd_from_set(self, col_set):
        """从一个集合构建ZDD路径"""
        if not col_set:
            return self.T
        
        # 从最大的level开始自下向上构建
        sorted_cols = sorted(col_set, reverse=True)
        node = self.T
        
        for col in sorted_cols:
            node = self.get_node_id(col, self.F, node)
        
        return node
    
    def union(self, p_id, q_id):
        """ZDD的Union操作（集合并集）"""
        # 终止条件
        if p_id == self.F:
            return q_id
        if q_id == self.F:
            return p_id
        if p_id == q_id:
            return p_id
        
        # 检查缓存
        cache_key = (p_id, q_id) if p_id < q_id else (q_id, p_id)
        if cache_key in self.union_cache:
            return self.union_cache[cache_key]
        
        # 处理终端节点
        if p_id == self.T and q_id == self.T:
            return self.T
        
        # 获取节点信息
        p_node = self.nodes.get(p_id)
        q_node = self.nodes.get(q_id)
        
        result = None
        
        if p_node is None and q_node is None:
            # 两个都是终端节点
            result = self.T if (p_id == self.T or q_id == self.T) else self.F
        elif p_node is None and q_node is not None:
            # p是终端节点
            if p_id == self.T:
                result = self.get_node_id(
                    q_node["level"],
                    self.union(p_id, q_node["lo"]),
                    q_node["hi"]
                )
            else:
                result = q_id
        elif q_node is None and p_node is not None:
            # q是终端节点
            if q_id == self.T:
                result = self.get_node_id(
                    p_node["level"],
                    self.union(p_node["lo"], q_id),
                    p_node["hi"]
                )
            else:
                result = p_id
        elif q_node is not None and p_node is not None:
            # 两个都是内部节点
            p_level = p_node["level"]
            q_level = q_node["level"]
            
            if p_level < q_level:
                result = self.get_node_id(
                    p_level,
                    self.union(p_node["lo"], q_id),
                    p_node["hi"]
                )
            elif p_level > q_level:
                result = self.get_node_id(
                    q_level,
                    self.union(p_id, q_node["lo"]),
                    q_node["hi"]
                )
            else:  # p_level == q_level
                result = self.get_node_id(
                    p_level,
                    self.union(p_node["lo"], q_node["lo"]),
                    self.union(p_node["hi"], q_node["hi"])
                )
                # self.level_nodes[p_level].remove(p_id)
        
        self.union_cache[cache_key] = result
        return result
    
    def traverseZdd(self, node_id, visited=None):
        """遍历ZDD, 收集所有节点"""
        if visited is None:
            visited = set()
        
        if node_id is None or node_id == self.T or node_id == self.F:
            return
        
        if node_id in visited:
            return
        
        visited.add(node_id)
        
        node = self.nodes.get(node_id)
        if node is None:
            return
        
        level = node["level"]
        lo_id = node["lo"]
        hi_id = node["hi"]
        
        if level not in self.traverse_node:
            self.traverse_node[level] = []
        self.traverse_node[level].append(node_id)
        
        self.traverseZdd(lo_id, visited)
        self.traverseZdd(hi_id, visited)

    def output_zdd(self, root_id, output_file_path):
        """将ZDD输出到文件"""
        if root_id is None or root_id == self.T or root_id == self.F:
            # 空ZDD或只有终端节点
            with open(output_file_path, "w") as f:
                pass
            return
        
        # 初始化遍历字典
        self.traverse_node = {}

        # 遍历ZDD收集节点
        self.traverseZdd(root_id)
        
        # 按level从大到小排序
        sorted_levels = sorted(self.traverse_node.keys(), reverse=True)

        with open(output_file_path, "w") as f:
            for level in sorted_levels:
                for node_id in self.traverse_node[level]:
                    node_info = self.nodes[node_id]
                    f.write(f"{node_id} {level} {node_info['lo']} {node_info['hi']}\n")


# 使用示例
if __name__ == "__main__":
    # 转换单个文件
    matrix_to_zdd("graph_matrix/Missouri.txt", "output.zdd")
    print("转换完成")
    
    # 批量转换示例
    # import os
    # input_folder = "./matrix_files"
    # output_folder = "./zdd_files"
    # 
    # for filename in os.listdir(input_folder):
    #     if filename.endswith(".txt"):
    #         input_path = os.path.join(input_folder, filename)
    #         output_path = os.path.join(output_folder, filename.replace(".txt", ".zdd"))
    #         matrix_to_zdd(input_path, output_path)
    #         print(f"Converted: {filename}")