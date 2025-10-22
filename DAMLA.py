import math
import pandas as pd

def calculate_y2(r1, r2, t1, t2, W):
    numerator = (r1**2 * r2 * t1) + (W - r1 * t2) * (r1 * r2 + r2**2)
    denominator = r1**2 + r1 * r2 + r2**2
    
    if abs(denominator) < 1e-10:
        return 0.0
    
    return numerator / denominator

def calculate_T1_to_2_s(r1, r2, t1, t2, W):
    numerator = W * r2 + t2 * (r1**2 + r2**2) - t1 * r2**2
    denominator = r1**2 + r1 * r2 + r2**2
    
    if abs(denominator) < 1e-10:
        return 0.0
    
    return numerator / denominator

def calculate_T1_to_2_s(r1, r2, t1, t2, W):
    """
    计算第二个公式: T_{1→2}^s = [W * r2 + t2 * (r1^2 + r2^2) - t1 * r2^2] / (r1^2 + r1*r2 + r2^2)
    """
    numerator = W * r2 + t2 * (r1**2 + r2**2) - t1 * r2**2
    denominator = r1**2 + r1 * r2 + r2**2
    
    if abs(denominator) < 1e-10:
        return 0.0
    
    return numerator / denominator

if __name__ == "__main__":
    # 初始化参数
    # R = [344.118, 2882.353]
    R = [344.118, 1441.176]
    # R = [688.235, 1441.176]
    CW_min = [16, 16]
    L_subf = 1572 * 8
    L_P = 1500 * 8
    # L_subf = 6240 * 8;  # groupsize = 4
    # L_P = 1500 * 8 * 4; #groupsize = 4
    # L_subf = 3136 * 8;  # groupsize = 2
    # L_P = 1500 * 8 * 2; #groupsize = 2
    sigma = 9
    T_PH = 56
    T_SIFS = [10, 16]
    T_RTS = [34, 28]
    T_CTS = [34, 28]
    maxPpduDuration = 5484
    
    n_max = [int(math.floor((maxPpduDuration - T_PH) * r / L_subf)) for r in R]
    T_DIFS = [sifs + 2 * sigma for sifs in T_SIFS]
    
    results = []
    
    for W in range(64, 1025, 64):
        # 根据W值确定T_BA
        if W <= 256:
            T_BA = [46, 40]
        elif W <= 512:
            T_BA = [58, 52]
        else:
            T_BA = [78, 72]

        # 计算T_OH和T_BO
        T_OH = [
            (T_PH + T_SIFS[i] * 3 + T_BA[i] + T_DIFS[i] + T_RTS[i] + T_CTS[i])
            for i in [0, 1]
        ]

        T_BO = [
            (CW_min[i] - 1) / 2.0 * sigma
            for i in [0, 1]  
        ]
        
        # 计算y2和T1_to_2_s
        y1 = calculate_y2(R[1] / L_subf, R[0] / L_subf, T_OH[1] + T_BO[1], T_OH[0] + T_BO[0], W)
        y2 = calculate_y2(R[0] / L_subf, R[1] / L_subf, T_OH[0] + T_BO[0], T_OH[1] + T_BO[1], W)
        T1_to_2_s = calculate_T1_to_2_s(R[0] / L_subf, R[1] / L_subf, T_OH[0] + T_BO[0], T_OH[1] + T_BO[1], W)
        
        # 将结果添加到列表中
        results.append({
            'W': W,
            'y1': round(y1),
            'y2': round(y2),
            'T1_to_2_s': T1_to_2_s
        })
    
    df = pd.DataFrame(results)
    title = f"results_RTS_gs_1_DAMLA_cwmin_{CW_min[0]}_r1_{R[0]}_r2_{R[1]}"

    df.to_csv(title, index=False)
    print("结果已保存到文件中")

