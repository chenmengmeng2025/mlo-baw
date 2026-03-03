#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import math
from typing import Tuple, List

class Config:
    """配置参数类"""
    def __init__(self):
        self.aifsn = 2
        self.sigma = 9.0  # 时隙时间 (us)
        self.L_subf = 1572.0 * 8.0  # 子帧长度
        self.L_subf_single = 1570.0 * 8.0  # 单包子帧长度
        self.L_P = 1500 * 8.0  # 包长度
        self.T_PH = 72.0
        self.T_RTS = [30, 24]
        self.T_CTS = [34, 28]
        self.T_SIFS = [10, 16]
        self.maxPpduDuration = 5484  # us
        self.K = 6  # cutoff phase
        self.W = [16, 16]  # 初始退避窗口


def compute_q(i: int, W0: float, K: int) -> float:
    """计算 q_i"""
    if i > K:
        i = K
    return 2.0 / (1.0 + W0 * (2.0 ** i))


def compute_alpha(pM: float, pS: float, tau_F: float, tau_T_M: float, 
                  tau_T_S: float, N: int) -> float:
    """计算 alpha"""
    pM_pow = pM ** (1.0 / N)
    denom = (1.0 + tau_F + (tau_T_M - tau_F) * pM + N * (tau_T_S - tau_F) * pS
             - (tau_T_M + N * (tau_T_S - tau_F)) * pS * pM_pow)
    return 1.0 / denom


def denom_sum(p: float, W0: float, K: int) -> float:
    """计算分母求和项"""
    s = 0.0
    for i in range(K):
        s += ((1 - p) ** i) * p / compute_q(i, W0, K)
    s += ((1 - p) ** K) / compute_q(K, W0, K)
    return s


def solve_p(N: int, K: int, W: List[float], max_iter: int = 2000, 
            tol: float = 1e-12, damping: float = 0.4) -> Tuple[float, float]:
    """固定点迭代求解 pM 和 pS"""
    if N <= 0:
        return 0.0, 0.0
    
    eps = 1e-12
    pM = 0.5
    pS = 0.5
    
    for _ in range(max_iter):
        D_S = denom_sum(pS, W[1], K)
        pM_new = eps if D_S <= 1.0 + 1e-15 else (1.0 - 1.0 / D_S) ** N
        pM_new = max(eps, min(pM_new, 1.0 - eps))
        
        D_M = denom_sum(pM_new, W[0], K)
        pS_candidate = eps if D_M <= 0 else (1.0 - 1.0 / D_M) * ((1.0 - 1.0 / D_S) ** (N - 1))
        pS_candidate = max(eps, min(pS_candidate, 1.0 - eps))
        pS_new = (1.0 - damping) * pS + damping * pS_candidate
        
        if abs(pM_new - pM) < tol and abs(pS_new - pS) < tol:
            return pM_new, pS_new
        
        pM = pM_new
        pS = pS_new
    
    return pM, pS


def compute_lambda(n: int, pM: float, pS: float, tau_T: List[float], 
                   tau_F: float, W: List[float], K: int = 6) -> Tuple[float, float, float]:
    """计算 lambda"""
    if n == 0:
        return 0, tau_T[0] / ((W[0] - 1) / 2 + tau_T[0]), 1.0
    
    alpha = compute_alpha(pM, pS, tau_F, tau_T[0], tau_T[1], n)
    lambdaS = n * (1 - pM ** (1.0 / n)) * alpha * pS * tau_T[1]
    lambdaM = (1 - pS * (pM ** (1.0 / n - 1))) * alpha * pM * tau_T[0]
    
    return lambdaS, lambdaM, alpha


def calc_payload_duration(nmpdu: float, L_subf: float, rate: float, 
                         is_link0: bool) -> float:
    if nmpdu < 1e-9:
        return 0.0
    return (nmpdu * L_subf) / rate

def calc_tau_T(nmpdu: float, L_subf: float, rate: float, sigma: float, 
               T_OH: float, T_PH: float, is_link0: bool) -> float:
    """计算传输时间 (tau_T)"""
    payloadDuration = calc_payload_duration(nmpdu, L_subf, rate, is_link0)
    ppduDuration = payloadDuration + T_PH
    return ppduDuration / sigma + T_OH


def calc_T_BA(nmpdu: float, BAW: int, is_link0: bool) -> float:
    """计算BA时间"""
    if nmpdu < 1.5:
        return 34.0 if is_link0 else 28.0
    
    if BAW <= 256:
        T_BA = 46.0
    elif BAW <= 512:
        T_BA = 58.0
    else:
        T_BA = 78.0
    
    return T_BA if is_link0 else T_BA - 6.0


def get_throughput(tau_T: List[float], PL: float, sigma: float, 
                   lambda_val: float, nmpdu: float) -> float:
    """计算吞吐量"""
    if nmpdu < 1e-9:
        return 0.0
    
    throughput = lambda_val * PL / tau_T[0] / sigma
    
    if throughput < 0:
        raise ValueError(f"Invalid throughput: {throughput}")
    
    return throughput


def calculate_throughput(N1: int, N2: int, nmpdu_mld0: int, nmpdu_mld1: int,
                        nmpdu_sld0: int, nmpdu_sld1: int, BAW: int,
                        R1: float, R2: float, cfg: Config):
    """主计算函数"""
    
    # print("\n========== 输入参数 ==========")
    # print(f"N1 = {N1}, N2 = {N2}")
    # print(f"nmpdu_mld0 = {nmpdu_mld0}, nmpdu_mld1 = {nmpdu_mld1}")
    # print(f"nmpdu_sld0 = {nmpdu_sld0}, nmpdu_sld1 = {nmpdu_sld1}")
    # print(f"BAW = {BAW}")
    # print(f"R1 = {R1} Mbps, R2 = {R2} Mbps")
    # print("================================")
    
    # 求解碰撞概率
    pM1, pS1 = solve_p(N1, cfg.K, cfg.W)
    pM2, pS2 = solve_p(N2, cfg.K, cfg.W)
    
    # 计算tau_F
    tau_F = [
        (cfg.T_RTS[i] + cfg.T_SIFS[i] + cfg.aifsn * cfg.sigma) / cfg.sigma
        for i in range(2)
    ]
    
    # 计算开销时间
    T_DIFS = [cfg.T_SIFS[i] + cfg.aifsn * cfg.sigma for i in range(2)]
    
    # 设置子帧长度
    L_subf_mld0 = cfg.L_subf_single if nmpdu_mld0 == 1 else cfg.L_subf
    L_subf_mld1 = cfg.L_subf_single if nmpdu_mld1 == 1 else cfg.L_subf
    L_subf_sld0 = cfg.L_subf_single if nmpdu_sld0 == 1 else cfg.L_subf
    L_subf_sld1 = cfg.L_subf_single if nmpdu_sld1 == 1 else cfg.L_subf
    
    # 计算BA时间
    T_BA = [
        calc_T_BA(nmpdu_mld0, BAW, True),
        calc_T_BA(nmpdu_mld1, BAW, False)
    ]
    T_BA_sld = [
        calc_T_BA(nmpdu_sld0, BAW, True),
        calc_T_BA(nmpdu_sld1, BAW, False)
    ]
    
    # 计算开销
    T_OH_mld = [
        (cfg.T_SIFS[i] * 3 + T_BA[i] + T_DIFS[i] + cfg.T_RTS[i] + cfg.T_CTS[i]) / cfg.sigma
        for i in range(2)
    ]
    T_OH_sld = [
        (cfg.T_SIFS[i] * 3 + T_BA_sld[i] + T_DIFS[i] + cfg.T_RTS[i] + cfg.T_CTS[i]) / cfg.sigma
        for i in range(2)
    ]
    
    # 计算传输时间
    R = [R1, R2]
    tau_T1 = [
        calc_tau_T(nmpdu_mld0, L_subf_mld0, R[0], cfg.sigma, T_OH_mld[0], cfg.T_PH, True),
        calc_tau_T(nmpdu_sld0, L_subf_sld0, R[0], cfg.sigma, T_OH_sld[0], cfg.T_PH, True)
    ]
    tau_T2 = [
        calc_tau_T(nmpdu_mld1, L_subf_mld1, R[1], cfg.sigma, T_OH_mld[1], cfg.T_PH, False),
        calc_tau_T(nmpdu_sld1, L_subf_sld1, R[1], cfg.sigma, T_OH_sld[1], cfg.T_PH, False)
    ]
    
    # 计算lambda
    lambdaS1, lambdaM1, alpha1 = compute_lambda(N1, pM1, pS1, tau_T1, tau_F[0], cfg.W, cfg.K)
    lambdaS2, lambdaM2, alpha2 = compute_lambda(N2, pM2, pS2, tau_T2, tau_F[1], cfg.W, cfg.K)
    
    # 计算吞吐量
    D1 = get_throughput(tau_T1, cfg.L_P * nmpdu_mld0, cfg.sigma, lambdaM1, nmpdu_mld0)
    D2 = get_throughput(tau_T2, cfg.L_P * nmpdu_mld1, cfg.sigma, lambdaM2, nmpdu_mld1)
    
    # 计算传输周期
    T1 = tau_T1[0] / lambdaM1
    T2 = tau_T2[0] / lambdaM2
    
    # 输出结果
    print("\n========== 计算结果 ==========")
    # print("碰撞概率:")
    # print(f"  pM1 = {pM1:.6f}, pS1 = {pS1:.6f}")
    # print(f"  pM2 = {pM2:.6f}, pS2 = {pS2:.6f}")
    # print("\nAlpha值:")
    # print(f"  alpha1 = {alpha1:.6f}, alpha2 = {alpha2:.6f}")
    # print("\nLambda值:")
    # print(f"  lambdaM1 = {lambdaM1:.6f}, lambdaM2 = {lambdaM2:.6f}")
    # print("\n传输时间 tau_T (slots):")
    # print(f"  Link1 MLD: {tau_T1[0]:.6f}, SLD: {tau_T1[1]:.6f}")
    # print(f"  Link2 MLD: {tau_T2[0]:.6f}, SLD: {tau_T2[1]:.6f}")
    # print("\n传输周期 T (slots):")
    # print(f"  T1 = {T1:.6f}, T2 = {T2:.6f}")
    # print(f"  |T1 - T2| = {abs(T1 - T2):.6f}")
    # print("\n吞吐量 (Mbps):")
    print(f"  D1 = {D1:.6f} Mbps")
    print(f"  D2 = {D2:.6f} Mbps")
    print(f"  D_total = {D1 + D2:.6f} Mbps")
    print("================================\n")
    
    return {
        'pM1': pM1, 'pS1': pS1, 'pM2': pM2, 'pS2': pS2,
        'alpha1': alpha1, 'alpha2': alpha2,
        'lambdaM1': lambdaM1, 'lambdaM2': lambdaM2,
        'tau_T1': tau_T1, 'tau_T2': tau_T2,
        'T1': T1, 'T2': T2,
        'D1': D1, 'D2': D2, 'D_total': D1 + D2
    }


def main():
    """主函数"""
    cfg = Config()
    N1 = 0
    N2 = 1
    nmpdu_mld0 = 0
    nmpdu_mld1 = 1024
    BAW = 1024
    R1 = 516.176472
    R2 = 2161.764708
    
    # 只扫描特定的 nmpdu_sld0 值
    test_values = [96]  # 自定义要测试的值
    
    for nmpdu_sld0 in test_values:
        # nmpdu_sld1 = BAW - nmpdu_sld0
        nmpdu_sld1 = 928
        print(f"\n测试 nmpdu_sld0={nmpdu_sld0:3d}, nmpdu_sld1={nmpdu_sld1:3d}")
        calculate_throughput(N1, N2, nmpdu_mld0, nmpdu_mld1,
                            nmpdu_sld0, nmpdu_sld1, BAW, R1, R2, cfg)
    
    return 0

if __name__ == "__main__":
    exit(main())
