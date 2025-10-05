#!/bin/bash


if [ $# -lt 2 ]; then
    echo "Usage: $0 <source_folder> <target_folder>"
    exit 1
fi

SRC=$1
DST=$2

# 如果目标文件夹不存在则创建
mkdir -p "$DST"

# 遍历源文件夹中的文件
for file in "$SRC"/*; do
    if [ -f "$file" ]; then
        filename=$(basename "$file")
        
        # 检查目标文件夹中是否已存在同名文件
        if [ -f "$DST/$filename" ]; then
            echo "Skip: $filename already exists"
        else
            cp "$file" "$DST/"
            echo "Copied: $filename"
        fi
    fi
done
