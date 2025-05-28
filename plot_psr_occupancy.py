import pandas as pd
import matplotlib.pyplot as plt
import argparse
plt.rcParams.update({'font.size': 14})

# 添加命令行参数解析
def parse_args():
    parser = argparse.ArgumentParser(description='Plot PSR and Occupancy Rate Metrics')
    parser.add_argument('--file', type=str, required=True, help='Path to the input CSV file')
    return parser.parse_args()

if __name__ == "__main__":
    # 解析命令行参数
    args = parse_args()
    file = args.file

    # 读取数据
    df = pd.read_csv(file)

    # 创建图表
    fig, ax1 = plt.subplots(figsize=(16, 7))

    # 绘制PSR相关曲线
    ln1 = ax1.plot(df[' Time'], df[' PSR1'], 'o-', label='PSR on 2.4G', color='green')
    ln2 = ax1.plot(df[' Time'], df[' PSR2'], 'o-', label='PSR on 5G', color='red')

    # 绘制Occupancy Rate相关曲线
    ln3 = ax1.plot(df[' Time'], df[' Occupancy Rate 1'], '--', label='Occupancy Rate on 2.4G', color='green')
    ln4 = ax1.plot(df[' Time'], df[' Occupancy Rate 2'], '--', label='Occupancy Rate on 5G', color='red')

    # 设置轴标签
    ax1.set_xlabel('Time (s)')
    ax1.set_ylabel('Rate')

    # 合并图例
    lns = ln1 + ln2 + ln3 + ln4
    labs = [l.get_label() for l in lns]
    ax1.legend(lns, labs, loc='center right', bbox_to_anchor=(1.3, 0.5))
    # 设置网格线
    ax1.grid(True, alpha=0.3)

    plt.title('PSR and Occupancy Rate Metrics')
    plt.tight_layout()
    plt.show()
