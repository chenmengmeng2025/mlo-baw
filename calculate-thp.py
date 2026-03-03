import pandas as pd
import numpy as np
from collections import defaultdict

def calculate_throughput_v2(file_path, target_device, time_interval_sec, payload_bytes=1500):
    """
    计算指定设备在每个时间窗口内的吞吐量
    
    参数:
    file_path: CSV文件路径
    target_device: 要统计的设备名称
    time_interval_sec: 时间间隔（秒）
    payload_bytes: 每个MPDU的负载大小
    """
    
    # 读取CSV文件
    df = pd.read_csv(file_path, header=None, names=['device', 'start_time', 'end_time', 'mpdu_count'])
    
    # 过滤出目标设备的数据
    target_data = df[df['device'] == target_device].copy()
    
    if len(target_data) == 0:
        print(f"警告: 设备 '{target_device}' 在文件中没有找到")
        return
    
    target_data['start_sec'] = target_data['start_time'] / 1e6
    target_data['end_sec'] = target_data['end_time'] / 1e6
    
    # 确定时间范围
    min_time = target_data['start_sec'].min()
    max_time = target_data['end_sec'].max()
    # max_time = 10
    
    # 创建时间窗口
    time_windows = []
    window_start = 2
    while window_start <= max_time:
        window_end = window_start + time_interval_sec
        time_windows.append((window_start, window_end))
        window_start = window_end
    
    window_results = []
    
    for window_start, window_end in time_windows:
        # 找出完全在当前窗口内的AMPDU
        window_ampdus = target_data[
            (target_data['start_sec'] >= window_start) & 
            (target_data['end_sec'] <= window_end)
        ]
        
        num_ampdus = len(window_ampdus)
        
        if num_ampdus > 0:
            total_mpdu_in_window = window_ampdus['mpdu_count'].sum()
            
            total_throughput_bps = (total_mpdu_in_window * payload_bytes * 8) / time_interval_sec
          
            window_info = {
                'window': f"{window_start:.3f}-{window_end:.3f}",
                'window_start': window_start,
                'window_end': window_end,
                'num_ampdus': num_ampdus,
                'total_mpdu_in_window': total_mpdu_in_window,
                'total_throughput_bps': total_throughput_bps,
                'total_throughput_mbps': total_throughput_bps / 1e6,
            }
        else:
            window_info = {
                'window': f"{window_start:.3f}-{window_end:.3f}",
                'window_start': window_start,
                'window_end': window_end,
                'num_ampdus': 0,
                'total_mpdu_in_window': 0,
                'total_throughput_bps': 0,
                'total_throughput_mbps': 0,
            }
        
        window_results.append(window_info)
    
    # 创建结果DataFrame
    results_df = pd.DataFrame(window_results)
    
    # 打印汇总信息
    print(f"设备: {target_device}")
    print(f"时间间隔: {time_interval_sec}秒")
    print(f"负载大小: {payload_bytes}字节")
    print(f"总时间范围: {min_time:.3f} - {max_time:.3f}秒")
    
    # 只显示有数据的窗口
    non_zero_results = results_df[results_df['num_ampdus'] > 0]
    
    if len(non_zero_results) > 0:
        print("吞吐量统计结果:")
        print("=" * 60)
        print(f"{'时间窗口(s)':<16} {'AMPDU数量':<10} {'总MPDU数':<10} {'吞吐量(Mbps)':<15}")
        print("-" * 60)
        
        for _, row in non_zero_results.iterrows():
            print(f"{row['window']:<20} {row['num_ampdus']:<12} {row['total_mpdu_in_window']:<12} {row['total_throughput_mbps']:<15.3f}")
        
        print("-" * 60)
        
        # 总体统计
        total_ampdus = non_zero_results['num_ampdus'].sum()
        total_mpdus = non_zero_results['total_mpdu_in_window'].sum()
        total_time_windows = len([w for w in time_windows if w[1] <= max_time])
        avg_throughput = non_zero_results['total_throughput_bps'].sum() / total_time_windows
        
        print(f"总计:")
        print(f"  - AMPDU总数: {total_ampdus}")
        print(f"  - MPDU总数: {total_mpdus}")
        print(f"  - 平均吞吐量: {avg_throughput/1e6:.3f} Mbps")
    else:
        print("警告: 没有找到完全在任何时间窗口内的AMPDU")
    
    return results_df

# 使用示例
if __name__ == "__main__":
    file_path = "scratch/mode-test-udp-1vN-dl/sld-n10-nmpdu1/bothset_baw_512_bw_20_160_mcs_13_13_interference_0_10_seed_4_maxAmpduNum0_0_maxAmpduNum1_462_PPDU.csv"
    
    results1 = calculate_throughput_v2(
        file_path=file_path,
        target_device="MLD1",  # 要统计的设备
        time_interval_sec=2,  # 时间间隔（秒）
        payload_bytes=1500  # 每个MPDU的负载大小（字节）
    )