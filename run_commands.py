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
        'nsld1': r'--nsld1=(\d+)',
        'nsld2': r'--nsld2=(\d+)', 
        'bawsize': r'--bawsize=(\d+)',
        'bw1': r'--bw1=(\d+)',
        'bw2': r'--bw2=(\d+)',
        'mcs1': r'--mcs1=(\d+)',
        'mcs2': r'--mcs2=(\d+)',
        'nss' : r'--nss=(\d+)',
        'simt': r'--simt=(\d+)',
        'pretitle': r'--pretitle=(\d+)',
        'seed': r'--seed=(\d+)',
        'maxampdunum0': r'--maxampdunum0=(\d+)',
        'maxampdunum1': r'--maxampdunum1=(\d+)',
        'scenario': r'--scenario=([\w-]+)', 
        'SLDinterval': r'--SLDinterval=([\d.]+)',
        'ampdunumsld0': r'--ampdunumsld0=(\d+)',
        'ampdunumsld1': r'--ampdunumsld1=(\d+)'
    }
    
    for key, pattern in patterns.items():
        match = re.search(pattern, cmd)
        if match:
            params[key] = match.group(1)
    
    # 生成文件名
    filename_parts = ["output"]
    
    # 添加关键参数到文件名
    if 'pretitle' in params:
        pretitleint = int(params['pretitle'])
        pretitle_mapping = {
            0: "my",
            1: "greedy", 
            2: "damla",
            3: "only5G",
            4: "only2G",
            5: "sumbawby2g",
            6: "bothset",
        }
        pretitle = pretitle_mapping.get(pretitleint, f"unknown{pretitleint}")
        filename_parts.append(f"{pretitle}")
    if 'bawsize' in params:
        filename_parts.append(f"baw{params['bawsize']}")
    if 'bw1' in params:
        filename_parts.append(f"bw1_{params['bw1']}")
    if 'bw2' in params:
        filename_parts.append(f"bw2_{params['bw2']}")
    if 'mcs1' in params:
        filename_parts.append(f"mcs1_{params['mcs1']}")
    if 'mcs2' in params:
        filename_parts.append(f"mcs2_{params['mcs2']}")
    if 'nss' in params:
        filename_parts.append(f"nss_{params['nss']}")
    if 'nsld1' in params:
        filename_parts.append(f"nsld1_{params['nsld1']}")
    if 'nsld2' in params and int(params['nsld2']) != 0:
        filename_parts.append(f"nsld2_{params['nsld2']}")
    if 'seed' in params and params['seed'] != '1':    
        filename_parts.append(f"seed{params['seed']}")
    if 'maxampdunum0' in params and int(params['maxampdunum0']) != 0:
        filename_parts.append(f"maxampdunum0_{params['maxampdunum0']}")
    if 'maxampdunum1' in params and int(params['maxampdunum1']) != 0:
        filename_parts.append(f"maxampdunum1_{params['maxampdunum1']}")
    if 'SLDinterval' in params and float(params['SLDinterval']) != 1.0:
        filename_parts.append(f"sldinterval_{params['SLDinterval']}")
    if 'ampdunumsld0' in params:
        filename_parts.append(f"ampdunumsld0_{params['ampdunumsld0']}")
    if 'ampdunumsld1' in params:
        filename_parts.append(f"ampdunumsld1_{params['ampdunumsld1']}")
    
    
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
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=2 --bw1=20 --bw2=80 --mcs1=4  --mcs2=10 --nss=4 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=2 --bw1=20 --bw2=80 --mcs1=5  --mcs2=10 --nss=4 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=2 --bw1=20 --bw2=80 --mcs1=6  --mcs2=10 --nss=4 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=2 --bw1=20 --bw2=80 --mcs1=7  --mcs2=10 --nss=4 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=2 --bw1=20 --bw2=80 --mcs1=8  --mcs2=10 --nss=4 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=2 --bw1=20 --bw2=80 --mcs1=9  --mcs2=10 --nss=4 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=2 --bw1=20 --bw2=80 --mcs1=10  --mcs2=10 --nss=4 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=2 --bw1=20 --bw2=80 --mcs1=11  --mcs2=10 --nss=4 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=2 --bw1=20 --bw2=80 --mcs1=12  --mcs2=10 --nss=4 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=2 --bw1=20 --bw2=80 --mcs1=13  --mcs2=10 --nss=4 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",



# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=1 --bw1=20 --bw2=80 --mcs1=4  --mcs2=10 --nss=4 --ampdunumsld0=96 --ampdunumsld1=928 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=2 --bw1=20 --bw2=80 --mcs1=4  --mcs2=10 --nss=4 --ampdunumsld0=96 --ampdunumsld1=928 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=1 --bw1=20 --bw2=80 --mcs1=5  --mcs2=10 --nss=4 --ampdunumsld0=96 --ampdunumsld1=928 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=2 --bw1=20 --bw2=80 --mcs1=5  --mcs2=10 --nss=4 --ampdunumsld0=96 --ampdunumsld1=928 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=1 --bw1=20 --bw2=80 --mcs1=6  --mcs2=10 --nss=4 --ampdunumsld0=96 --ampdunumsld1=928 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=2 --bw1=20 --bw2=80 --mcs1=6  --mcs2=10 --nss=4 --ampdunumsld0=96 --ampdunumsld1=928 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=1 --bw1=20 --bw2=80 --mcs1=7  --mcs2=10 --nss=4 --ampdunumsld0=96 --ampdunumsld1=928 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=2 --bw1=20 --bw2=80 --mcs1=7  --mcs2=10 --nss=4 --ampdunumsld0=96 --ampdunumsld1=928 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=1 --bw1=20 --bw2=80 --mcs1=8  --mcs2=10 --nss=4 --ampdunumsld0=96 --ampdunumsld1=928 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=2 --bw1=20 --bw2=80 --mcs1=8  --mcs2=10 --nss=4 --ampdunumsld0=96 --ampdunumsld1=928 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=1 --bw1=20 --bw2=80 --mcs1=9  --mcs2=10 --nss=4 --ampdunumsld0=96 --ampdunumsld1=928 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=2 --bw1=20 --bw2=80 --mcs1=9  --mcs2=10 --nss=4 --ampdunumsld0=96 --ampdunumsld1=928 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=1 --bw1=20 --bw2=80 --mcs1=10  --mcs2=10 --nss=4 --ampdunumsld0=96 --ampdunumsld1=928 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=2 --bw1=20 --bw2=80 --mcs1=10  --mcs2=10 --nss=4 --ampdunumsld0=96 --ampdunumsld1=928 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=1 --bw1=20 --bw2=80 --mcs1=11  --mcs2=10 --nss=4 --ampdunumsld0=96 --ampdunumsld1=928 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=2 --bw1=20 --bw2=80 --mcs1=11  --mcs2=10 --nss=4 --ampdunumsld0=96 --ampdunumsld1=928 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=1 --bw1=20 --bw2=80 --mcs1=12  --mcs2=10 --nss=4 --ampdunumsld0=96 --ampdunumsld1=928 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=2 --bw1=20 --bw2=80 --mcs1=12  --mcs2=10 --nss=4 --ampdunumsld0=96 --ampdunumsld1=928 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=1 --bw1=20 --bw2=80 --mcs1=13  --mcs2=10 --nss=4 --ampdunumsld0=96 --ampdunumsld1=928 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
# "./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=1024 --pretitle=2 --bw1=20 --bw2=80 --mcs1=13  --mcs2=10 --nss=4 --ampdunumsld0=96 --ampdunumsld1=928 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",

"./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=256 --pretitle=1 --bw1=20 --bw2=80 --mcs1=4  --mcs2=10 --nss=4 --ampdunumsld0=24 --ampdunumsld1=232 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
"./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=256 --pretitle=2 --bw1=20 --bw2=80 --mcs1=4  --mcs2=10 --nss=4 --ampdunumsld0=24 --ampdunumsld1=232 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
"./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=256 --pretitle=1 --bw1=20 --bw2=80 --mcs1=5  --mcs2=10 --nss=4 --ampdunumsld0=24 --ampdunumsld1=232 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
"./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=256 --pretitle=2 --bw1=20 --bw2=80 --mcs1=5  --mcs2=10 --nss=4 --ampdunumsld0=24 --ampdunumsld1=232 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
"./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=256 --pretitle=1 --bw1=20 --bw2=80 --mcs1=6  --mcs2=10 --nss=4 --ampdunumsld0=24 --ampdunumsld1=232 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
"./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=256 --pretitle=2 --bw1=20 --bw2=80 --mcs1=6  --mcs2=10 --nss=4 --ampdunumsld0=24 --ampdunumsld1=232 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
"./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=256 --pretitle=1 --bw1=20 --bw2=80 --mcs1=7  --mcs2=10 --nss=4 --ampdunumsld0=24 --ampdunumsld1=232 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
"./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=256 --pretitle=2 --bw1=20 --bw2=80 --mcs1=7  --mcs2=10 --nss=4 --ampdunumsld0=24 --ampdunumsld1=232 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
"./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=256 --pretitle=1 --bw1=20 --bw2=80 --mcs1=8  --mcs2=10 --nss=4 --ampdunumsld0=24 --ampdunumsld1=232 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
"./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=256 --pretitle=2 --bw1=20 --bw2=80 --mcs1=8  --mcs2=10 --nss=4 --ampdunumsld0=24 --ampdunumsld1=232 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
"./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=256 --pretitle=1 --bw1=20 --bw2=80 --mcs1=9  --mcs2=10 --nss=4 --ampdunumsld0=24 --ampdunumsld1=232 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
"./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=256 --pretitle=2 --bw1=20 --bw2=80 --mcs1=9  --mcs2=10 --nss=4 --ampdunumsld0=24 --ampdunumsld1=232 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
"./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=256 --pretitle=1 --bw1=20 --bw2=80 --mcs1=10  --mcs2=10 --nss=4 --ampdunumsld0=24 --ampdunumsld1=232 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
"./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=256 --pretitle=2 --bw1=20 --bw2=80 --mcs1=10  --mcs2=10 --nss=4 --ampdunumsld0=24 --ampdunumsld1=232 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
"./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=256 --pretitle=1 --bw1=20 --bw2=80 --mcs1=11  --mcs2=10 --nss=4 --ampdunumsld0=24 --ampdunumsld1=232 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
"./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=256 --pretitle=2 --bw1=20 --bw2=80 --mcs1=11  --mcs2=10 --nss=4 --ampdunumsld0=24 --ampdunumsld1=232 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
"./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=256 --pretitle=1 --bw1=20 --bw2=80 --mcs1=12  --mcs2=10 --nss=4 --ampdunumsld0=24 --ampdunumsld1=232 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
"./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=256 --pretitle=2 --bw1=20 --bw2=80 --mcs1=12  --mcs2=10 --nss=4 --ampdunumsld0=24 --ampdunumsld1=232 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
"./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=256 --pretitle=1 --bw1=20 --bw2=80 --mcs1=13  --mcs2=10 --nss=4 --ampdunumsld0=24 --ampdunumsld1=232 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",
"./ns3 run mode-test-udp-1vN-dl --  --nsld1=3 --nsld2=1 --simt=6.5 --bawsize=256 --pretitle=2 --bw1=20 --bw2=80 --mcs1=13  --mcs2=10 --nss=4 --ampdunumsld0=24 --ampdunumsld1=232 --maxampdunum0=0 --maxampdunum1=0 --seed=7 --scenario=changer1r2-new",

]

print("开始顺序执行命令...")
print("=" * 60)

# 顺序执行所有命令
for i, cmd in enumerate(commands, 1):
    print(f"\n[{i}/{len(commands)}]")
    run_command(cmd)

print("\n" + "=" * 60)
print("所有命令执行完成！")

# # 显示生成的日志文件
# print("\n生成的日志文件:")
# for folder in os.listdir('.'):
#     if os.path.isdir(folder) and not folder.startswith('.'):
#         log_files = [f for f in os.listdir(folder) if f.endswith('.log')]
#         for log_file in sorted(log_files):
#             print(f"  - {folder}/{log_file}")