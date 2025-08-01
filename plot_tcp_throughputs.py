import matplotlib.pyplot as plt
import pandas as pd
import os
import re
plt.rcParams.update({'font.size': 14})
def top_n_indices(arr, n):
    # 创建(值, 下标)元组列表
    indexed = [(value, idx) for idx, value in enumerate(arr)]
    # 按值排序（从大到小）
    sorted_indexed = sorted(indexed, key=lambda x: x[0], reverse=True)
    # 取前n个元素的下标
    return [idx for _, idx in sorted_indexed[:n]]

plt.figure(figsize=(12, 8))  
path = "./scratch/tcp-throughputs"
files = os.listdir(path)
plt.figure(1)
data_list = []
thpts_mean = []
for f in files: # params-1.json 把1提取出来
    df = pd.read_csv(path+"/"+f)
    data_list.append(df)
    thpts_mean.append(df[" Throughput(Mbps)"][-3:].mean())
    
top_ids = top_n_indices(thpts_mean, 5)
for i in top_ids:
    match = re.search(r'(\d+)', files[i]).group(1)
    df = data_list[i]
    plt.plot(df["Time"], df[" Throughput(Mbps)"], "o-", label = "params-"+ match)
plt.xlabel("Time (s)")
plt.ylabel("Throughput (Mbps)")
plt.legend()
plt.savefig("tcp-throughputs-gs.png", dpi=300)
