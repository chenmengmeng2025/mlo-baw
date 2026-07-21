import pandas as pd
import matplotlib.pyplot as plt
import argparse
import os
from matplotlib.patches import Patch

out_file = "Timeline.png"
def parse_args():
    parser = argparse.ArgumentParser(description='Plot PPDU Timeline with RTS/CTS annotations (per-device color)')
    parser.add_argument('--begin', type=float, default=1.50,
                        help='Begin time for the plot (in seconds)')
    parser.add_argument('--end', type=float, default=1.54,
                        help='End time for the plot (in seconds)')
    parser.add_argument(
        "--csv",
        type=str,
        default="scratch/optimal-test-link2/changer1r2-re-4/bothset_baw_1024_bw_20_80_mcs_10_10_interference_1_1_seed_1_maxAmpduNumSld0_128_maxAmpduNumSld1_32_maxAmpduNum0_200_maxAmpduNum1_824_PPDU.csv",
        help="CSV file path for PPDU data (RTSCTS/BA will be auto-derived from this filename)"
    )
    return parser.parse_args()


# ─────────────────────────────────────────────
#  自动判断是否为 3 链路场景
#  规则：PPDU/RTSCTS/BA 中出现 "_2" 后缀 → 3链路
# ─────────────────────────────────────────────
def detect_num_links(df, rtscts_df, ba_df):
    all_types = (
        df["type"].astype(str).tolist()
        + rtscts_df["type"].astype(str).tolist()
        + ba_df["type"].astype(str).tolist()
    )
    for t in all_types:
        last = t.strip().split("_")[-1]
        if last == "2":
            return 3
    return 2


# ─────────────────────────────────────────────
#  颜色表（兼容 2 / 3 链路）
# ─────────────────────────────────────────────
COLOR_MAP = {
    # MLD data frames
    "MLD_0":    "#1f77b4",   # 蓝   - MLD link0 (2.4G)
    "MLD_1":    "#2ca02c",   # 绿   - MLD link1 (5G)
    "MLD_2":    "#ff7f0e",   # 橙   - MLD link2 (6G)
    # SLD data frames
    "SLD2G":    "#ff9999",   # 浅红 - 干扰 2.4G
    "SLD5G":    "#d62728",   # 深红 - 干扰 5G
    "SLD6G":    "#8B0000",   # 暗红 - 干扰 6G
    # AP data frames
    "AP_0":     "#9467bd",   # 紫   - AP link0
    "AP_1":     "#8c564b",   # 棕   - AP link1
    "AP_2":     "#e377c2",   # 粉   - AP link2
    # AP control
    "AP_BA":    "#dd00ff",
    "AP_ACK":   "#4800ff",
}

DERIVED_COLOR_MAP = {
    # AP RTS/CTS
    "AP_CTS":       "#b794f6",
    "AP_RTS":       "#6a51a3",
    # SLD2G RTS/CTS
    "SLD2G_CTS":    "#cc6666",
    "SLD2G_RTS":    "#ffb3b3",
    # SLD5G RTS/CTS
    "SLD5G_CTS":    "#a83232",
    "SLD5G_RTS":    "#ff7f7f",
    # SLD6G RTS/CTS
    "SLD6G_CTS":    "#5a0000",
    "SLD6G_RTS":    "#c05050",
    # MLD RTS/CTS — link0 (2.4G, 蓝系)
    "MLD_CTS_0":    "#63a0e6",
    "MLD_RTS_0":    "#015eb0",
    # MLD RTS/CTS — link1 (5G, 绿系)
    "MLD_CTS_1":    "#74d67a",
    "MLD_RTS_1":    "#1a7a1e",
    # MLD RTS/CTS — link2 (6G, 橙系)
    "MLD_CTS_2":    "#ffbb78",
    "MLD_RTS_2":    "#c45c00",
}


def get_color(name: str) -> str:
    """根据帧类型名称选择颜色，优先匹配 RTS/CTS 专用色，再按设备基色。"""
    for key, color in DERIVED_COLOR_MAP.items():
        if key in name:
            return color
    for key, color in COLOR_MAP.items():
        if key in name:
            return color
    return "gray"


def get_y(name: str) -> int:
    """
    根据帧名确定 y 轴位置（链路编号 0/1/2）。
    支持格式：
      - 末尾 _0 / _1 / _2
      - MLD0 / MLD1 / MLD2 内嵌
    返回 None 表示无法识别（该帧跳过）。
    """
    name = name.strip()

    # 优先：直接内嵌 MLD0/MLD1/MLD2
    for link_id in [2, 1, 0]:
        if f"MLD{link_id}" in name:
            return link_id

    # 次优：末尾下划线数字
    try:
        last_part = name.split("_")[-1].strip()
        if last_part.isdigit():
            val = int(last_part)
            if val in [0, 1, 2]:
                return val
    except Exception:
        pass

    return None


def build_legend(num_links: int):
    """根据链路数量生成图例列表。"""
    link_labels = {
        0: ("2.4G", "link0"),
        1: ("5G",   "link1"),
        2: ("6G",   "link2"),
    }
    sld_keys   = {0: "SLD2G", 1: "SLD5G", 2: "SLD6G"}
    ap_keys    = {0: "AP_0",  1: "AP_1",  2: "AP_2"}
    mld_keys   = {0: "MLD_0", 1: "MLD_1", 2: "MLD_2"}
    rts_keys   = {0: "MLD_RTS_0", 1: "MLD_RTS_1", 2: "MLD_RTS_2"}
    cts_keys   = {0: "MLD_CTS_0", 1: "MLD_CTS_1", 2: "MLD_CTS_2"}

    elements = []
    for i in range(num_links):
        band, lname = link_labels[i]
        elements += [
            Patch(facecolor=COLOR_MAP[mld_keys[i]],  label=f'MLD {band} Data ({lname})'),
            Patch(facecolor=COLOR_MAP[sld_keys[i]],  label=f'SLD {band} Data'),
            Patch(facecolor=COLOR_MAP[ap_keys[i]],   label=f'AP {band} Data ({lname})'),
            Patch(facecolor=DERIVED_COLOR_MAP[rts_keys[i]], label=f'MLD {band} RTS'),
            Patch(facecolor=DERIVED_COLOR_MAP[cts_keys[i]], label=f'MLD {band} CTS'),
        ]
    elements += [
        Patch(facecolor=DERIVED_COLOR_MAP["AP_RTS"], label='AP RTS'),
        Patch(facecolor=DERIVED_COLOR_MAP["AP_CTS"], label='AP CTS'),
        Patch(facecolor=COLOR_MAP["AP_BA"],          label='AP BA'),
        Patch(facecolor=COLOR_MAP["AP_ACK"],         label='AP ACK'),
    ]
    return elements


# ─────────────────────────────────────────────
#  y 轴标签（随链路数量自动生成）
# ─────────────────────────────────────────────
YTICK_LABELS = {
    2: {0: '2.4G (link0)', 1: '5G (link1)'},
    3: {0: '2.4G (link0)', 1: '5G (link1)', 2: '6G (link2)'},
}


# ─────────────────────────────────────────────
#  主程序
# ─────────────────────────────────────────────
if __name__ == "__main__":
    args = parse_args()
    begin = int(args.begin * 1e6)
    end   = int(args.end   * 1e6)

    # 自动根据 PPDU 文件名生成 RTSCTS / BA 文件名
    ppdu_csv = args.csv

    if ppdu_csv.endswith("_PPDU.csv"):
        rtscts_csv = ppdu_csv.replace("_PPDU.csv", "_RTSCTS.csv")
        ba_csv     = ppdu_csv.replace("_PPDU.csv", "_BA.csv")
    else:
        base, ext = os.path.splitext(ppdu_csv)
        rtscts_csv = base + "_RTSCTS.csv"
        ba_csv     = base + "_BA.csv"

    print(f"[plot] PPDU   : {os.path.abspath(ppdu_csv)}")
    print(f"[plot] RTSCTS : {os.path.abspath(rtscts_csv)}")
    print(f"[plot] BA     : {os.path.abspath(ba_csv)}")

    # 读取 CSV
    df = pd.read_csv(ppdu_csv, header=None)
    df.columns = ["type", "start", "end", "num"]

    rtscts_df = pd.read_csv(rtscts_csv, header=None)
    rtscts_df.columns = ["type", "start", "end"]

    ba_df = pd.read_csv(ba_csv, header=None)
    ba_df.columns = ["type", "start", "end"]

    # 自动判断链路数
    num_links = detect_num_links(df, rtscts_df, ba_df)
    print(f"[plot] Detected {num_links}-link scenario.")

    ytick_ids     = list(range(num_links))
    ytick_labels  = [YTICK_LABELS[num_links][i] for i in ytick_ids]
    y_max         = num_links - 1

    # 图形高度随链路数自适应
    fig_height = 3 + num_links * 1.2
    fig, ax = plt.subplots(figsize=(30, fig_height))

    # ── 绘制数据帧 (PPDU) ──────────────────────
    for _, row in df.iterrows():
        type_, x_start, x_end, num = row
        if not (begin < x_start < end):
            continue
        y = get_y(str(type_))
        if y is None or y >= num_links:
            continue
        width = x_end - x_start
        color = get_color(str(type_))
        rect = plt.Rectangle((x_start, y - 0.4), width, 0.8,
                              facecolor=color, alpha=0.6)
        ax.add_patch(rect)
        ax.text(x_start + width / 2, y, num,
                ha='center', va='center', fontsize=7, color='black')

    # ── 绘制 BlockAck ──────────────────────────
    for _, row in ba_df.iterrows():
        type_, x_start, x_end = row
        if not (begin < x_start < end):
            continue
        y = get_y(str(type_))
        if y is None or y >= num_links:
            continue
        width = x_end - x_start
        color = get_color(str(type_))
        rect = plt.Rectangle((x_start, y - 0.4), width, 0.8,
                              facecolor=color, alpha=0.6)
        ax.add_patch(rect)

    # ── 绘制 RTS/CTS（悬浮在帧上方的细条）──────
    for _, row in rtscts_df.iterrows():
        type_, x_start, x_end = row
        if not (begin < x_start < end):
            continue
        y = get_y(str(type_))
        if y is None or y >= num_links:
            continue
        width = x_end - x_start
        color = get_color(str(type_))
        rect = plt.Rectangle((x_start, y + 0.45), width, 0.12,
                              facecolor=color, alpha=0.5,
                              edgecolor='black', linewidth=0.2)
        ax.add_patch(rect)

    # ── 坐标轴 ────────────────────────────────
    ax.set_yticks(ytick_ids)
    ax.set_yticklabels(ytick_labels)
    ax.set_xlabel('Time (µs)')
    ax.set_ylabel('Link')
    ax.set_title(f'PPDU Timeline — {num_links}-Link Scenario')
    ax.set_ylim(-0.7, y_max + 0.9)
    ax.autoscale(enable=True, axis='x', tight=True)

    # ── 图例 ──────────────────────────────────
    legend_elements = build_legend(num_links)
    ax.legend(handles=legend_elements, loc='upper right',
              ncol=3, fontsize=7, framealpha=0.8)

    # ── 输出 ──────────────────────────────────
    plt.tight_layout()

    plt.savefig(out_file, dpi=400)

    abs_path = os.path.abspath(out_file)
    print(f"[plot] Saved → {abs_path}")