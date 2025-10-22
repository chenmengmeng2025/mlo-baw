#include <ostream>
#include <string>
#include <vector>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <iostream>
#include <numeric>
#include <cmath>
#include <fstream>  
#include <iomanip>  
#include "ns3/command-line.h"
#include <filesystem>

// T_RTS\ T_CTS不随Datarate变  
std::vector<double> T_RTS = {34,28};
std::vector<double> T_CTS = {34, 28};

// 计算 T_OH,i 和 T_BO,i
std::tuple<std::vector<double>, std::vector<double>> calculateOverheadAndBackoff(
    const std::vector<double>& T_SIFS,
    const std::vector<double>& T_BA,
    const std::vector<double>& T_DIFS,
    const std::vector<int>& CW_min,
    double T_PH,
    double sigma) {
    
    int M = CW_min.size();
    if (T_SIFS.size() != M || T_BA.size() != M || T_DIFS.size() != M) {
        throw std::invalid_argument("All input vectors must have the same size");
    }

    std::vector<double> T_OH(M);
    std::vector<double> T_BO(M);

    for (int i = 0; i < M; ++i) {
        T_OH[i] = (T_PH + T_SIFS[i] * 3 + T_BA[i] + T_DIFS[i] + T_RTS[i] + T_CTS[i]) / sigma;
        T_BO[i] = (CW_min[i] - 1) / 2.0;
    }

    return {T_OH, T_BO};
}

// 计算 TudpBest，并返回选择的结果项信息
std::tuple<double, std::string, bool>calculateTudpBest(
    int W,
    int L_subf,  // 使用 L_subf 替代 L_P + L_M
    const std::vector<int>& CW_min,
    const std::vector<double>& R,
    double sigma,
    const std::vector<int>& n_max,
    double T_PH,
    const std::vector<double>& T_SIFS,
    const std::vector<double>& T_BA,
    const std::vector<double>& T_DIFS) {
    
    auto [T_OH, T_BO] = calculateOverheadAndBackoff(T_SIFS, T_BA, T_DIFS, CW_min, T_PH, sigma);

    int M = CW_min.size();
    if (R.size() != M) {
        throw std::invalid_argument("R vector must have the same size as CW_min");
    }

    // 计算第一个项：平均时间项
    double numerator = W * L_subf;  // 使用 L_subf
    double denominator = 0.0;
    std::string terms;

    
    for (int i = 0; i < M; ++i) {
        numerator += (T_OH[i] + T_BO[i]) * R[i] * sigma;
        denominator += R[i] * sigma;
    }
    double first_term = numerator / denominator;
    terms += "first_term: " + std::to_string(first_term) + "; ";


    // 计算第二个项：最小节点时间项
    double second_term = std::numeric_limits<double>::max();
    terms += "second_term:";
    for (int i = 0; i < M; ++i) {
        double current_term = T_OH[i] + T_BO[i] + (n_max[i] * L_subf) / (R[i] * sigma);
        terms += " " + std::to_string(current_term);
        if (current_term < second_term) {
            second_term = current_term;
        }
    }
    // 确定选择了哪个项
    double TudpBest;

    if (first_term < second_term) {
        TudpBest = first_term;
    } else {
        TudpBest = second_term;
    }

    return {TudpBest, terms , TudpBest == first_term};
}

// 计算 n_i,best 
std::vector<int> calculateN_iBest(
    double TudpBest,
    const std::vector<double>& T_OH,
    const std::vector<double>& T_BO,
    const std::vector<double>& R,
    double sigma,
    int L_subf,
    const std::vector<int>& n_max,
    int W,
    bool sumW) { // 新增参数 W 和 n_max
    
    int M = T_OH.size();
    std::vector<int> n_iBest(M);
    double total = 0.0;
    
    // 1. 计算初始值（四舍五入）
    for (int i = 0; i < M; ++i) {
        double temp = ((TudpBest - T_OH[i] - T_BO[i]) * R[i] * sigma) / L_subf;
        n_iBest[i] = std::min(static_cast<int>(std::round(temp)), n_max[i]); // 不超过 n_max
        total += n_iBest[i];
    }
    
    // // 2. 动态调整总和至 W
    if(sumW){
        int remaining = W - total;
        while (remaining > 0) {
            std::cout<<"adjust to sum W"<<std::endl;
            bool adjusted = false;
            // 按速率 R[i] 降序分配（优先分配高速节点）
            std::vector<size_t> indices(M);
            std::iota(indices.begin(), indices.end(), 0);
            std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
                return R[a] > R[b];
            });
            
            for (int idx : indices) {
                if (n_iBest[idx] < n_max[idx]) {
                    n_iBest[idx]++;
                    remaining--;
                    adjusted = true;
                    if (remaining == 0) break;
                }
            }
            if (!adjusted) break; // 所有节点已达上限
        }

    }

    return n_iBest;
}

// 计算 D_udp,max^mlo
double calculateD_udpMaxMlo(
    int L_P,
    double TudpBest,
    double sigma,
    const std::vector<int>& n_iBest) {
    
    double sum = 0.0;
    for (double n : n_iBest) {
        sum += n;
    }
    
    return (L_P / (TudpBest * sigma)) * sum;
}

// 计算 D_{udp,i}^{slo} 和 D_{udp,max}^{slo}
std::tuple<std::vector<double>, double> calculateD_udpSlo(
    int W,
    int L_P,
    int L_subf,
    const std::vector<double>& R,
    double sigma,
    const std::vector<double>& T_OH,
    const std::vector<double>& T_BO,
    const std::vector<int>& n_max) {
    
    int M = R.size();
    std::vector<double> D_udp_i_slo(M);
    
    for (int i = 0; i < M; ++i) {
        int n_slo_i = std::min(W, n_max[i]);
        double numerator = n_slo_i * L_P;
        double denominator = (n_slo_i * L_subf / R[i]) + (T_OH[i] + T_BO[i]) * sigma;
        D_udp_i_slo[i] = numerator / denominator;
    }
    
    double D_udp_max_slo = *std::max_element(D_udp_i_slo.begin(), D_udp_i_slo.end());
    
    return {D_udp_i_slo, D_udp_max_slo};
}

int main(int argc, char* argv[]) {
    // 常量参数（保持不变）
    // const int L_subf = 6240 * 8;  // bit for groupsize = 4
    // const int L_P = 1500 * 8 * 4;// for groupsize = 4
    const int L_subf = 1572 * 8;  // bit for groupsize = 1
    const int L_P = 1500 * 8;// for groupsize = 1
    const std::vector<int> CW_min = {16, 16};
    const std::vector<double> R = {688.235, 1441.176}; //Mbps
    const double sigma = 9; // us   与datarate无关
    const double T_PH = 56; // us   与datarate无关
    const std::vector<double> T_SIFS = {10, 16}; //us 与datarate无关
    const double maxPpduDuration = 5484; // us 与datarate无关

    // 计算固定值（与W无关）
    std::vector<int> n_max;
    for (size_t i = 0; i < R.size(); ++i) {
        double value = (maxPpduDuration - T_PH) * R[i] / L_subf;
        n_max.push_back(static_cast<int>(std::floor(value)));
    }
    
    std::vector<double> T_DIFS;
    for (double sifs : T_SIFS) {
        T_DIFS.push_back(sifs + 2 * sigma);
    }

    // 创建并初始化CSV文件
    std::string title;
    title = "results_RTS_gs1";
    title += "_cwmin_" + std::to_string(CW_min[0]) + "_r1_" + std::to_string(R[0]) + "_r2_" + std::to_string(R[1]);
    std::ofstream outFile(title + ".csv");
    outFile << "W,TudpBest,isFirstTerm,D_udp_max_slo,D_udp_max_mlo";
    for (size_t i = 0; i < n_max.size(); ++i) {
        outFile << ",n_iBest_" << i+1;
    }
    outFile << "\n";

    // 主循环：W从64到1024（步长64）
    for (int W = 64; W <= 1024; W += 2) {
        // 根据W值确定T_BA  与datarate无关
        std::vector<double> T_BA;
        if (W <= 256) {
            T_BA = {46, 40};  // W ≤ 256
        } else if (W <= 512) {
            T_BA = {58, 52};  // 256 < W ≤ 512
        } else {
            T_BA = {78, 72};  // W > 512
        }

        try {
            // 计算核心指标
            auto [TudpBest, terms, isFirstTerm] = calculateTudpBest(
                W, L_subf, CW_min, R, sigma, n_max, T_PH, T_SIFS, T_BA, T_DIFS);
            
            auto [T_OH, T_BO] = calculateOverheadAndBackoff(
                T_SIFS, T_BA, T_DIFS, CW_min, T_PH, sigma);
            
            auto n_iBest = calculateN_iBest(
                TudpBest, T_OH, T_BO, R, sigma, L_subf, n_max, W, isFirstTerm);
            
            double D_udpMaxMlo = calculateD_udpMaxMlo(L_P, TudpBest, sigma, n_iBest);
            
            auto [_, D_udp_max_slo] = calculateD_udpSlo(
                W, L_P, L_subf, R, sigma, T_OH, T_BO, n_max);

            // 写入CSV行
            outFile << W << ","
                    << std::fixed << std::setprecision(2) << TudpBest << ","
                    << isFirstTerm << ","
                    << D_udp_max_slo << ","
                    << D_udpMaxMlo;
            
            for (int n : n_iBest) {
                outFile << "," << n;
            }
            outFile << "\n";

            // // 控制台输出
            // std::cout << "TudpBest: " << TudpBest 
            //           << " | D_udp_max_slo: " << D_udp_max_slo
            //           << " | D_udpMaxMlo: " << D_udpMaxMlo
            //           << " | n_iBest: [";
            // for (size_t i = 0; i < n_iBest.size(); ++i) {
            //     std::cout << n_iBest[i] << (i < n_iBest.size()-1 ? ", " : "");
            // }
            // std::cout << "]\n";

        } catch (const std::exception& e) {
            std::cerr << "W=" << W << " Error: " << e.what() << std::endl;
            outFile << W << ",ERROR,ERROR,ERROR,ERROR";
            for (size_t i = 0; i < n_max.size(); ++i) outFile << ",ERROR";
            outFile << "\n";
        }
    }
    outFile.close();
    return 0;
}