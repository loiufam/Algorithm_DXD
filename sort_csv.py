import pandas as pd
import re

df = pd.read_csv("exp_results.csv", header=None)

def parse_grafo(name):
    # 匹配 grafo1000.47
    m = re.match(r"grafo(\d+)\.(\d+)", str(name))
    if m:
        return int(m.group(1)), int(m.group(2))
    return float("inf"), float("inf")

# 从第一列提取排序键
first_col = df.columns[0]
df[["_id", "_mid"]] = df[first_col].apply(
    lambda x: pd.Series(parse_grafo(x))
)

# 排序：先 grafo 编号，再 mid
df.sort_values(by=["_id", "_mid"], inplace=True)

# 删除临时列
df.drop(columns=["_id", "_mid"], inplace=True)

# 写回 CSV（保留表头）
df.to_csv("exp_results.csv", index=False)

