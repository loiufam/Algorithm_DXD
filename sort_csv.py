import pandas as pd

df = pd.read_csv('exp_results.csv')
df.sort_values(by=df.columns[0], inplace=True)
df.to_csv('exp_results.csv', index=False)
