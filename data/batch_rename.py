import os

# 目标文件夹路径
folder_path = "graph_dataset"  # 修改为你的文件夹路径

for root, dirs, files in os.walk(folder_path):
    for file in files:
        if file.endswith(".in.txt"):
            old_path = os.path.join(root, file)
            new_file = file.replace(".in.txt", ".txt")
            new_path = os.path.join(root, new_file)

            os.rename(old_path, new_path)
            print(f"重命名: {old_path} -> {new_path}")
