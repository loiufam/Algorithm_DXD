#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import requests
from bs4 import BeautifulSoup
from urllib.parse import urljoin
from concurrent.futures import ThreadPoolExecutor, as_completed

# 目标 URL 和 本地保存目录
BASE_URL = "https://people.brunel.ac.uk/~mastjjb/jeb/orlib/files/"
SAVE_DIR = "./set_partition/"

def process_file(file_url, save_dir):
    """
    流式读取文件前10行，验证格式，符合要求则保存
    """
    filename = file_url.split('/')[-1]
    save_path = os.path.join(save_dir, filename)
    
    # 如果文件已存在，可以选择跳过
    if os.path.exists(save_path):
        return f"[-] 跳过 (已存在): {filename}"

    try:
        # 启用流式传输，避免将整个大文件读入内存
        with requests.get(file_url, stream=True, timeout=15) as r:
            r.raise_for_status()
            
            lines_cache = []
            iterator = r.iter_lines(decode_unicode=True)
            
            # 读取前10个非空行
            while len(lines_cache) < 10:
                try:
                    line = next(iterator)
                    if line is not None:
                        line_str = line.strip()
                        if line_str:  # 跳过完全空白的行
                            lines_cache.append(line_str)
                except StopIteration:
                    break  # 文件行数少于10行的情况
                    
            if not lines_cache:
                return f"[-] 跳过 (空文件): {filename}"
                
            # === 规则 1 & 2: 验证第一行（必须是2个整数） ===
            first_line_tokens = lines_cache[0].split()
            if len(first_line_tokens) != 2:
                return f"[-] 跳过 (第一行格式不符): {filename}"
            try:
                int(first_line_tokens[0])
                int(first_line_tokens[1])
            except ValueError:
                return f"[-] 跳过 (第一行包含非整数): {filename}"
                
            # === 规则 3: 验证第2行到第10行 ===
            for line_str in lines_cache[1:10]:
                tokens = line_str.split()
                if len(tokens) < 2:
                    return f"[-] 跳过 (数据行元素不足): {filename}"
                
                try:
                    nums = [int(t) for t in tokens]
                except ValueError:
                    return f"[-] 跳过 (包含非整数字符): {filename}"
                
                # 校验：第二个数字表示后续元素的个数
                # 元素总数 - 2(即忽略前两个数字) 应该等于 count
                element_count = nums[1]
                if len(nums) - 2 != element_count:
                    return f"[-] 跳过 (行元素数量校验失败): {filename}"
                    
            # === 校验通过，写入本地文件 ===
            with open(save_path, 'w', encoding='utf-8') as f:
                # 1. 先将刚刚校验缓存的前 10 行写入
                for cached_line in lines_cache:
                    f.write(cached_line + '\n')
                # 2. 继续流式读取并写入剩余文件内容，确保完整性
                for line in iterator:
                    if line is not None:
                        f.write(line + '\n')
                        
            return f"[+] 成功下载: {filename}"
            
    except requests.exceptions.RequestException as e:
        return f"[!] 请求失败: {filename} - {str(e)}"
    except Exception as e:
        return f"[!] 处理异常: {filename} - {str(e)}"

def main():
    if not os.path.exists(SAVE_DIR):
        os.makedirs(SAVE_DIR)
        print(f"[*] 已创建目录: {SAVE_DIR}")

    print(f"[*] 正在获取网站目录树: {BASE_URL}")
    try:
        response = requests.get(BASE_URL, timeout=15)
        response.raise_for_status()
    except requests.exceptions.RequestException as e:
        print(f"[!] 无法连接到目标网站: {e}")
        return

    # 解析所有的 txt 文件链接
    soup = BeautifulSoup(response.text, 'html.parser')
    txt_links = []
    for a_tag in soup.find_all('a'):
        href = a_tag.get('href')
        if href and href.endswith('.txt'):
            txt_links.append(urljoin(BASE_URL, href))
            
    print(f"[*] 共发现 {len(txt_links)} 个 txt 文件，开始校验与下载...")

    # 使用多线程并发下载，6个线程既能提速又不会对目标服务器造成过大压力
    with ThreadPoolExecutor(max_workers=6) as executor:
        futures = {executor.submit(process_file, url, SAVE_DIR): url for url in txt_links}
        
        for future in as_completed(futures):
            result = future.result()
            # 为了让输出清晰，可以只打印成功和异常的，或者打印所有结果
            # 这里打印所有结果以便追踪
            print(result)

    print("[*] 所有任务处理完毕！")

if __name__ == "__main__":
    main()