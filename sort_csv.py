import pandas as pd
import re

df = pd.read_csv("exp_results.csv", header=None)

def parse_name(name):
    s = str(name).strip()
    # 匹配 grafo10116.100 格式
    m = re.match(r"grafo(\d+)\.(\d+)", s)
    if m:
        return (1, int(m.group(1)), int(m.group(2)), "")
    # 非 grafo 格式，按原始字符串字母序排列在前
    return (0, 0, 0, s)

sort_keys = df[0].apply(lambda x: parse_name(x))
df["_sort"] = sort_keys
df.sort_values(by="_sort", inplace=True)
df.drop(columns=["_sort"], inplace=True)

df.to_csv("exp_results.csv", index=False, header=False)