import pandas as pd
import matplotlib.pyplot as plt
import argparse
import os
from matplotlib.patches import Patch

def parse_args():
    parser = argparse.ArgumentParser(description='Plot PPDU Timeline with RTS/CTS annotations (per-device color)')
    parser.add_argument('--begin', type=float, default=5.20,
                        help='Begin time for the plot (in seconds)')
    parser.add_argument('--end', type=float, default=5.23,
                        help='End time for the plot (in seconds)')
    parser.add_argument("--tcp", action="store_true", help="Enable TCP mode")
    parser.add_argument("--csv", type=str,
                        default="scratch/mode-test-udp-1vN-dl/debug104/bothset_baw_1024_bw_20_160_mcs_13_13_interference_10_10_seed_2_maxAmpduNum0_0_maxAmpduNum1_920_PPDU.csv",
                        help="CSV file path for PPDU data")
    parser.add_argument("--rtscts", type=str,
                        default="scratch/mode-test-udp-1vN-dl/debug104/bothset_baw_1024_bw_20_160_mcs_13_13_interference_10_10_seed_2_maxAmpduNum0_0_maxAmpduNum1_920_RTSCTS.csv",
                        help="CSV file path for RTS/CTS data")
    parser.add_argument("--ba", type=str,
                    default="scratch/mode-test-udp-1vN-dl/debug104/bothset_baw_1024_bw_20_160_mcs_13_13_interference_10_10_seed_2_maxAmpduNum0_0_maxAmpduNum1_920_BA.csv",
                    help="CSV file path for BlockAck data")
    return parser.parse_args()

if __name__ == "__main__":
    args = parse_args()
    begin = int(args.begin * 1e6)
    end = int(args.end * 1e6)
    tcp = bool(args.tcp)

    df = pd.read_csv(args.csv, header=None)
    df.columns = ["type", "start", "end", "num"]

    rtscts_df = pd.read_csv(args.rtscts, header=None)
    rtscts_df.columns = ["type", "start", "end"]

    ba_df = pd.read_csv(args.ba, header=None)
    ba_df.columns = ["type", "start", "end"]

    color_map = {
        "MLD0": "#1f77b4",   # 蓝 - MLD link0
        "MLD1": "#2ca02c",   # 绿 - MLD link1
        "SLD2G": "#ff9999",  # 浅红 - 干扰 2G
        "SLD5G": "#d62728",  # 深红 - 干扰 5G
        "AP_0": "#9467bd",   # 紫 - AP 0
        "AP_1": "#8c564b",   # 棕 - AP 1
        "AP_BA": "#dd00ff",
        "AP_ACK": "#4800ff",
    }

    # 为不同设备的RTS/CTS定义衍生颜色（亮/暗）
    derived_color_map = {
        "AP_CTS": "#b794f6",      # 浅紫
        "AP_RTS": "#6a51a3",      # 深紫
        "SLD2G_CTS": "#cc6666",   # 更浅红
        "SLD2G_RTS":  "#ffb3b3",   # 暗红
        "SLD5G_CTS": "#a83232",   # 更亮红
        "SLD5G_RTS": "#ff7f7f",   # 深红
        "MLD_CTS_0": "#63a0e6",    # 浅蓝
        "MLD_RTS_0": "#015eb0",    # 深蓝
        "MLD_CTS_1": "#2ca02c",    # 浅绿
        "MLD_RTS_1": "#74d67a",    # 深绿
    }

    def get_color(name: str):
        """根据帧类型选择颜色"""
        # 优先匹配 RTS/CTS 专用色
        for key in derived_color_map.keys():
            if key in name:
                return derived_color_map[key]

        # 否则按设备基色
        for key in color_map.keys():
            if key in name:
                return color_map[key]

        # 默认灰色
        return "gray"

    def get_y(name: str):
        """根据帧名确定y位置（链路编号）：
        - *_0 → link0
        - *_1 → link1
        - MLD0 → link0
        - MLD1 → link1
        """
        name = name.strip()

        # ✅ 情况1：末尾直接是 MLD0 / MLD1
        if "MLD1" in name:
            return 1
        if "MLD0" in name:
            return 0

        # ✅ 情况2：根据最后一段下划线判断
        try:
            last_part = name.split("_")[-1].strip()
            if last_part.isdigit():
                val = int(last_part)
                if val in [0, 1]:
                    return val
        except Exception:
            pass

    fig, ax = plt.subplots(figsize=(30, 4))

    for _, row in df.iterrows():
        type_, x_start, x_end, num = row
        if x_start > begin and x_start < end:
            width = x_end - x_start
            y = get_y(type_)
            color = get_color(type_)
            rect = plt.Rectangle((x_start, y - 0.4), width, 0.8,
                                 facecolor=color, alpha=0.6)
            ax.add_patch(rect)
            ax.text(x_start + width / 2, y, num,
                    ha='center', va='center', fontsize=7, color='black')
            
    for _, row in ba_df.iterrows():
        type_, x_start, x_end = row
        if x_start > begin and x_start < end:
            width = x_end - x_start
            y = get_y(type_)
            color = get_color(type_)
            rect = plt.Rectangle((x_start, y - 0.4), width, 0.8,
                                 facecolor=color, alpha=0.6)
            ax.add_patch(rect)

    for i, row in rtscts_df.iterrows():
        type_, x_start, x_end = row
        if x_start < begin or x_start > end:
            continue

        width = x_end - x_start
        y = get_y(type_)
        color = get_color(type_)
        rect = plt.Rectangle((x_start, y + 0.45), width, 0.12,
                            facecolor=color, alpha=0.5, edgecolor='black', linewidth=0.2)
        ax.add_patch(rect)

    ax.set_yticks([0, 1])
    ax.set_yticklabels(['2.4G (link0)', '5G (link1)'])
    ax.set_xlabel('Time (µs)')
    ax.set_ylabel('Link')
    ax.set_title('PPDU Timeline with Per-Device RTS/CTS Colors')

    # # 自动生成图例
    # legend_elements = [
    #     Patch(facecolor=color_map["MLD0"], label='MLD0 Data'),
    #     Patch(facecolor=color_map["MLD1"], label='MLD1 Data'),
    #     Patch(facecolor=color_map["SLD2G"], label='SLD2G Data'),
    #     Patch(facecolor=color_map["SLD5G"], label='SLD5G Data'),
    #     Patch(facecolor=color_map["AP_0"], label='AP_0 Data'),
    #     Patch(facecolor=color_map["AP_1"], label='AP_1 Data'),
    #     Patch(facecolor=derived_color_map["AP_RTS"], label='AP RTS'),
    #     Patch(facecolor=derived_color_map["AP_CTS"], label='AP CTS'),
    #     Patch(facecolor=derived_color_map["SLD2G_RTS"], label='SLD2G RTS'),
    #     Patch(facecolor=derived_color_map["SLD2G_CTS"], label='SLD2G CTS'),
    #     Patch(facecolor=derived_color_map["SLD5G_RTS"], label='SLD5G RTS'),
    #     Patch(facecolor=derived_color_map["SLD5G_CTS"], label='SLD5G CTS'),
    # ]
    # ax.legend(handles=legend_elements, loc='upper right', ncol=2)

    ax.set_ylim(-0.7, 1.8)
    ax.autoscale(enable=True, axis='x', tight=True)
    plt.tight_layout()
    plt.savefig('PPDU_Timeline_with_RTSCTS_colored2.png', dpi=400)
    plt.show()
