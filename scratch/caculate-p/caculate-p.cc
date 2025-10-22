#include <cmath>
#include <iostream>
#include <ostream>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <limits>
#include <iomanip>
#include <fstream>
#include <sstream>

double compute_pA(int M, const std::vector<int>& n, const std::vector<double>& W, int K, double epsilon = 1e-6, int max_iterations = 1000) {
    auto f = [&](double p) {
        double sum = 0.0;
        for (int g = 0; g < M; g++) {
            double term1 = p / (2 * p - 1);
            double term2 = (1 - term1) * std::pow(2 - 2 * p, K);
            double denominator = W[g] * (term1 + term2);
            sum += (-2 * n[g]) / denominator;
        }
        double exp_value = std::exp(sum);
        return p - exp_value;
    };

    double a = 0.5 + epsilon;
    double b = 1.0 - epsilon;
    double fa = f(a);
    double fb = f(b);

    if (fa * fb > 0) {
        throw std::runtime_error("No root found in the interval (0.5, 1) or multiple roots may exist");
    }

    for (int iter = 0; iter < max_iterations; iter++) {
        double mid = (a + b) / 2.0;
        double fmid = f(mid);
        if (std::abs(fmid) < epsilon) {
            return mid;
        }
        if (fa * fmid < 0) {
            b = mid;
            fb = fmid;
        } else {
            a = mid;
            fa = fmid;
        }
    }

    throw std::runtime_error("Failed to converge within max iterations");
}

// 定义方程 f(p) = 0
double equation(double p, double W, int K, double n) {
    if (p < 0 || p > 1) return NAN;
    
    double term1 = 4 * p / (W * (1 + p));
    double term2 = (2 * (1 - p) / (W * (1 + p))) * std::pow((1 - p) / 2, K);
    
    double inside = 1 - term1 - term2;
    
    // 检查是否在合理范围内
    if (inside <= 0) return p - 0;  // 使 f(p) 在区间端点符号一致，便于二分法
    if (inside >= 1) return p - 1;
    
    double right_side = std::pow(inside, n);
    return p - right_side;
}

// 二分法求解
double compute_pA_another(double W, int K, double n, double tol = 1e-8, int max_iter = 1000) {
    double low = 0.0;
    double high = 1.0;
    
    double f_low = equation(low, W, K, n);
    double f_high = equation(high, W, K, n);
    
    if (std::abs(f_low) < tol) return low;
    if (std::abs(f_high) < tol) return high;
    
    if (f_low * f_high > 0) {
        throw std::runtime_error("方程在 [0,1] 上可能无解或有多解，请检查参数。");
    }
    
    for (int iter = 0; iter < max_iter; ++iter) {
        double mid = (low + high) / 2;
        double f_mid = equation(mid, W, K, n);
        
        if (std::abs(f_mid) < tol || (high - low) / 2 < tol) {
            return mid;
        }
        
        if (f_mid * f_low > 0) {
            low = mid;
            f_low = f_mid;
        } else {
            high = mid;
        }
    }
    
    throw std::runtime_error("二分法未在最大迭代次数内收敛");
}

double compute_lambda_out(double pA, int M, 
                         const std::vector<int>& N,
                         const std::vector<double>& W,
                         const std::vector<double>& tau_T,
                         double tau_F, int K, int g) {
    if(N[1]){
        double term1 = pA / (2 * pA - 1);
        double term2 = (1 - term1) * std::pow(2 - 2 * pA, K);
        
        if (std::isnan(term1) || std::isnan(term2)) {
            throw std::runtime_error("NaN detected in term1 or term2");
        }
        
        double denom1 = term1 + term2;
        double sum1 = 0.0;
        double sum2 = 0.0;
        
        for (int i = 0; i < M; i++) {
            sum1 += N[i] * tau_T[i] / W[i];
            sum2 += N[i] / W[i];
        }
        
        double ratio = sum1 / sum2;
        double term3 = (ratio - tau_F) * pA * std::log(pA);
        double denom2 = 1 + tau_F - tau_F * pA - term3;

        double numerator = 2 * pA * tau_T[g] / W[g];

        double lambda_out = numerator / (denom1 * denom2);
        
        if (lambda_out <= 0) {
            throw std::runtime_error("Invalid lambda_out: " + std::to_string(lambda_out));
        }
        
        return lambda_out;
    }else{
        return 1;
    }

}

double get_throughput(const std::vector<double>& tau_T,
                                 std::vector<double> W,
                                 double PL,
                                 double sigma, 
                                 double lambda_outs) {
    double throughput; // 没干扰的时候考虑BO？
    if(lambda_outs == 1){
        throughput = PL / (tau_T[0] + (W[0] - 1) / 2) / sigma;
    }else{
        throughput = lambda_outs * PL / tau_T[0] / sigma;
    }
    
    if (throughput <= 0) {
        throw std::runtime_error("Invalid throughput: " + std::to_string(throughput));
    }
    
    return throughput;
}

int main() {
    std::vector<double> R = {344.118, 1441.176}; // {2.4G,5G}Mbps
    std::vector<double> R_sld = {143.382, 600.490};
    // std::vector<double> R = {688.235, 1441.176}; // {2.4G,5G}Mbps
    // std::vector<double> R_sld = {286.764, 600.490};     
    // std::vector<double> R = {344.118, 2882.353}; // {2.4G,5G}Mbps    
    // std::vector<double> R_sld = {143.382, 1200.980};     
    // std::vector<double> R = {688.235, 2882.353}; // {2.4G,5G}Mbps    
    // std::vector<double> R_sld = {286.764, 1200.980};    
    
    double sigma = 9.0; // us
    double L_subf_sld = 1572.0 * 8.0;  // 干扰设备SLD
    // groupsize = 1
    double L_subf_mld = 1572.0 * 8.0; 
    double L_P = 1500.0 * 8.0; 

    std::vector<double> T_PH = {56.0, 44.0}; // {mld, sld}
    std::vector<double> T_RTS = {34,28};
    std::vector<double> T_CTS = {34, 28};
    std::vector<double> T_SIFS = {10, 16};
    const double maxPpduDuration = 5484; // us 与datarate无关
    
    // 2.4G
    std::vector<int> N1 = {1, 0}; // {mlds == 1 ,slds}
    std::vector<double> W1 = {16, 16};   // initial backoff window size
    int K1 = 6;  // cutoff phase

    // 5G
    std::vector<int> N2 = {1, 0};
    std::vector<double> W2 = {16, 16};
    int K2 = 6;

    int32_t nmpdu_sld = 8;

    std::vector<double> T_DIFS;
    for (double sifs : T_SIFS) {
        T_DIFS.push_back(sifs + 2 * sigma);
    }

    std::vector<int32_t> nmpdu_mld_max;
    for (size_t i = 0; i < R.size(); ++i) {
        double value = (maxPpduDuration - T_PH[0]) * R[i] / L_subf_mld;
        nmpdu_mld_max.push_back(static_cast<int32_t>(std::floor(value)));
    }

    std::vector<double> tau_F; 
    for (size_t i = 0; i < R.size(); ++i) {
        double value = (T_RTS[i] + T_DIFS[i]) / sigma;  
        tau_F.push_back(value);
    }
    
    double pA1, pA2;
    pA1 = compute_pA(2, N1, W1, K1);
    pA2 = compute_pA(2, N2, W2, K2);

    // 创建CSV文件
    std::string title;
    title = "results_RTS_gs_1";
    title += "_sld1_" + std::to_string(N1[1]) + "_sld2_" + std::to_string(N2[1]) 
    + "_r1_" + std::to_string(R[0]) + "_r2_" + std::to_string(R[1])
    + "_W1_" + std::to_string(W1[0]) + "_" + std::to_string(W1[1])
    + "_W2_" + std::to_string(W2[0]) + "_" + std::to_string(W2[1]) + ".csv";
    
    std::ofstream csv_file(title);
    csv_file << "BAW,Best_nmpdu_mld_0,Best_nmpdu_mld_1,Max_nmpdu_mld_0,Max_nmpdu_mld_1,D1,D2,D1+D2,T1,T2,lambda_out1,lambda_out2,D_slo,lambda_out_slo" << std::endl;

    // 循环BAW从64到1024，步长为2
    for (int BAW = 64; BAW <= 1024; BAW += 2) {
        // 输出当前BAW值（作为进度指示）
        std::cout << "Processing BAW = " << BAW << std::endl;

        double T_BA;    //?
        if (BAW <= 256) T_BA = 46.0;
        else if (BAW <= 512) T_BA = 58.0;  
        else T_BA = 78.0;  

        std::vector<double> T_OH_mld;
        for (size_t i = 0; i < R.size(); ++i) {
            double value = (T_PH[0] + T_SIFS[i] * 3 + T_BA + T_DIFS[i] + T_RTS[i] + T_CTS[i]) / sigma;
            T_OH_mld.push_back(value);
        }

        std::vector<double> T_OH_sld;
        for (size_t i = 0; i < R.size(); ++i) {
            double value = (T_PH[1] + T_SIFS[i] * 3 + T_BA + T_DIFS[i] + T_RTS[i] + T_CTS[i]) / sigma;
            T_OH_sld.push_back(value);
        }

        double min_diff = std::numeric_limits<double>::max();
        std::vector<int32_t> best_nmpdu_mld(2, 0);
        double best_T1 = 0.0, best_T2 = 0.0;
        double best_D1 = 0.0, best_D2 = 0.0;
        double best_lambda_out1 = 0.0, best_lambda_out2 = 0.0;
        int valid_count = 0;

        // 确保 nmpdu_mld_max 值合理
        int32_t max_n0 = std::min(BAW - 1, nmpdu_mld_max[0]);
        if (max_n0 < 1) {
            // 如果nmpdu_mld_max[0]太小，跳过这个BAW
            continue;
        }

        for (int32_t n0 = 1; n0 <= max_n0; ++n0) {
            int32_t n1_val = BAW - n0;
            if (n1_val < 1 || n1_val > nmpdu_mld_max[1]) {
                continue;
            }

            std::vector<int32_t> nmpdu_mld = {n0, n1_val};
            std::vector<double> tau_T1(2);
            tau_T1[0] = nmpdu_mld[0] * L_subf_mld / R[0] / sigma + T_OH_mld[0];  
            tau_T1[1] = nmpdu_sld * L_subf_sld / R_sld[0] / sigma + T_OH_sld[0];  
            std::vector<double> tau_T2(2);
            tau_T2[0] = nmpdu_mld[1] * L_subf_mld / R[1] / sigma + T_OH_mld[1];  
            tau_T2[1] = nmpdu_sld * L_subf_sld / R_sld[1] / sigma + T_OH_sld[1];  

            try {
                double lambda_out1 = compute_lambda_out(pA1, 2, N1, W1, tau_T1, tau_F[0], K1, 0);
                double lambda_out2 = compute_lambda_out(pA2, 2, N2, W2, tau_T2, tau_F[1], K2, 0);
                double D1 = get_throughput(tau_T1, W1, L_P * nmpdu_mld[0], sigma, lambda_out1);
                double D2 = get_throughput(tau_T2, W2, L_P * nmpdu_mld[1], sigma, lambda_out2);

                // 检查吞吐量是否有效
                if (D1 <= 0 || D2 <= 0 || std::isnan(D1) || std::isnan(D2)) {
                    continue;
                }

                // double T1 = nmpdu_mld[0] * L_subf_mld / D1 / sigma + T_OH[0];
                // double T2 = nmpdu_mld[1] * L_subf_mld / D2 / sigma + T_OH[1];
                double T1 = tau_T1[0]/lambda_out1;
                double T2 = tau_T2[0]/lambda_out2;

                // 检查 T1 和 T2 是否有效
                if (std::isnan(T1) || std::isnan(T2) || std::isinf(T1) || std::isinf(T2)) {
                    continue;
                }

                double diff = std::abs(T1 - T2);
                if (diff < min_diff) {
                    min_diff = diff;
                    best_nmpdu_mld = nmpdu_mld;
                    best_T1 = T1;
                    best_T2 = T2;
                    best_D1 = D1;
                    best_D2 = D2;
                    best_lambda_out1 = lambda_out1;
                    best_lambda_out2 = lambda_out2;
                    valid_count++;
                }
            } catch (const std::exception& e) {
                // 忽略异常，继续下一个组合
                continue;
            }
        }

        if (valid_count == 0) {
            // 如果没有找到有效组合，跳过这个BAW
            continue;
        }


        std::vector<double> tau_T1_slo(2);
        std::vector<int> nmpdu_mld_slo(2);
        nmpdu_mld_slo[0] = std::min(nmpdu_mld_max[0], BAW);
        nmpdu_mld_slo[1] = std::min(nmpdu_mld_max[1], BAW);
        tau_T1_slo[0] = nmpdu_mld_slo[0] * L_subf_mld / R[0] / sigma + T_OH_mld[0];  
        tau_T1_slo[1] = nmpdu_sld * L_subf_sld / R_sld[0] / sigma + T_OH_sld[0];  
        std::vector<double> tau_T2_slo(2);
        tau_T2_slo[0] = nmpdu_mld_slo[1] * L_subf_mld / R[1] / sigma + T_OH_mld[1];  
        tau_T2_slo[1] = nmpdu_sld * L_subf_sld / R_sld[1] / sigma + T_OH_sld[1];  
        double lambda_out1_slo = compute_lambda_out(pA1, 2, N1, W1, tau_T1_slo, tau_F[0], K1, 0);
        double lambda_out2_slo = compute_lambda_out(pA2, 2, N2, W2, tau_T2_slo, tau_F[1], K2, 0);
        double D1_slo = get_throughput(tau_T1_slo, W1, L_P * nmpdu_mld_slo[0], sigma, lambda_out1_slo);
        double D2_slo = get_throughput(tau_T2_slo, W2, L_P * nmpdu_mld_slo[1], sigma, lambda_out2_slo);


        // 将结果写入CSV文件
        csv_file << BAW << ","
                 << best_nmpdu_mld[0] << ","
                 << best_nmpdu_mld[1] << ","
                 << nmpdu_mld_max[0] << ","
                 << nmpdu_mld_max[1] << ","
                 << best_D1 << ","
                 << best_D2 << ","
                 << best_D1 + best_D2 << ","
                 << best_T1 << ","
                 << best_T2 << ","
                 << best_lambda_out1 << ","
                 << best_lambda_out2 << ","
                 << std::max(D1_slo, D2_slo) << ","
                 << std::max(lambda_out1_slo, lambda_out2_slo) <<std::endl;
    }

    csv_file.close();
    std::cout << "计算完成，结果已保存到 " << title << std::endl;

    return 0;
}