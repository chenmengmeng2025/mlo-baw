import pandas as pd
import matplotlib.pyplot as plt
import argparse
plt.rcParams.update({'font.size': 14})

def parse_args():
    parser = argparse.ArgumentParser(description='Plot Throughput')
    parser.add_argument('--file', type=str, help='csv result file', required=True)
    return parser.parse_args()
if __name__ == "__main__":
    # 解析命令行参数
    args = parse_args()
    file = args.file
    df = pd.read_csv(file)

    # 创建图表和两个Y轴
    fig, ax1 = plt.subplots(figsize=(16, 7))
    ax2 = ax1.twinx()

    # 绘制吞吐量相关曲线 (左Y轴)
    ln1 = ax1.plot(df[' Time'], df[' Throughput(Mbps)'], 'o-', label='Total Throughput', color='blue')
    ln2 = ax1.plot(df[' Time'], df[' throughput1'], 'o-', label='Throughput on 2.4G', color='green')
    ln3 = ax1.plot(df[' Time'], df[' throughput2'], 'o-', label='Throughput on 5G', color='red')
    ln4 = ax1.plot(df[' Time'], df[' datarate1'], '--', label='Datarate on 2.4G', color='cyan')
    ln5 = ax1.plot(df[' Time'], df[' datarate2'], '--', label='Datarate on 5G', color='magenta')
    ax1.set_xlim(1, df[' Time'].max())
    ax1.set_ylim(0, 3000)
    # 绘制rate相关曲线 (右Y轴)
    ln6 = ax2.plot(df[' Time'], df[' blocktimerate1'], 's:', label='Blocktimerate on 2.4G', color='brown')
    ln7 = ax2.plot(df[' Time'], df[' blocktimerate2'], 's:', label='Blocktimerate on 5G', color='orange')
    ln8 = ax2.plot(df[' Time'], df[' severeblocktimerate1'], '^:', label='Severeblocktimerate on 2.4G', color='#9B870C')  # 暗黄色
    ln9 = ax2.plot(df[' Time'], df[' severeblocktimerate2'], '^:', label='Severeblocktimerate on 5G', color='purple')

    # 设置轴标签和范围
    ax1.set_xlabel('Time (s)')
    ax1.set_ylabel('Throughput/Datarate (Mbps)')
    ax2.set_ylabel('Rate')
    ax2.set_ylim(0, 1)

    # 合并两个轴的图例
    lns = ln1 + ln2 + ln3 + ln4 + ln5 + ln6 + ln7 + ln8 + ln9
    labs = [l.get_label() for l in lns]  
    ax1.legend(lns, labs, loc='center right', bbox_to_anchor=(1.66, 0.5))

    # 设置网格线
    ax1.grid(True, alpha=0.3)

    plt.title('Throughput and Rate Metrics')
    plt.tight_layout()
    # plt.show()
    plt.savefig('throughput.png', dpi=400)