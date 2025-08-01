import json
import os

def generate_json_files(output_dir, start_id=0, num_files=10, 
                       ampdulimits_min=0):
    """
    生成多个JSON文件，变化AmpduLimits参数
    
    参数:
    output_dir: 输出目录
    start_id: 起始文件ID (用于文件命名)
    num_files: 生成的文件数量
    link1_min: 链路1的最小值
    link1_max: 链路1的最大值
    link2_min: 链路2的最小值
    link2_max: 链路2的最大值
    """
    
    # 创建输出目录（如果不存在）
    os.makedirs(output_dir, exist_ok=True)
    
    # 基础模板（固定参数部分）
    base_params = {
        "CWmins": [1, 1],
        "CWmaxs": [3, 3],
        "Aifsns": [2, 2],
        "RTS/CTS": [1, 0],
        "TxopLimits": [0, 0],
        "AmpduSize": [0, 0],
        "MaxSlrc": [4294967295, 4294967295],
        "MaxSsrc": [4294967295, 4294967295],
        "RedundancyThreshold": [0.5, 0.5],
        "RedundancyFixedNumber": [0, 0],
        "link1Pcts": [0.0]
    }
    
    step = 2
    
    # 生成每个JSON文件
    for i in range(num_files):
        # 计算当前值（线性插值）
        ampdulimit1 = int(ampdulimits_min + i * step)
        ampdulimit2 = 256 - ampdulimit1
        
        # 创建完整参数（只变化AmpduLimits）
        current_params = {
            **base_params,
            "AmpduLimits": [ampdulimit1, ampdulimit2]
        }
        
        # 构建JSON数据结构
        json_data = {
            "params": [current_params]
        }
        
        # 构建文件名
        file_id = start_id + i
        filename = os.path.join(output_dir, f"params-{file_id}.json")
        
        # 写入文件
        with open(filename, 'w') as f:
            json.dump(json_data, f, indent=4)
    
    print(f"Successfully generated {num_files} files in {output_dir}")

# 配置参数
output_directory = "./scratch/tcp-params"  # 输出目录
start_id = 1     # 起始文件ID
num_files = 20    # 文件数量

ampdulimits_min = 0

# 生成文件
generate_json_files(
    output_directory, 
    start_id, 
    num_files,
    ampdulimits_min, 
)