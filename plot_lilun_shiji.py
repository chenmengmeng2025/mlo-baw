import numpy as np
import matplotlib.pyplot as plt
import pandas as pd

# 读取CSV文件
df = pd.read_csv('result_tcp.csv')

# 提取理论值数据
W_theory = df['W'].values
single_link_theory = df['D_tcp_max_slo'].values
multi_link_theory = df['D_tcp_max_mlo'].values

# 离散点位置（用于实际吞吐量标记）
W_discrete = [128, 192, 256, 320, 384, 512, 768, 1024]

# 单链路实际最大吞吐量（更新后的值）
single_link_actual = [0, 0, 1846.43, 0, 0, 0, 0, 0]

# 多链路实际吞吐量（更新后的值）
multi_link_actual = [2186.39, 2275.91, 2321.17, 2352.99, 2464.22, 2570.89, 2595.39, 2593.8]

# 创建图形
plt.figure(figsize=(12, 8), dpi=100)

# 绘制理论曲线
# plt.plot(W_theory, single_link_theory, 'b-', linewidth=2.5, label='Single-link Theoretical Max Throughput')
plt.plot(W_theory, multi_link_theory, 'g-', linewidth=2.5, label='Multi-link Theoretical Throughput (Optimal)')

# 绘制实际吞吐量点（带标记）
# plt.plot(W_discrete, single_link_actual, 'ro', markersize=10, markeredgewidth=2, 
#          markerfacecolor='none', label='Single-link Actual Max Throughput')
plt.plot(W_discrete, multi_link_actual, 'ks', markersize=10, markeredgewidth=2,
         markerfacecolor='none', label='Multi-link Actual Throughput (Optimal)')

# 添加网格和标签
plt.grid(True, linestyle='--', alpha=0.7)
plt.xlabel('Window Size (W)', fontsize=14)
plt.ylabel('Throughput (Mbps)', fontsize=14)
plt.title('Throughput Comparison: Single-link vs Multi-link', fontsize=16)

# 设置坐标轴范围（根据要求改变横轴范围）
plt.xlim(100, 1100)
plt.ylim(2000, 3200)  # 调整纵轴范围以更好地显示数据

# 添加图例
plt.legend(fontsize=12, loc='lower right')

# 添加特殊点的标注
for w, sa, ma in zip(W_discrete, single_link_actual, multi_link_actual):
    plt.annotate(f'{sa:.2f}Mbps', (w, sa), textcoords="offset points", 
                 xytext=(0,10), ha='center', fontsize=9)
    plt.annotate(f'{ma:.2f}Mbps', (w, ma), textcoords="offset points", 
                 xytext=(0,-15), ha='center', fontsize=9)

# 优化布局
plt.tight_layout()

# 保存图片到当前目录
plt.savefig('throughput_comparison_tcp.png', dpi=150, bbox_inches='tight')
print("Chart saved as 'throughput_comparison_tcp.png'")