import pandas as pd  
import matplotlib.pyplot as plt
import numpy as np

# 读取 CSV 文件
df_pm = pd.read_csv("solve_pM_10.csv")   # pM = f(pS)
df_ps = pd.read_csv("solve_pS_10.csv")   # pS = g(pM)

# -----------------------------------------
# 1. 求两条曲线的交点
#    两个散点集： (pS, pM)
#    两条曲线的交点满足： pM_f = pM_g 且 pS_f = pS_g
#    我们用最近邻方法找到最接近的点对
# -----------------------------------------

points_f = df_pm[["pS", "pM"]].values
points_g = df_ps[["pS", "pM"]].values

# 寻找距离最小的点对
min_dist = float("inf")
intersection = None

for x1, y1 in points_f:
    # 计算当前点到另一曲线所有点的距离
    dist = np.sqrt((points_g[:,0] - x1)**2 + (points_g[:,1] - y1)**2)
    idx = np.argmin(dist)
    if dist[idx] < min_dist:
        min_dist = dist[idx]
        intersection = (x1, y1)

px, py = intersection
print("交点大致位置: pS =", px, ", pM =", py)

# -----------------------------------------
# 2. 绘图
# -----------------------------------------
plt.figure(figsize=(16,16))

# 散点图
plt.scatter(df_pm["pS"], df_pm["pM"], label="pM = f(pS)", s=1, alpha=0.6)
plt.scatter(df_ps["pS"], df_ps["pM"], label="pS = g(pM)", s=1, alpha=0.6)

# 标记交点
plt.scatter([px], [py], color='red', s=80, label=f"Intersection ({px:.4f}, {py:.4f})")
plt.annotate(f"({px:.4f}, {py:.4f})",
             xy=(px, py),
             xytext=(px+0.005, py+0.005),
             fontsize=12,
             color='red')

# 坐标范围
plt.xlim(0, 1)
plt.ylim(0, 1)

plt.xlabel("pS")
plt.ylabel("pM")
plt.title("Intersection of pM(pS) and pS(pM)")
plt.grid(True)
plt.legend()

plt.tight_layout()
plt.savefig('pM_pS_3.png', dpi=1000)
plt.close()
