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

static constexpr double EPS = 1e-12;

// 计算 q_i^{(g)} = 2 / (1 + W * 2^i)
double q_i(double W, int i) {
    return 2.0 / (1 + W * pow(2.0, i));

}

// 计算 D^{(g)}(p)
double D_of(double p, double W, int K) {
    double one_minus_p = 1.0 - p;
    double pow_term = 1.0; // (1-p)^i
    double s = 0.0;
    for (int i = 0; i < K; i++) {
        double qi = q_i(W, i);
        s += pow_term * p / qi;
        pow_term *= one_minus_p;
    }
    double qK = q_i(W, K);
    s += pow_term / qK;
    return s;
}

// F(pM) = pM - exp( -n / D^S(pS) ) ，其中 pS = pM * (1 - 1/D^M(pM))
double F_of_pM(double pM, int n, double WM, double WS, int K) {
    double Dm = D_of(pM, WM, K);
    if (Dm <= 1.0) return 1.0; // 避免非法
    double pS = pM * (1.0 - 1.0 / Dm);
    if (pS <= 0.0) pS = 1e-15;
    if (pS >= 1.0) pS = 1.0 - 1e-15;
    double Ds = D_of(pS, WS, K);
    if (Ds <= 0.0) return 1.0;
    double rhs = std::exp(-double(n) / Ds);
    return pM - rhs;
}

// 主函数：求解 pM, pS
std::pair<double,double> solve_pM_pS(int n, const std::vector<double>& W, int K) {
    if(!n)  return {0,0};
    double WM = W[0], WS = W[1];

    // 二分法求解 pM
    double lo = 1e-12, hi = 1.0 - 1e-12;
    double flo = F_of_pM(lo, n, WM, WS, K);
    double fhi = F_of_pM(hi, n, WM, WS, K);
    if (flo * fhi > 0) {
        throw std::runtime_error("无法找到有效的符号变化区间，请检查参数");
    }

    double mid = 0.0, fmid = 0.0;
    for (int iter = 0; iter < 200; iter++) {
        mid = 0.5 * (lo + hi);
        fmid = F_of_pM(mid, n, WM, WS, K);
        if (std::fabs(fmid) < 1e-12) break;
        if (flo * fmid <= 0) {
            hi = mid; fhi = fmid;
        } else {
            lo = mid; flo = fmid;
        }
    }

    double pM = mid;
    double Dm = D_of(pM, WM, K);
    double pS = pM * (1.0 - 1.0 / Dm);

    return {pM, pS};
}

std::pair<double,double> compute_lambda(int n, double pM, double pS,
                                        const std::vector<double>& tau_T, double tau_F, std::vector<double> W) {
    if(!n) {
        return {0, tau_T[0]/(( W[0]-1 ) / 2 + tau_T[0])};
    }

    double tauT_M = tau_T[0];
    double tauT_S = tau_T[1];

    // α
    double denom = 1.0 + tau_F 
                 + (tauT_M - tau_F) * pM 
                 - tauT_M * pS 
                 - (tauT_S - tau_F) * pS * std::log(pM);
    if (denom == 0.0) throw std::runtime_error("Denominator for alpha is zero!");
    double alpha = 1.0 / denom;

    // λ_out^(S), λ_out^(M)
    double lambdaS = -tauT_S * pS * std::log(pM) * alpha;
    double lambdaM = tauT_M * (pM - pS) * alpha;

        // ---- 调试打印 ----
    std::cout << "[DEBUG] compute_lambda n=" << n
              << " pM=" << pM 
              << " pS=" << pS
              << " tauT_M=" << tauT_M
              << " tauT_S=" << tauT_S
              << " alpha=" << alpha
              << " lambdaS=" << lambdaS 
              << " lambdaM=" << lambdaM
              << std::endl;

    return {lambdaS, lambdaM};
}

double get_throughput(const std::vector<double>& tau_T,
                                 std::vector<double> W,
                                 double PL,
                                 double sigma, 
                                 double lambda) {
    double throughput = lambda * PL / tau_T[0] / sigma;
    if (throughput < 0) {
        throw std::runtime_error("Invalid throughput: " + std::to_string(throughput));
    }
    return throughput;
}

int main() {
    // std::vector<double> R = {344.118, 1441.176}; // {2.4G,5G}Mbps
    // std::vector<double> R_sld = {143.382, 600.490};
    // std::vector<double> R = {688.235, 1441.176}; // {2.4G,5G}Mbps
    // std::vector<double> R_sld = {286.764, 600.490};     
    std::vector<double> R = {344.118, 2882.353}; // {2.4G,5G}Mbps    
    std::vector<double> R_sld = {143.382, 1200.980};     
    // std::vector<double> R = {688.235, 2882.353}; // {2.4G,5G}Mbps    
    // std::vector<double> R_sld = {286.764, 1200.980};    
    
    R_sld = R;
    double sigma = 9.0; // us
    double L_subf_sld = 1570.0 * 8.0;  // 干扰设备SLD
    double L_subf_mld = 1572.0 * 8.0; 
    double L_P = 1500.0 * 8.0; 

    std::vector<double> T_PH = {56.0, 56.0}; // {mld, sld}
    std::vector<double> T_RTS = {34, 28}; // different with link
    std::vector<double> T_CTS = {34, 28}; // different with link
    std::vector<double> T_SIFS = {10, 16}; 
    const double maxPpduDuration = 5484; // us 与datarate无关
    
    // 2.4G
    int N1 = 10; // {mlds == 1 ,slds}
    std::vector<double> W1 = {16, 16};   // initial backoff window size
    int K1 = 6;  // cutoff phase

    // 5G
    int N2 = 10; // {mlds == 1 ,slds}
    std::vector<double> W2 = {16, 16};
    int K2 = 6;

    int32_t nmpdu_sld = 1;

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
    
    std::pair<double, double> result1, result2;
    if (N1) {
        result1 = solve_pM_pS(N1, W1, K1);
    } else {
        result1 = {0, 0};
    }
    auto [pM1, pS1] = result1;

    if (N2) {
        result2 = solve_pM_pS(N2, W2, K2);
    } else {
        result2 = {0, 0};
    }
    auto [pM2, pS2] = result2;

    // 创建CSV文件
    std::string title;
    title = "results";
    title += "_sld1_" + std::to_string(N1) + "_sld2_" + std::to_string(N2) 
    + "_r1_" + std::to_string(R[0]) + "_r2_" + std::to_string(R[1]) + "_1vN_dl.csv";
    
    std::ofstream csv_file(title);
    csv_file << "BAW,Best_nmpdu_mld_0,Best_nmpdu_mld_1,Max_nmpdu_mld_0,Max_nmpdu_mld_1,D1,D2,D1+D2,T1,T2,lambda_out1,lambda_out2,D_slo,lambda_out_slo" << std::endl;

    for (int BAW = 1024; BAW <= 1024; BAW += 2) {
        std::cout << "Processing BAW = " << BAW << std::endl;

        double T_BA; 
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
        int32_t max_n0 = std::min(BAW, nmpdu_mld_max[0]);
        int32_t max_n1 = std::min(BAW, nmpdu_mld_max[1]);

        if(max_n0 + max_n1 >= BAW){
            for (int32_t n0 = 0; n0 <= max_n0; ++n0) {
                int32_t n1_val = BAW - n0;
                if (n1_val < 0 || n1_val > max_n1) {
                    continue;
                }
                std::vector<int32_t> nmpdu_mld = {n0, n1_val};
                std::vector<double> tau_T1(2);
                if (nmpdu_mld[0] == 0) {
                    tau_T1[0] = 0.0;
                } else {
                    tau_T1[0] = nmpdu_mld[0] * L_subf_mld / R[0] / sigma + T_OH_mld[0];  
                }
                // tau_T1[0] = nmpdu_mld[0] * L_subf_mld / R[0] / sigma + T_OH_mld[0];  
                tau_T1[1] = nmpdu_sld * L_subf_sld / R_sld[0] / sigma + T_OH_sld[0];  
                std::vector<double> tau_T2(2);
                if(nmpdu_mld[1] == 0) {
                    tau_T2[0] = 0.0;
                } else {
                    tau_T2[0] = nmpdu_mld[1] * L_subf_mld / R[1] / sigma + T_OH_mld[1];  
                }
                // tau_T2[0] = nmpdu_mld[1] * L_subf_mld / R[1] / sigma + T_OH_mld[1];  
                tau_T2[1] = nmpdu_sld * L_subf_sld / R_sld[1] / sigma + T_OH_sld[1];  

                try {
                    auto [lambdaS1, lambdaM1] = compute_lambda(N1, pM1, pS1, tau_T1, tau_F[0], W1);
                    auto [lambdaS2, lambdaM2] = compute_lambda(N2, pM2, pS2, tau_T2, tau_F[1], W2);
                    double D1 = get_throughput(tau_T1, W1, L_P * nmpdu_mld[0], sigma, lambdaM1);
                    double D2 = get_throughput(tau_T2, W2, L_P * nmpdu_mld[1], sigma, lambdaM2);

                    // 检查吞吐量是否有效
                    if (D1 < 0 || D2 < 0 || std::isnan(D1) || std::isnan(D2)) {
                        continue;
                    }

                    double T1 = tau_T1[0]/lambdaM1;
                    double T2 = tau_T2[0]/lambdaM2;
                    // 检查 T1 和 T2 是否有效
                    if (std::isnan(T1) || std::isnan(T2) || std::isinf(T1) || std::isinf(T2)) {
                        continue;
                    }

                    double diff = std::abs(T1 - T2);
                    std::cout << "nmpdu_mld: [" << nmpdu_mld[0] << ", " << nmpdu_mld[1] << "], "
                        << "T: [" << T1 << ", " << T2 << "], "
                        << "diff: " << diff
                        << std::endl;
                    if (diff < min_diff) {
                        min_diff = diff;
                        best_nmpdu_mld = nmpdu_mld;
                        best_T1 = T1;
                        best_T2 = T2;
                        best_D1 = D1;
                        best_D2 = D2;
                        best_lambda_out1 = lambdaM1;
                        best_lambda_out2 = lambdaM2;
                        valid_count++;
                    }
                } catch (const std::exception& e) {
                    continue;
                }
            }
        }else{
            
        }

        if (valid_count == 0) {
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
        double lambda_out1_slo = compute_lambda(N1, pM1, pS1, tau_T1_slo, tau_F[0], W1).second;
        double lambda_out2_slo = compute_lambda(N2, pM2, pS2, tau_T2_slo, tau_F[1], W2).second;
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
                 << ((std::max(D1_slo, D2_slo) == D1_slo) ? lambda_out1_slo : lambda_out2_slo) << std::endl;
    }

    csv_file.close();
    std::cout << "计算完成，结果已保存到 " << title << std::endl;

    return 0;
}