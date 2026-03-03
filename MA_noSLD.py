import numpy as np
import math
import csv

def compute_MA(alpha, beta, W_BA):
    alpha = np.asarray(alpha, dtype=float)
    beta = np.asarray(beta, dtype=float)
    L = len(alpha)

    assert len(beta) == L, "alpha and beta must have the same length"
    assert W_BA >= L, "W_BA must be >= number of links"

    T_bar_star = (W_BA + np.sum(beta / alpha)) / np.sum(1.0 / alpha)
    M_tilde = (T_bar_star - beta) / alpha
    M_floor = np.floor(M_tilde).astype(int)
    Delta = W_BA - np.sum(M_floor)
    if Delta < 0:
        raise ValueError("Infeasible allocation: Delta < 0")
    fractional = M_tilde - M_floor
    idx = np.argsort(-fractional)  # descending order

    M_A = M_floor.copy()
    M_A[idx[:Delta]] += 1

    return M_A, T_bar_star

def compute_Dmax(M_A, alpha, T_OH, W, L_P, sigma):
    """计算D_max和相关参数"""
    M_A = np.asarray(M_A, dtype=float)
    alpha = np.asarray(alpha, dtype=float)
    W = np.asarray(W, dtype=float)
    
    tau_T = alpha * M_A / sigma + T_OH
    lambda_out = tau_T / ((W - 1) / 2 + tau_T)
    D_per_link = lambda_out * (M_A * L_P) / (tau_T * sigma)
    D_max = np.sum(D_per_link)
    
    return D_max, D_per_link, tau_T, lambda_out

if __name__ == "__main__":
    # 固定参数
    BAW = 256
    R1_fixed = 2000  # 固定R[1]
    sigma = 9.0
    L_P = 1500.0 * 8.0
    T_PH = 56.0
    T_RTS = [30, 24]
    T_CTS = [34, 28]
    T_SIFS = [10, 16]
    L_subf_mld = 1572.0 * 8.0
    W = [16, 4]
    samebeta = False
    
    ratio_start = 0.025   # R[1]/R[0]的最小比值
    ratio_end = 1.0   # R[1]/R[0]的最大比值
    ratio_step = 0.025    # 比值步长
    
    print("R[0]/R[1]比值扫描：")
    print(f"R[1]固定为: {R1_fixed} Mbps")
    print(f"R[0]/R[1]比值: {ratio_start} - {ratio_end}，步长: {ratio_step}")
    print("=" * 100)
    
    # 创建结果列表
    results = []
    
    # 扫描R[0]的值
    ratio_values = np.arange(ratio_start, ratio_end + ratio_step, ratio_step)
    
    for ratio in ratio_values:
        # 根据比值计算R[0]
        R0 = R1_fixed * ratio
        R = [R0, R1_fixed]
        
        # 计算T_DIFS
        T_DIFS = [sifs + 2 * sigma for sifs in T_SIFS]
        
        # 计算T_BA
        T_BA = [0, 0]
        if BAW <= 256:
            T_BA[0] = 46.0
        elif BAW <= 512:
            T_BA[0] = 58.0
        else:
            T_BA[0] = 78.0
        T_BA[1] = T_BA[0] - 6.0
        
        # 计算T_OH_mld
        T_OH_mld = []
        for i in range(len(R)):
            value = (T_PH + T_SIFS[i] * 3 + T_BA[i] + T_DIFS[i] + T_RTS[i] + T_CTS[i]) / sigma
            T_OH_mld.append(value)
        
        # 计算alpha和beta
        alpha = []
        beta = []
        for i in range(len(R)):
            value1 = L_subf_mld / R[i]
            value2 = ((W[i] - 1)/2 + T_OH_mld[i]) * sigma
            alpha.append(value1)
            beta.append(value2)
        
        if samebeta:
            beta[1] = beta[0]
        
        # 计算最优M_A
        M_A, T_star = compute_MA(alpha, beta, BAW)
        
        # 计算D_max
        M_A = np.asarray(M_A, dtype=float)
        alpha = np.asarray(alpha, dtype=float)
        beta = np.asarray(beta, dtype=float)
        W = np.asarray(W, dtype=float)
        tau_T = alpha * M_A / sigma + T_OH_mld
        lambda_out = tau_T / ((W - 1) / 2 + tau_T)
        D_per_link = lambda_out * (M_A * L_P) / (tau_T * sigma)
        D_max = np.sum(D_per_link)
        
        # 计算单链路全BAW分配的情况
        tau_T_slo = alpha * BAW / sigma + T_OH_mld
        lambda_out_slo = tau_T_slo / ((W - 1) / 2 + tau_T_slo)
        D_per_link_slo = lambda_out_slo * (BAW * L_P) / (tau_T_slo * sigma)
        
        # 存储结果
        result = {
            'R0': R[0],
            'R1': R[1],
            'R_ratio': R[1] / R[0],
            'M_A': M_A.copy(),
            'M_A_ratio': M_A[1] / M_A[0],
            'M_A_sum': np.sum(M_A),
            'D_per_link': D_per_link.copy(),
            'D_max': D_max,
            'D_per_link_slo': D_per_link_slo.copy(),
            'alpha': alpha.copy(),
            'beta': beta.copy(),
        }
        results.append(result)
        
    filename = f"results_ratio_{ratio_start}_{ratio_end}_step{ratio_step}_diffOH.csv"
    with open(filename, 'w', newline='') as csvfile:
        fieldnames = ['R0', 'R1', 'R_ratio', 'M_A0', 'M_A1', 'M_A_ratio', 'M_A_sum', 'D0', 'D1', 'D_max', 'D_slo','alpha0', 'alpha1', 'beta0', 'beta1']
        writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
        writer.writeheader()
        for result in results:
            writer.writerow({
                'R0': result['R0'],
                'R1': result['R1'],
                'R_ratio': result['R_ratio'],
                'M_A0': result['M_A'][0],
                'M_A1': result['M_A'][1],
                'M_A_ratio': result['M_A_ratio'],
                'M_A_sum': result['M_A_sum'],
                'D0': result['D_per_link'][0],
                'D1': result['D_per_link'][1],
                'D_max': result['D_max'],
                'D_slo': result['D_per_link_slo'][1],
                'alpha0': result['alpha'][0],
                'alpha1': result['alpha'][1],
                'beta0': result['beta'][0],
                'beta1': result['beta'][1]
            })
    print(f"结果已保存到 {filename}")