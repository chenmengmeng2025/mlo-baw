import subprocess
import sys
import re
import os
from datetime import datetime

def generate_log_filename(cmd):
    """根据命令参数生成日志文件名和文件夹路径"""
    # 提取命令中的参数
    params = {}
    
    # 使用正则表达式提取参数值
    patterns = {
        'nsld0': r'--nsld0=(\d+)',
        'nsld1': r'--nsld1=(\d+)', 
        'nsld2': r'--nsld2=(\d+)',
        'bawsize': r'--bawsize=(\d+)',
        'bw1': r'--bw1=(\d+)',
        'bw2': r'--bw2=(\d+)',
        'bw3': r'--bw3=(\d+)',
        'mcs1': r'--mcs1=(\d+)',
        'mcs2': r'--mcs2=(\d+)',
        'mcs3': r'--mcs3=(\d+)',
        'nss' : r'--nss=(\d+)',
        'simt': r'--simt=(\d+)',
        'policy': r'--policy=(\d+)',
        'seed': r'--seed=(\d+)',
        'maxampdunum0': r'--maxampdunum0=(\d+)',
        'maxampdunum1': r'--maxampdunum1=(\d+)',
        'maxampdunum2': r'--maxampdunum2=(\d+)',
        'scenario': r'--scenario=([\w-]+)', 
        'SLDinterval': r'--SLDinterval=([\d.]+)',
        'ampdunumsld0': r'--ampdunumsld0=(\d+)',
        'ampdunumsld1': r'--ampdunumsld1=(\d+)',
        'ampdunumsld2': r'--ampdunumsld2=(\d+)',
        'fixedPER0': r'--fixedPER0=([\d.]+)',
        'fixedPER1': r'--fixedPER1=([\d.]+)',
        'fixedPER2': r'--fixedPER2=([\d.]+)',
    }
    
    for key, pattern in patterns.items():
        match = re.search(pattern, cmd)
        if match:
            params[key] = match.group(1)
    
    # 生成文件名
    filename_parts = ["output"]
    
    # 添加关键参数到文件名
    if 'policy' in params:
        policyint = int(params['policy'])
        policy_mapping = {
            1: "greedy", 
            2: "damla",
            3: "only2G",
            4: "only5G",
            5: "only6G",
            6: "bothset",
        }
        policy = policy_mapping.get(policyint, f"unknown{policyint}")
        filename_parts.append(f"{policy}")
    if 'bawsize' in params:
        filename_parts.append(f"baw{params['bawsize']}")
    if 'bw1' in params:
        filename_parts.append(f"bw1_{params['bw1']}")
    if 'bw2' in params:
        filename_parts.append(f"bw2_{params['bw2']}")
    if 'bw3' in params:
        filename_parts.append(f"bw3_{params['bw3']}")
    if 'mcs1' in params:
        filename_parts.append(f"mcs1_{params['mcs1']}")
    if 'mcs2' in params:
        filename_parts.append(f"mcs2_{params['mcs2']}")
    if 'mcs3' in params:
        filename_parts.append(f"mcs3_{params['mcs3']}")
    if 'nss' in params:
        filename_parts.append(f"nss_{params['nss']}")
    if 'nsld0' in params and int(params['nsld0']) != 0:
        filename_parts.append(f"nsld0_{params['nsld0']}")
        if 'ampdunumsld0' in params and int(params['ampdunumsld0']) != 0:
            filename_parts.append(f"ampdu0_{params['ampdunumsld0']}")
    if 'nsld1' in params and int(params['nsld1']) != 0:
        filename_parts.append(f"nsld1_{params['nsld1']}")
        if 'ampdunumsld1' in params and int(params['ampdunumsld1']) != 0:
            filename_parts.append(f"ampdu1_{params['ampdunumsld1']}")
    if 'nsld2' in params and int(params['nsld2']) != 0:
        filename_parts.append(f"nsld2_{params['nsld2']}")
        if 'ampdunumsld2' in params and int(params['ampdunumsld2']) != 0:
            filename_parts.append(f"ampdu2_{params['ampdunumsld2']}")
    if 'fixedPER0' in params and float(params['fixedPER0']) != 0.0:
        filename_parts.append(f"PER0_{params['fixedPER0']}")
    if 'fixedPER1' in params and float(params['fixedPER1']) != 0.0:
        filename_parts.append(f"PER1_{params['fixedPER1']}")
    if 'fixedPER2' in params and float(params['fixedPER2']) != 0.0:
        filename_parts.append(f"PER2_{params['fixedPER2']}")
    if 'seed' in params:    
        filename_parts.append(f"seed{params['seed']}")
    if 'maxampdunum0' in params and int(params['maxampdunum0']) != 0:
        filename_parts.append(f"maxampdu0_{params['maxampdunum0']}")
    if 'maxampdunum1' in params and int(params['maxampdunum1']) != 0:
        filename_parts.append(f"maxampdu1_{params['maxampdunum1']}")
    if 'maxampdunum2' in params and int(params['maxampdunum2']) != 0:
        filename_parts.append(f"maxampdu2_{params['maxampdunum2']}")
    if 'SLDinterval' in params and float(params['SLDinterval']) != 1.0:
        filename_parts.append(f"sldinterval_{params['SLDinterval']}")
    
    
    filename = "_".join(filename_parts) + ".log"
    
    # 获取scenario参数作为文件夹名
    folder_name = params.get('scenario', 'default_scenario')
    
    return folder_name, filename

def run_command(cmd):
    """执行单个命令并等待完成，显示实时输出"""
    # 生成日志文件夹和文件名
    folder_name, log_filename = generate_log_filename(cmd)
    
    # 创建文件夹（如果不存在）
    if not os.path.exists(folder_name):
        os.makedirs(folder_name)
        print(f"创建文件夹: {folder_name}")
    
    # 完整的日志文件路径
    log_file_path = os.path.join(folder_name, log_filename)
    
    print(f"执行命令: {cmd}")
    print(f"日志文件: {log_file_path}")
    print("-" * 50)
    
    try:
        # 替换命令中的重定向部分（如果有的话）
        if "&>" in cmd:
            base_cmd = cmd.split("&>")[0].strip()
        else:
            base_cmd = cmd
            
        # 使用新的日志文件路径
        final_cmd = f"{base_cmd} &> {log_file_path}"
        
        process = subprocess.Popen(
            final_cmd, 
            shell=True, 
            stdout=subprocess.PIPE, 
            stderr=subprocess.STDOUT,
            universal_newlines=True,
            executable="/bin/bash"  # 指定使用bash而不是sh
        )
        
        # 实时打印输出
        for line in process.stdout:
            print(line, end='')
            
        process.wait()
        
        if process.returncode != 0:
            print(f"命令执行失败，退出码: {process.returncode}")
            sys.exit(1)
            
    except Exception as e:
        print(f"执行命令时发生错误: {e}")
        sys.exit(1)

# 原始命令列表（不再需要手动指定日志文件名）
commands = [

    # # 第1组: mcs1=4, bw1=40, mcs2=10, bw2=80, nsld0=4, nsld1=1, bawsize=1024, maxampdunum0=0, maxampdunum1=1024
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --mcs1=4 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0 --maxampdunum0=0 --maxampdunum1=1024",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=2 --bw1=40 --bw2=80 --mcs1=4 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=4 --bw1=40 --bw2=80 --mcs1=4 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=1 --bw1=40 --bw2=80 --mcs1=4 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",

    # # 第2组: mcs1=9, bw1=20, mcs2=10, bw2=80, nsld0=4, nsld1=1, bawsize=1024, maxampdunum0=12, maxampdunum1=1012
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=20 --bw2=80 --mcs1=9 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0 --maxampdunum0=12 --maxampdunum1=1012",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=2 --bw1=20 --bw2=80 --mcs1=9 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=4 --bw1=20 --bw2=80 --mcs1=9 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=1 --bw1=20 --bw2=80 --mcs1=9 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",

    # # 第3组: mcs1=10, bw1=20, mcs2=10, bw2=80, nsld0=4, nsld1=1, bawsize=1024, maxampdunum0=52, maxampdunum1=972
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=20 --bw2=80 --mcs1=10 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0 --maxampdunum0=52 --maxampdunum1=972",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=2 --bw1=20 --bw2=80 --mcs1=10 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=4 --bw1=20 --bw2=80 --mcs1=10 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=1 --bw1=20 --bw2=80 --mcs1=10 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",

    # # 第4组: mcs1=5, bw1=40, mcs2=10, bw2=80, nsld0=4, nsld1=1, bawsize=1024, maxampdunum0=75, maxampdunum1=949
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --mcs1=5 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0 --maxampdunum0=75 --maxampdunum1=949",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=2 --bw1=40 --bw2=80 --mcs1=5 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=4 --bw1=40 --bw2=80 --mcs1=5 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=1 --bw1=40 --bw2=80 --mcs1=5 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",

    # # 第5组: mcs1=11, bw1=20, mcs2=10, bw2=80, nsld0=4, nsld1=1, bawsize=1024, maxampdunum0=90, maxampdunum1=934
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=20 --bw2=80 --mcs1=11 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0 --maxampdunum0=90 --maxampdunum1=934",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=2 --bw1=20 --bw2=80 --mcs1=11 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=4 --bw1=20 --bw2=80 --mcs1=11 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=1 --bw1=20 --bw2=80 --mcs1=11 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=17 --scenario=resub-sld-2 --logsender=0",

    # # 第6组: mcs1=6, bw1=40, mcs2=10, bw2=80, nsld0=4, nsld1=1, bawsize=1024, maxampdunum0=119, maxampdunum1=905
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --mcs1=6 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0 --maxampdunum0=119 --maxampdunum1=905",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=2 --bw1=40 --bw2=80 --mcs1=6 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=4 --bw1=40 --bw2=80 --mcs1=6 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=1 --bw1=40 --bw2=80 --mcs1=6 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",

    # # 第7组: mcs1=7, bw1=40, mcs2=10, bw2=80, nsld0=4, nsld1=1, bawsize=1024, maxampdunum0=162, maxampdunum1=862
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --mcs1=7 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0 --maxampdunum0=162 --maxampdunum1=862",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=2 --bw1=40 --bw2=80 --mcs1=7 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=4 --bw1=40 --bw2=80 --mcs1=7 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=1 --bw1=40 --bw2=80 --mcs1=7 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",

    # # 第8组: mcs1=8, bw1=40, mcs2=10, bw2=80, nsld0=4, nsld1=1, bawsize=1024, maxampdunum0=241, maxampdunum1=783
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --mcs1=8 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0 --maxampdunum0=241 --maxampdunum1=783",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=2 --bw1=40 --bw2=80 --mcs1=8 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=4 --bw1=40 --bw2=80 --mcs1=8 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=1 --bw1=40 --bw2=80 --mcs1=8 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",

    # # 第9组: mcs1=9, bw1=40, mcs2=10, bw2=80, nsld0=4, nsld1=1, bawsize=1024, maxampdunum0=289, maxampdunum1=735
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --mcs1=9 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0 --maxampdunum0=289 --maxampdunum1=735",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=2 --bw1=40 --bw2=80 --mcs1=9 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=4 --bw1=40 --bw2=80 --mcs1=9 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=1 --bw1=40 --bw2=80 --mcs1=9 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",

    # # 第10组: mcs1=10, bw1=40, mcs2=10, bw2=80, nsld0=4, nsld1=1, bawsize=1024, maxampdunum0=346, maxampdunum1=678
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --mcs1=10 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0 --maxampdunum0=346 --maxampdunum1=678",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=2 --bw1=40 --bw2=80 --mcs1=10 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=4 --bw1=40 --bw2=80 --mcs1=10 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=1 --bw1=40 --bw2=80 --mcs1=10 --mcs2=10 --nss=2 --ampdunumsld0=96 --ampdunumsld1=928 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",

    # # 第11组: mcs1=4, bw1=40, mcs2=10, bw2=80, nsld0=4, nsld1=1, bawsize=256, maxampdunum0=0, maxampdunum1=256
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=6 --bw1=40 --bw2=80 --mcs1=4 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0 --maxampdunum0=0 --maxampdunum1=256",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=2 --bw1=40 --bw2=80 --mcs1=4 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=4 --bw1=40 --bw2=80 --mcs1=4 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=1 --bw1=40 --bw2=80 --mcs1=4 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",

    # # 第12组: mcs1=9, bw1=20, mcs2=10, bw2=80, nsld0=4, nsld1=1, bawsize=256, maxampdunum0=0, maxampdunum1=256
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=6 --bw1=20 --bw2=80 --mcs1=9 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0 --maxampdunum0=0 --maxampdunum1=256",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=2 --bw1=20 --bw2=80 --mcs1=9 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=4 --bw1=20 --bw2=80 --mcs1=9 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=1 --bw1=20 --bw2=80 --mcs1=9 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",

    # # 第13组: mcs1=10, bw1=20, mcs2=10, bw2=80, nsld0=4, nsld1=1, bawsize=256, maxampdunum0=3, maxampdunum1=253
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=6 --bw1=20 --bw2=80 --mcs1=10 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0 --maxampdunum0=3 --maxampdunum1=253",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=2 --bw1=20 --bw2=80 --mcs1=10 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=4 --bw1=20 --bw2=80 --mcs1=10 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=1 --bw1=20 --bw2=80 --mcs1=10 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",

    # # 第14组: mcs1=5, bw1=40, mcs2=10, bw2=80, nsld0=4, nsld1=1, bawsize=256, maxampdunum0=9, maxampdunum1=247
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=6 --bw1=40 --bw2=80 --mcs1=5 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0 --maxampdunum0=9 --maxampdunum1=247",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=2 --bw1=40 --bw2=80 --mcs1=5 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=4 --bw1=40 --bw2=80 --mcs1=5 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=1 --bw1=40 --bw2=80 --mcs1=5 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",

    # # 第15组: mcs1=11, bw1=20, mcs2=10, bw2=80, nsld0=4, nsld1=1, bawsize=256, maxampdunum0=12, maxampdunum1=244
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=6 --bw1=20 --bw2=80 --mcs1=11 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0 --maxampdunum0=12 --maxampdunum1=244",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=2 --bw1=20 --bw2=80 --mcs1=11 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=4 --bw1=20 --bw2=80 --mcs1=11 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=1 --bw1=20 --bw2=80 --mcs1=11 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=17 --scenario=resub-sld-2 --logsender=0",

    # # 第16组: mcs1=6, bw1=40, mcs2=10, bw2=80, nsld0=4, nsld1=1, bawsize=256, maxampdunum0=19, maxampdunum1=237
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=6 --bw1=40 --bw2=80 --mcs1=6 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0 --maxampdunum0=19 --maxampdunum1=237",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=2 --bw1=40 --bw2=80 --mcs1=6 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=4 --bw1=40 --bw2=80 --mcs1=6 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=1 --bw1=40 --bw2=80 --mcs1=6 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",

    # # 第17组: mcs1=7, bw1=40, mcs2=10, bw2=80, nsld0=4, nsld1=1, bawsize=256, maxampdunum0=29, maxampdunum1=227
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=6 --bw1=40 --bw2=80 --mcs1=7 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0 --maxampdunum0=29 --maxampdunum1=227",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=2 --bw1=40 --bw2=80 --mcs1=7 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=4 --bw1=40 --bw2=80 --mcs1=7 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=1 --bw1=40 --bw2=80 --mcs1=7 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",

    # # 第18组: mcs1=8, bw1=40, mcs2=10, bw2=80, nsld0=4, nsld1=1, bawsize=256, maxampdunum0=47, maxampdunum1=209
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=6 --bw1=40 --bw2=80 --mcs1=8 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0 --maxampdunum0=47 --maxampdunum1=209",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=2 --bw1=40 --bw2=80 --mcs1=8 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=4 --bw1=40 --bw2=80 --mcs1=8 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=1 --bw1=40 --bw2=80 --mcs1=8 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=20 --scenario=resub-sld-2 --logsender=0",

    # # 第19组: mcs1=9, bw1=40, mcs2=10, bw2=80, nsld0=4, nsld1=1, bawsize=256, maxampdunum0=58, maxampdunum1=198
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=6 --bw1=40 --bw2=80 --mcs1=9 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0 --maxampdunum0=58 --maxampdunum1=198",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=2 --bw1=40 --bw2=80 --mcs1=9 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=4 --bw1=40 --bw2=80 --mcs1=9 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=1 --bw1=40 --bw2=80 --mcs1=9 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",

    # # 第20组: mcs1=10, bw1=40, mcs2=10, bw2=80, nsld0=4, nsld1=1, bawsize=256, maxampdunum0=71, maxampdunum1=185
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=6 --bw1=40 --bw2=80 --mcs1=10 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0 --maxampdunum0=71 --maxampdunum1=185",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=2 --bw1=40 --bw2=80 --mcs1=10 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=4 --bw1=40 --bw2=80 --mcs1=10 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=4 --nsld1=1 --simt=16.5 --bawsize=256 --policy=1 --bw1=40 --bw2=80 --mcs1=10 --mcs2=10 --nss=2 --ampdunumsld0=24 --ampdunumsld1=232 --fixedPER0=0 --fixedPER1=0 --seed=14 --scenario=resub-sld-2 --logsender=0",

    # "build/scratch/optimal-test-link3/ns3.43-link3-optimized --nsld0=4 --nsld1=4 --nsld2=4 --simt=21.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --bw3=160 --mcs1=10 --mcs2=10 --mcs3=10 --nss=4 --ampdunumsld0=32 --ampdunumsld1=32 --ampdunumsld2=32 --maxampdunum0=70 --maxampdunum1=275 --maxampdunum2=679 --fixedPER0=0 --fixedPER1=0 --fixedPER2=0  --seed=5 --scenario=3link",
    # "build/scratch/optimal-test-link3/ns3.43-link3-optimized --nsld0=4 --nsld1=4 --nsld2=4 --simt=21.5 --bawsize=1024 --policy=5 --bw1=40 --bw2=80 --bw3=160 --mcs1=10 --mcs2=10 --mcs3=10 --nss=4 --ampdunumsld0=32 --ampdunumsld1=32 --ampdunumsld2=32 --fixedPER0=0 --fixedPER1=0 --fixedPER2=0  --seed=5 --scenario=3link",
    # "build/scratch/optimal-test-link3/ns3.43-link3-optimized --nsld0=4 --nsld1=4 --nsld2=4 --simt=21.5 --bawsize=1024 --policy=1 --bw1=40 --bw2=80 --bw3=160 --mcs1=10 --mcs2=10 --mcs3=10 --nss=4 --ampdunumsld0=32 --ampdunumsld1=32 --ampdunumsld2=32 --fixedPER0=0 --fixedPER1=0 --fixedPER2=0  --seed=5 --scenario=3link",

    # "build/scratch/optimal-test-link3/ns3.43-link3-optimized --nsld0=4 --nsld1=4 --nsld2=4 --simt=21.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --bw3=160 --mcs1=5 --mcs2=10 --mcs3=10 --nss=4 --ampdunumsld0=32 --ampdunumsld1=32 --ampdunumsld2=32 --maxampdunum0=0 --maxampdunum1=299 --maxampdunum2=725 --fixedPER0=0 --fixedPER1=0 --fixedPER2=0  --seed=5 --scenario=3link",
    # "build/scratch/optimal-test-link3/ns3.43-link3-optimized --nsld0=4 --nsld1=4 --nsld2=4 --simt=21.5 --bawsize=1024 --policy=5 --bw1=40 --bw2=80 --bw3=160 --mcs1=5 --mcs2=10 --mcs3=10 --nss=4 --ampdunumsld0=32 --ampdunumsld1=32 --ampdunumsld2=32 --fixedPER0=0 --fixedPER1=0 --fixedPER2=0  --seed=5 --scenario=3link",
    # "build/scratch/optimal-test-link3/ns3.43-link3-optimized --nsld0=4 --nsld1=4 --nsld2=4 --simt=21.5 --bawsize=1024 --policy=1 --bw1=40 --bw2=80 --bw3=160 --mcs1=5 --mcs2=10 --mcs3=10 --nss=4 --ampdunumsld0=32 --ampdunumsld1=32 --ampdunumsld2=32 --fixedPER0=0 --fixedPER1=0 --fixedPER2=0  --seed=5 --scenario=3link",

    # "build/scratch/optimal-test-link3/ns3.43-link3-optimized --nsld0=6 --nsld1=4 --nsld2=2 --simt=21.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --bw3=160 --mcs1=10 --mcs2=10 --mcs3=10 --nss=4 --ampdunumsld0=192 --ampdunumsld1=32 --ampdunumsld2=32 --maxampdunum0=0 --maxampdunum1=203 --maxampdunum2=821 --fixedPER0=0 --fixedPER1=0 --fixedPER2=0  --seed=5 --scenario=3link",
    # "build/scratch/optimal-test-link3/ns3.43-link3-optimized --nsld0=6 --nsld1=4 --nsld2=2 --simt=21.5 --bawsize=1024 --policy=5 --bw1=40 --bw2=80 --bw3=160 --mcs1=10 --mcs2=10 --mcs3=10 --nss=4 --ampdunumsld0=192 --ampdunumsld1=32 --ampdunumsld2=32 --fixedPER0=0 --fixedPER1=0 --fixedPER2=0  --seed=5 --scenario=3link",
    # "build/scratch/optimal-test-link3/ns3.43-link3-optimized --nsld0=6 --nsld1=4 --nsld2=2 --simt=21.5 --bawsize=1024 --policy=1 --bw1=40 --bw2=80 --bw3=160 --mcs1=10 --mcs2=10 --mcs3=10 --nss=4 --ampdunumsld0=192 --ampdunumsld1=32 --ampdunumsld2=32 --fixedPER0=0 --fixedPER1=0 --fixedPER2=0  --seed=5 --scenario=3link",

    # "build/scratch/optimal-test-link3/ns3.43-link3-optimized --nsld0=4 --nsld1=6 --nsld2=2 --simt=21.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --bw3=160 --mcs1=10 --mcs2=10 --mcs3=10 --nss=4 --ampdunumsld0=32 --ampdunumsld1=192 --ampdunumsld2=32 --maxampdunum0=67 --maxampdunum1=0 --maxampdunum2=957 --fixedPER0=0 --fixedPER1=0 --fixedPER2=0  --seed=5 --scenario=3link",
    # "build/scratch/optimal-test-link3/ns3.43-link3-optimized --nsld0=4 --nsld1=6 --nsld2=2 --simt=21.5 --bawsize=1024 --policy=5 --bw1=40 --bw2=80 --bw3=160 --mcs1=10 --mcs2=10 --mcs3=10 --nss=4 --ampdunumsld0=32 --ampdunumsld1=192 --ampdunumsld2=32 --fixedPER0=0 --fixedPER1=0 --fixedPER2=0  --seed=5 --scenario=3link",
    # "build/scratch/optimal-test-link3/ns3.43-link3-optimized --nsld0=4 --nsld1=6 --nsld2=2 --simt=21.5 --bawsize=1024 --policy=1 --bw1=40 --bw2=80 --bw3=160 --mcs1=10 --mcs2=10 --mcs3=10 --nss=4 --ampdunumsld0=32 --ampdunumsld1=192 --ampdunumsld2=32 --fixedPER0=0 --fixedPER1=0 --fixedPER2=0  --seed=5 --scenario=3link",

    # "build/scratch/optimal-test-link3/ns3.43-link3-optimized --nsld0=6 --nsld1=6 --nsld2=0 --simt=21.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --bw3=160 --mcs1=10 --mcs2=10 --mcs3=10 --nss=4 --ampdunumsld0=512 --ampdunumsld1=512 --ampdunumsld2=0 --maxampdunum0=0 --maxampdunum1=0 --maxampdunum2=1024 --fixedPER0=0 --fixedPER1=0 --fixedPER2=0  --seed=5 --scenario=3link",
    # "build/scratch/optimal-test-link3/ns3.43-link3-optimized --nsld0=6 --nsld1=6 --nsld2=0 --simt=21.5 --bawsize=1024 --policy=5 --bw1=40 --bw2=80 --bw3=160 --mcs1=10 --mcs2=10 --mcs3=10 --nss=4 --ampdunumsld0=512 --ampdunumsld1=512 --ampdunumsld2=0 --fixedPER0=0 --fixedPER1=0 --fixedPER2=0  --seed=5 --scenario=3link",
    # "build/scratch/optimal-test-link3/ns3.43-link3-optimized --nsld0=6 --nsld1=6 --nsld2=0 --simt=21.5 --bawsize=1024 --policy=1 --bw1=40 --bw2=80 --bw3=160 --mcs1=10 --mcs2=10 --mcs3=10 --nss=4 --ampdunumsld0=512 --ampdunumsld1=512 --ampdunumsld2=0 --fixedPER0=0 --fixedPER1=0 --fixedPER2=0  --seed=5 --scenario=3link",


    # # 第11组: bawsize=1024, maxampdunum0=0, maxampdunum1=1024, mcs1=4
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=2 --bw1=40 --bw2=80 --mcs1=4 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=1",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=1 --bw1=40 --bw2=80 --mcs1=4 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --mcs1=4 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=0 --maxampdunum0=0 --maxampdunum1=1024",
    # # 第12组: bawsize=1024, maxampdunum0=0, maxampdunum1=1024, mcs1=5
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=2 --bw1=40 --bw2=80 --mcs1=5 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=1",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=1 --bw1=40 --bw2=80 --mcs1=5 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --mcs1=5 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=0 --maxampdunum0=0 --maxampdunum1=1024",
    # # 第13组: bawsize=1024, maxampdunum0=23, maxampdunum1=1001, mcs1=6
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=2 --bw1=40 --bw2=80 --mcs1=6 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=1",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --mcs1=6 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02 --maxampdunum0=23 --maxampdunum1=1001  --seed=5 --scenario=test-bawstates --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=1 --bw1=40 --bw2=80 --mcs1=6 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --mcs1=6 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=0 --maxampdunum0=0 --maxampdunum1=1024",
    # # 第14组: bawsize=1024, maxampdunum0=46, maxampdunum1=978, mcs1=7
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=2 --bw1=40 --bw2=80 --mcs1=7 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=1",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --mcs1=7 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02 --maxampdunum0=46 --maxampdunum1=978  --seed=5 --scenario=test-bawstates --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=1 --bw1=40 --bw2=80 --mcs1=7 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --mcs1=7 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=0 --maxampdunum0=0 --maxampdunum1=1024",
    # # 第15组: bawsize=1024, maxampdunum0=91, maxampdunum1=933, mcs1=8
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=2 --bw1=40 --bw2=80 --mcs1=8 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=1",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --mcs1=8 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02 --maxampdunum0=91 --maxampdunum1=933  --seed=5 --scenario=test-bawstates --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=1 --bw1=40 --bw2=80 --mcs1=8 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --mcs1=8 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=0 --maxampdunum0=0 --maxampdunum1=1024",
    # # 第16组: bawsize=1024, maxampdunum0=119, maxampdunum1=905, mcs1=9
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=2 --bw1=40 --bw2=80 --mcs1=9 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=1",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --mcs1=9 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02 --maxampdunum0=119 --maxampdunum1=905  --seed=5 --scenario=test-bawstates --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=1 --bw1=40 --bw2=80 --mcs1=9 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --mcs1=9 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=0 --maxampdunum0=0 --maxampdunum1=1024",
    # # 第17组: bawsize=1024, maxampdunum0=151, maxampdunum1=873, mcs1=10
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=2 --bw1=40 --bw2=80 --mcs1=10 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=1",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --mcs1=10 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02 --maxampdunum0=151 --maxampdunum1=873  --seed=5 --scenario=test-bawstates --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=1 --bw1=40 --bw2=80 --mcs1=10 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --mcs1=10 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=0 --maxampdunum0=0 --maxampdunum1=1024",
    # # 第18组: bawsize=1024, maxampdunum0=181, maxampdunum1=843, mcs1=11
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=2 --bw1=40 --bw2=80 --mcs1=11 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=1",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --mcs1=11 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02 --maxampdunum0=181 --maxampdunum1=843  --seed=5 --scenario=test-bawstates --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=1 --bw1=40 --bw2=80 --mcs1=11 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --mcs1=11 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=0 --maxampdunum0=0 --maxampdunum1=1024",
    # # 第19组: bawsize=1024, maxampdunum0=204, maxampdunum1=820, mcs1=12
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=2 --bw1=40 --bw2=80 --mcs1=12 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=1",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --mcs1=12 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02 --maxampdunum0=204 --maxampdunum1=820  --seed=5 --scenario=test-bawstates --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=1 --bw1=40 --bw2=80 --mcs1=12 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=4.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --mcs1=12 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=5 --scenario=test-bawstates --logsender=0 --maxampdunum0=0 --maxampdunum1=1024",
    # # 第20组: bawsize=1024, maxampdunum0=236, maxampdunum1=788, mcs1=13
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=2 --bw1=40 --bw2=80 --mcs1=13 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02 --seed=2 --scenario=test-bawstates --logsender=1",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --mcs1=13 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02 --maxampdunum0=236 --maxampdunum1=788  --seed=5 --scenario=test-bawstates --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=1 --bw1=40 --bw2=80 --mcs1=13 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02 --seed=2 --scenario=test-bawstates --logsender=0",
    # "build/scratch/optimal-test-link2/ns3.43-link2-optimized --nsld0=2 --nsld1=1 --simt=16.5 --bawsize=1024 --policy=6 --bw1=40 --bw2=80 --mcs1=13 --mcs2=10 --nss=2 --ampdunumsld0=128 --ampdunumsld1=64 --fixedPER0=0.08 --fixedPER1=0.02  --seed=2 --scenario=test-bawstates --logsender=0 --maxampdunum0=0 --maxampdunum1=1024",
]

print("开始顺序执行命令...")
print("=" * 60)

# 顺序执行所有命令
for i, cmd in enumerate(commands, 1):
    print(f"\n[{i}/{len(commands)}]")
    run_command(cmd)

print("\n" + "=" * 60)
print("所有命令执行完成！")