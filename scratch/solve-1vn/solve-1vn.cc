#include <bits/stdc++.h>
using namespace std;

// 扫描配置结构体
struct ScanConfig {
    vector<int> N1_values = {0};
    vector<int> N2_values = {0};
    // vector<int> nmpdu_sld0_values = {32,64,96,128,160,192,224,256,288,320,352,384,416,448,480,512,
    //                              544,576,608,640,672,704,736,768,800,832,864,896,
    //                              928,960,992};
    // vector<int> nmpdu_sld0_values = {8,16,24,32,40,48,56,64,72,80,88,96,104,112,120,128,
    //                              136,144,152,160,168,176,184,192,200,208,216,224,
    //                              232,240,248};
    vector<int> nmpdu_sld0_values = {0};
    vector<int> nmpdu_sld1_values = {0};
    vector<int> BAW_values = {256,1024};
    vector<double> R1_values = {206.470592,275.29412,309.705884,344.117648,412.94118, 458.823532,516.176472,573.529412,619.411768, 688.235296};
    // vector<double> R1_values = {};
    vector<double> R2_values = {2161.764708};
};

// 结果记录结构体
struct Result {
    vector<double> R;
    int N1, N2, BAW;
    vector<int32_t> nmpdu_sld;
    vector<int32_t> best_nmpdu;
    vector<int32_t> nmpdu_mld_max;
    double D_mlo;
    double D1, D2;
    double T1, T2;
    double p1, p2;
    double alpha1, alpha2;
    double lambda1, lambda2;
    double D_slo, lambda_slo;
    vector<double> continuous_nmpdu;  // 连续最优解
    double continuous_D_mlo;
    double continuous_D1, continuous_D2;
    double continuous_T1, continuous_T2;
};

// 配置结构体
struct Config {
    int aifsn = 2;
    double sigma = 9.0;            // 时隙时间 (us)
    double L_subf = 1572.0 * 8.0;  // 子帧长度
    double L_subf_single = 1570.0 * 8.0;  // 单包子帧长度
    double L_P = 1500 * 8.0;       // 包长度
    double T_PH = 72.0;
    vector<double> T_RTS = {30, 24};
    vector<double> T_CTS = {34, 28};
    vector<double> T_SIFS = {10, 16};
    double maxPpduDuration = 5484; // us
    int K = 6;                     // cutoff phase
    vector<double> W = {16, 16};   // 初始退避窗口
};

// 计算 q_i
inline double compute_q(int i, double W0, int K) {
    if (i > K) i = K;
    return 2.0 / (1.0 + W0 * pow(2.0, i));
}

// 计算 alpha
double compute_alpha(double pM, double pS, double tau_F, double tau_T_M, double tau_T_S, int N) {
    double pM_pow = pow(pM, 1.0 / N);
    double denom = 1.0 + tau_F + (tau_T_M - tau_F) * pM + N * (tau_T_S - tau_F) * pS
                   - (tau_T_M + N * (tau_T_S - tau_F)) * pS * pM_pow;
    return 1.0 / denom;
}

// 计算分母求和项
double denom_sum(double p, double W0, int K) {
    double s = 0.0;
    for (int i = 0; i < K; ++i) {
        s += pow(1 - p, i) * p / compute_q(i, W0, K);
    }
    s += pow(1 - p, K) / compute_q(K, W0, K);
    return s;
}

// 固定点迭代求解 pM 和 pS
pair<double, double> solve_p(int N, int K, const vector<double>& W, 
                             int max_iter = 2000, double tol = 1e-12, double damping = 0.4) {
    if (N <= 0) return {0.0, 0.0};
    
    const double eps = 1e-12;
    double pM = 0.5, pS = 0.5;

    for (int iter = 0; iter < max_iter; ++iter) {
        double D_S = denom_sum(pS, W[1], K);
        double pM_new = (D_S <= 1.0 + 1e-15) ? eps : pow(1.0 - 1.0 / D_S, N);
        pM_new = clamp(pM_new, eps, 1.0 - eps);

        double D_M = denom_sum(pM_new, W[0], K);
        double pS_candidate = (D_M <= 0) ? eps : (1.0 - 1.0 / D_M) * pow(1.0 - 1.0 / D_S, N - 1);
        pS_candidate = clamp(pS_candidate, eps, 1.0 - eps);
        double pS_new = (1.0 - damping) * pS + damping * pS_candidate;

        if (abs(pM_new - pM) < tol && abs(pS_new - pS) < tol) {
            return {pM_new, pS_new};
        }
        pM = pM_new;
        pS = pS_new;
    }
    return {pM, pS};
}

// 计算 lambda
tuple<double, double, double> compute_lambda(int n, double pM, double pS,
                                             const vector<double>& tau_T, double tau_F, 
                                             const vector<double>& W, int K = 6) {
    if (n == 0) return {0, tau_T[0] / ((W[0] - 1) / 2 + tau_T[0]), 1.0};

    double alpha = compute_alpha(pM, pS, tau_F, tau_T[0], tau_T[1], n);
    double lambdaS = n * (1 - pow(pM, 1.0 / n)) * alpha * pS * tau_T[1];
    double lambdaM = (1 - pS * pow(pM, 1.0 / n - 1)) * alpha * pM * tau_T[0];
    
    return {lambdaS, lambdaM, alpha};
}

// 计算吞吐量
double get_throughput(const vector<double>& tau_T, double PL, double sigma, double lambda, double nmpdu) {
    if(nmpdu < 1e-9) return 0.0;
    double throughput = lambda * PL / tau_T[0] / sigma;
    if (throughput < 0) throw runtime_error("Invalid throughput: " + to_string(throughput));
    return throughput;
}

// 计算payload持续时间
// double calc_payload_duration(double nmpdu, double L_subf, double rate, bool is_link0) {
//     if (nmpdu < 1e-9) return 0.0;
//     if (is_link0) {
//         return floor(ceil((16 + nmpdu * L_subf + 6) / (rate * 13.6)) * 13.6 + 6);
//     } else {
//         std::cout << "calc_payload_duration: nmpdu=" << nmpdu << ", L_subf=" << L_subf << ", rate=" << rate << ", payloadDuration =" << floor(ceil((16 + nmpdu * L_subf + 6) / (rate * 13.6)) * 13600) / 1000 << std::endl;
//         return floor(ceil((16 + nmpdu * L_subf + 6) / (rate * 13.6)) * 13600) / 1000;
//     }
// }

// // 计算payload持续时间
// double calc_payload_duration(double nmpdu, double L_subf, double rate, bool is_link0) {
//     if (nmpdu < 1e-9) return 0.0;
    
//     // 固定参数
//     const uint8_t stbc = 1;              // 对应 STBC = 1 (非STBC模式)
//     const uint8_t service = 16;          // service bits
//     const uint8_t nes = 1;               // Number of BCC encoders
//     const double symbolDuration = 13600; // 纳秒
//     const double dataRate = rate * 1e6;  // 转换为 bps (bits per second)
//     const double signalExtension = is_link0 ? 6 : 0; // 纳秒
    
//     // 计算每个符号的数据位数
//     double numDataBitsPerSymbol = dataRate * symbolDuration / 1e9;
    
//     double numSymbols = 0;
    
//     if (nmpdu == 1) {
//         // NORMAL_MPDU / SINGLE_MPDU
//         double totalBits = service + 1570 * 8 + 6.0 * nes;
//         numSymbols = lrint(stbc * ceil(totalBits / (stbc * numDataBitsPerSymbol)));
//     } else {
//         // A-MPDU 聚合情况
//         // FIRST_MPDU_IN_AGGREGATE 的贡献
//         double firstSymbols = (stbc * (service + L_subf + 6 * nes) / (stbc * numDataBitsPerSymbol));       
//         // MIDDLE_MPDU_IN_AGGREGATE 的贡献 (nmpdu - 2 个中间帧)
//         double middleSymbols = 0;
//         if (nmpdu > 2) {
//             middleSymbols = (nmpdu - 2) * (stbc * L_subf) / (stbc * numDataBitsPerSymbol);
//         } 
//         // LAST_MPDU_IN_AGGREGATE 的计算
//         double totalSymbolsIncludingLast = lrint(stbc * ceil((service + (nmpdu - 1) * L_subf + 1570 * 8 + 6 * nes) / (stbc * numDataBitsPerSymbol)));
//         double lastSymbols = totalSymbolsIncludingLast - firstSymbols - middleSymbols;
//         numSymbols = firstSymbols + middleSymbols + lastSymbols;
//     }
    
//     // 计算 payload duration（纳秒）
//     double payloadDuration = numSymbols * symbolDuration;
//     payloadDuration += signalExtension;
//     return payloadDuration/1000.0; // 转换为微秒
// }

double calc_payload_duration(double nmpdu, double L_subf, double rate, bool is_link0) {
    if (nmpdu < 1e-9) return 0.0;
    return (nmpdu * L_subf) / rate;
    // return floor(ceil((16 + nmpdu * L_subf + 6) / (rate * 13.6)) * 13.6);
}

// 计算传输时间 (tau_T)
double calc_tau_T(double nmpdu, double L_subf, double rate, double sigma, double T_OH, double T_PH, bool is_link0) {
    double payloadDuration = calc_payload_duration(nmpdu, L_subf, rate, is_link0);
    double ppduDuration = payloadDuration + T_PH;
    return ppduDuration / sigma + T_OH;
}

// 计算BA时间
double calc_T_BA(double nmpdu, int BAW, bool is_link0) {
    if (nmpdu < 1.5) {  // 近似为1
        return is_link0 ? 34.0 : 28.0;
    }
    double T_BA = (BAW <= 256) ? 46.0 : (BAW <= 512) ? 58.0 : 78.0;
    return is_link0 ? T_BA : T_BA - 6.0;
}

// 新增：计算目标函数（用于连续优化）
// 目标：最小化 |T1 - T2|，同时最大化 D_mlo
struct OptimizationParams {
    const Config* cfg;
    int N1, N2, BAW;
    vector<int32_t> nmpdu_sld;
    pair<double, double> p1, p2;
    vector<double> tau_F;
    vector<double> R;
};

// // 计算给定 n0 时的性能指标
// tuple<double, double, double, double, double, double> evaluate_continuous(
//     double n0, const OptimizationParams& params) {
    
//     double n1 = params.BAW - n0;
//     if (n0 < 0 || n1 < 0) return {1e9, 0, 0, 0, 0, 0};
    
//     auto [pM1, pS1] = params.p1;
//     auto [pM2, pS2] = params.p2;
    
//     // 对于连续值，使用固定的子帧长度
//     double L_subf_mld0 = params.cfg->L_subf;
//     double L_subf_mld1 = params.cfg->L_subf;
//     double L_subf_sld0 = (params.nmpdu_sld[0] == 1) ? params.cfg->L_subf_single : params.cfg->L_subf;
//     double L_subf_sld1 = (params.nmpdu_sld[1] == 1) ? params.cfg->L_subf_single : params.cfg->L_subf;
    
//     vector<double> T_DIFS(2);
//     for (int i = 0; i < 2; ++i) 
//         T_DIFS[i] = params.cfg->T_SIFS[i] + params.cfg->aifsn * params.cfg->sigma;
    
//     vector<double> T_BA = {calc_T_BA(n0, params.BAW, true), calc_T_BA(n1, params.BAW, false)};
//     vector<double> T_BA_sld = {calc_T_BA(params.nmpdu_sld[0], params.BAW, true), 
//                                calc_T_BA(params.nmpdu_sld[1], params.BAW, false)};
    
//     vector<double> T_OH_mld(2), T_OH_sld(2);
//     for (int i = 0; i < 2; ++i) {
//         T_OH_mld[i] = (params.cfg->T_SIFS[i] * 3 + T_BA[i] + T_DIFS[i] + 
//                        params.cfg->T_RTS[i] + params.cfg->T_CTS[i]) / params.cfg->sigma;
//         T_OH_sld[i] = (params.cfg->T_SIFS[i] * 3 + T_BA_sld[i] + T_DIFS[i] + 
//                        params.cfg->T_RTS[i] + params.cfg->T_CTS[i]) / params.cfg->sigma;
//     }
    
//     vector<double> tau_T1(2), tau_T2(2);
//     tau_T1[0] = calc_tau_T(n0, L_subf_mld0, params.R[0], params.cfg->sigma, T_OH_mld[0], params.cfg->T_PH, true);
//     tau_T1[1] = calc_tau_T(params.nmpdu_sld[0], L_subf_sld0, params.R[0], params.cfg->sigma, T_OH_sld[0], params.cfg->T_PH, true);
//     tau_T2[0] = calc_tau_T(n1, L_subf_mld1, params.R[1], params.cfg->sigma, T_OH_mld[1], params.cfg->T_PH, false);
//     tau_T2[1] = calc_tau_T(params.nmpdu_sld[1], L_subf_sld1, params.R[1], params.cfg->sigma, T_OH_sld[1], params.cfg->T_PH, false);
    
//     try {
//         auto [lambdaS1, lambdaM1, alpha1] = compute_lambda(params.N1, pM1, pS1, tau_T1, params.tau_F[0], params.cfg->W, params.cfg->K);
//         auto [lambdaS2, lambdaM2, alpha2] = compute_lambda(params.N2, pM2, pS2, tau_T2, params.tau_F[1], params.cfg->W, params.cfg->K);
        
//         double D1 = get_throughput(tau_T1, params.cfg->L_P * n0, params.cfg->sigma, lambdaM1, n0);
//         double D2 = get_throughput(tau_T2, params.cfg->L_P * n1, params.cfg->sigma, lambdaM2, n1);
        
//         if (D1 < 0 || D2 < 0 || isnan(D1) || isnan(D2)) return {1e9, 0, 0, 0, 0, 0};
        
//         double T1 = tau_T1[0] / lambdaM1;
//         double T2 = tau_T2[0] / lambdaM2;
        
//         if (isnan(T1) || isnan(T2) || isinf(T1) || isinf(T2)) return {1e9, 0, 0, 0, 0, 0};
        
//         double diff = abs(T1 - T2);
//         return {diff, D1, D2, T1, T2, D1 + D2};
//     } catch (const exception& e) {
//         return {1e9, 0, 0, 0, 0, 0};
//     }
// }

// 找到连续最优解（使用局部精细遍历搜索）
// 需要在整数最优解附近进行搜索，所以需要传入整数最优解
// pair<double, tuple<double, double, double, double, double, double>> 
// find_continuous_optimum(const OptimizationParams& params, int integer_best_n0) {
//     double best_n0 = integer_best_n0;
//     double best_diff = 1e9;
//     tuple<double, double, double, double, double, double> best_result;
    
//     // 在整数最优解 ±1 的范围内，以 0.001 为步长进行精细搜索
//     double search_range = 1.0;  // 搜索范围：±1
//     double step = 0.001;        // 搜索精度：0.001
    
//     double left = max(0.0, integer_best_n0 - search_range);
//     double right = min(static_cast<double>(params.BAW), integer_best_n0 + search_range);
    
//     // 遍历搜索
//     for (double n0 = left; n0 <= right; n0 += step) {
//         auto result = evaluate_continuous(n0, params);
//         auto [diff, D1, D2, T1, T2, D_mlo] = result;
        
//         // 只考虑有效结果
//         if (diff < 1e8 && diff < best_diff) {
//             best_diff = diff;
//             best_n0 = n0;
//             best_result = result;
//         }
//     }
    
//     // 如果没有找到更好的解，使用整数解进行评估
//     if (best_diff >= 1e8) {
//         best_n0 = integer_best_n0;
//         best_result = evaluate_continuous(best_n0, params);
//     }
    
//     return {best_n0, best_result};
// }

// 主计算流程 - 返回结果
Result process_BAW(int BAW, const Config& cfg, int N1, int N2, const vector<int32_t>& nmpdu_sld, 
                   const pair<double, double>& p1, const pair<double, double>& p2,
                   const vector<double>& tau_F, const vector<double>& R) {
    
    auto [pM1, pS1] = p1;
    auto [pM2, pS2] = p2;
    
    // 计算开销时间
    vector<double> T_DIFS(2);
    for (int i = 0; i < 2; ++i) T_DIFS[i] = cfg.T_SIFS[i] + cfg.aifsn * cfg.sigma;

    // === 整数优化 ===
    double min_diff = numeric_limits<double>::max();
    vector<int32_t> best_nmpdu(2, 0);
    double best_T1 = 0, best_T2 = 0, best_D1 = 0, best_D2 = 0;
    double best_alpha1 = 0, best_alpha2 = 0, best_lambda1 = 0, best_lambda2 = 0;

    for (int32_t n0 = 0; n0 <= BAW; ++n0) {
        int32_t n1 = BAW - n0;

        double L_subf_mld0 = (n0 == 1) ? cfg.L_subf_single : cfg.L_subf;
        double L_subf_mld1 = (n1 == 1) ? cfg.L_subf_single : cfg.L_subf;
        double L_subf_sld0 = (nmpdu_sld[0] == 1) ? cfg.L_subf_single : cfg.L_subf;
        double L_subf_sld1 = (nmpdu_sld[1] == 1) ? cfg.L_subf_single : cfg.L_subf;
        
        vector<double> T_BA = {calc_T_BA(n0, BAW, true), calc_T_BA(n1, BAW, false)};
        vector<double> T_BA_sld = {calc_T_BA(nmpdu_sld[0], BAW, true), calc_T_BA(nmpdu_sld[1], BAW, false)};
        
        vector<double> T_OH_mld(2), T_OH_sld(2);
        for (int i = 0; i < 2; ++i) {
            T_OH_mld[i] = (cfg.T_SIFS[i] * 3 + T_BA[i] + T_DIFS[i] + cfg.T_RTS[i] + cfg.T_CTS[i]) / cfg.sigma; // 没有加T_PH
            T_OH_sld[i] = (cfg.T_SIFS[i] * 3 + T_BA_sld[i] + T_DIFS[i] + cfg.T_RTS[i] + cfg.T_CTS[i]) / cfg.sigma;
        }

        vector<double> tau_T1(2), tau_T2(2);
        tau_T1[0] = calc_tau_T(n0, L_subf_mld0, R[0], cfg.sigma, T_OH_mld[0], cfg.T_PH, true);
        tau_T1[1] = calc_tau_T(nmpdu_sld[0], L_subf_sld0, R[0], cfg.sigma, T_OH_sld[0], cfg.T_PH, true);
        tau_T2[0] = calc_tau_T(n1, L_subf_mld1, R[1], cfg.sigma, T_OH_mld[1], cfg.T_PH, false);
        tau_T2[1] = calc_tau_T(nmpdu_sld[1], L_subf_sld1, R[1], cfg.sigma, T_OH_sld[1], cfg.T_PH, false);

        try {
            auto [lambdaS1, lambdaM1, alpha1] = compute_lambda(N1, pM1, pS1, tau_T1, tau_F[0], cfg.W, cfg.K);
            auto [lambdaS2, lambdaM2, alpha2] = compute_lambda(N2, pM2, pS2, tau_T2, tau_F[1], cfg.W, cfg.K);

            double D1 = get_throughput(tau_T1, cfg.L_P * n0, cfg.sigma, lambdaM1, n0);
            double D2 = get_throughput(tau_T2, cfg.L_P * n1, cfg.sigma, lambdaM2, n1);

            if (D1 < 0 || D2 < 0 || isnan(D1) || isnan(D2)) continue;

            double T1 = tau_T1[0] / lambdaM1;
            double T2 = tau_T2[0] / lambdaM2;
            if (isnan(T1) || isnan(T2) || isinf(T1) || isinf(T2)) continue;

            double diff = abs(T1 - T2);
            if (diff < min_diff) {
                min_diff = diff;
                best_nmpdu = {n0, n1};
                tie(best_T1, best_T2) = make_tuple(T1, T2);
                tie(best_D1, best_D2) = make_tuple(D1, D2);
                tie(best_alpha1, best_alpha2) = make_tuple(alpha1, alpha2);
                tie(best_lambda1, best_lambda2) = make_tuple(lambdaM1, lambdaM2);
            }
        } catch (const exception& e) {
            continue;
        }
    }

    // === 连续优化 ===
    OptimizationParams opt_params;
    opt_params.cfg = &cfg;
    opt_params.N1 = N1;
    opt_params.N2 = N2;
    opt_params.BAW = BAW;
    opt_params.nmpdu_sld = nmpdu_sld;
    opt_params.p1 = p1;
    opt_params.p2 = p2;
    opt_params.tau_F = tau_F;
    opt_params.R = R;
    
    // 传入整数最优解，在其附近进行精细搜索
    // auto [optimal_n0, continuous_result] = find_continuous_optimum(opt_params, best_nmpdu[0]);
    // auto [cont_diff, cont_D1, cont_D2, cont_T1, cont_T2, cont_D_mlo] = continuous_result;
    // double optimal_n1 = BAW - optimal_n0;

    // 计算SLO结果
    double L_subf_slo = cfg.L_subf;
    double L_subf_sld0_slo = (nmpdu_sld[0] == 1) ? cfg.L_subf_single : cfg.L_subf;
    double L_subf_sld1_slo = (nmpdu_sld[1] == 1) ? cfg.L_subf_single : cfg.L_subf;
    double T_BA0_slo = calc_T_BA(BAW, BAW, true);
    double T_BA1_slo = calc_T_BA(BAW, BAW, false);
    double T_BA_sld0_slo = calc_T_BA(nmpdu_sld[0], BAW, true);
    double T_BA_sld1_slo = calc_T_BA(nmpdu_sld[1], BAW, false);
    
    vector<double> T_OH_mld_slo(2), T_OH_sld_slo(2);
    for (int i = 0; i < 2; ++i) {
        double T_BA = (i == 0) ? T_BA0_slo : T_BA1_slo;
        double T_BA_sld = (i == 0) ? T_BA_sld0_slo : T_BA_sld1_slo;
        T_OH_mld_slo[i] = (cfg.T_SIFS[i] * 3 + T_BA + T_DIFS[i] + cfg.T_RTS[i] + cfg.T_CTS[i]) / cfg.sigma;
        T_OH_sld_slo[i] = (cfg.T_SIFS[i] * 3 + T_BA_sld + T_DIFS[i] + cfg.T_RTS[i] + cfg.T_CTS[i]) / cfg.sigma;
    }
    
    vector<double> tau_T1_slo(2), tau_T2_slo(2);
    tau_T1_slo[0] = calc_tau_T(BAW, L_subf_slo, R[0], cfg.sigma, T_OH_mld_slo[0], cfg.T_PH, true);
    tau_T1_slo[1] = calc_tau_T(nmpdu_sld[0], L_subf_sld0_slo, R[0], cfg.sigma, T_OH_sld_slo[0], cfg.T_PH, true);
    tau_T2_slo[0] = calc_tau_T(BAW, L_subf_slo, R[1], cfg.sigma, T_OH_mld_slo[1], cfg.T_PH, false);
    tau_T2_slo[1] = calc_tau_T(nmpdu_sld[1], L_subf_sld1_slo, R[1], cfg.sigma, T_OH_sld_slo[1], cfg.T_PH, false);

    double lambda1_slo = get<1>(compute_lambda(N1, pM1, pS1, tau_T1_slo, tau_F[0], cfg.W, cfg.K));
    double lambda2_slo = get<1>(compute_lambda(N2, pM2, pS2, tau_T2_slo, tau_F[1], cfg.W, cfg.K));
    double D1_slo = get_throughput(tau_T1_slo, cfg.L_P * BAW, cfg.sigma, lambda1_slo, BAW);
    double D2_slo = get_throughput(tau_T2_slo, cfg.L_P * BAW, cfg.sigma, lambda2_slo, BAW);

    // 构造并返回结果
    Result result;
    result.R = R;
    result.N1 = N1;
    result.N2 = N2;
    result.nmpdu_sld = nmpdu_sld;
    result.BAW = BAW;
    result.best_nmpdu = best_nmpdu;
    result.D_mlo = best_D1 + best_D2;
    result.D1 = best_D1;
    result.D2 = best_D2;
    result.T1 = best_T1;
    result.T2 = best_T2;
    result.p1 = pM1;
    result.p2 = pM2;
    result.alpha1 = best_alpha1;
    result.alpha2 = best_alpha2;
    result.lambda1 = best_lambda1;
    result.lambda2 = best_lambda2;
    result.D_slo = max(D1_slo, D2_slo);
    result.lambda_slo = (max(D1_slo, D2_slo) == D1_slo) ? lambda1_slo : lambda2_slo;

    
    return result;
}

int main() {
    Config cfg;
    ScanConfig scan_cfg;    

    // double start = 206.470592;
    // double end = 688.235296;
    // double step = (end - start) / 20;
    // for (int i = 0; i <= 20; ++i) {
    //     double value = start + i * step;
    //     if (value > end + step * 0.5) break;
    //     scan_cfg.R1_values.push_back(value);
    // }

    // 创建总CSV文件
    string summary_filename = "changer1r2-simple.csv";
    std::string csv_file = "/home/cmm/mlo_hw/scratch/solve-1vn/" + summary_filename;
    ofstream summary_csv(csv_file);
    summary_csv << "R1,R2,N1,N2,nmpdu_sld0,nmpdu_sld1,BAW,"
                   "Best_nmpdu_mld_0,Best_nmpdu_mld_1,"
                   "Max_nmpdu_mld_0,Max_nmpdu_mld_1,"
                   "D1,D2,D,T1,T2,"
                   "p1,p2,alpha1,alpha2,lambda_out1,lambda_out2,D_slo,lambda_out_slo\n";

    std::cout << "========== 开始参数扫描 ==========" << endl;
    std::cout << "N1 values: ";
    for (int v : scan_cfg.N1_values) std::cout << v << " ";
    std::cout << "\nN2 values: ";
    for (int v : scan_cfg.N2_values) std::cout << v << " ";
    std::cout << "\nnmpdu_sld0 values: ";
    for (int v : scan_cfg.nmpdu_sld0_values) std::cout << v << " ";
    std::cout << "\nnmpdu_sld1 values: ";
    for (int v : scan_cfg.nmpdu_sld1_values) std::cout << v << " ";
    std::cout << "\nBAW values: ";
    for (int v : scan_cfg.BAW_values) std::cout << v << " ";
    std::cout << "\n=================================" << endl;

    int total_combinations = scan_cfg.N1_values.size() * scan_cfg.N2_values.size() * 
                            scan_cfg.nmpdu_sld0_values.size() * scan_cfg.nmpdu_sld1_values.size() * scan_cfg.BAW_values.size() *
                            scan_cfg.R1_values.size() * scan_cfg.R2_values.size();
    int current_combination = 0;

    // 多重循环扫描所有参数组合
    for (double R1 : scan_cfg.R1_values) {
        for (double R2 : scan_cfg.R2_values) {
            for (int N1 : scan_cfg.N1_values) {
                for (int N2 : scan_cfg.N2_values) {
                    for (int nmpdu_sld0 : scan_cfg.nmpdu_sld0_values) {
                        for (int nmpdu_sld1 : scan_cfg.nmpdu_sld1_values) {
                            // int N2 = 4 - N1;
                            // int nmpdu_sld1 = scan_cfg.BAW_values[0] - nmpdu_sld0;
                            vector<double> tau_F(2);
                            vector<int32_t> nmpdu_mld_max(2);
                            vector<double> R = {R1, R2};
                            for (int i = 0; i < 2; ++i) {
                                tau_F[i] = (cfg.T_RTS[i] + cfg.T_SIFS[i] + cfg.aifsn * cfg.sigma) / cfg.sigma;
                                // 计算nmpdu_mld_max
                                bool is_link0 = (i == 0);
                                int32_t max_nmpdu = 1;
                                int32_t left = 1, right = 10000;
                                while (left <= right) {
                                    int32_t mid = (left + right) / 2;
                                    double payloadDuration = calc_payload_duration(mid, cfg.L_subf, R[i], is_link0);
                                    double ppduDuration = payloadDuration + cfg.T_PH;
                                    
                                    if (ppduDuration < cfg.maxPpduDuration) {
                                        max_nmpdu = mid;
                                        left = mid + 1;
                                    } else {
                                        right = mid - 1;
                                    }
                                }
                                nmpdu_mld_max[i] = max_nmpdu;
                            }

                            // 求解概率（每个N1/N2组合计算一次）
                            auto p1 = solve_p(N1, cfg.K, cfg.W);
                            auto p2 = solve_p(N2, cfg.K, cfg.W);

                            // 扫描不同的BAW值
                            for (int BAW : scan_cfg.BAW_values) {
                                current_combination++;
                                std::cout << "[" << current_combination << "/" << total_combinations << "] "
                                    << "Processing: rate1=" << R1 << ", rate2=" << R2
                                    << ", N1=" << N1 << ", N2=" << N2
                                    << ", nmpdu_sld0=" << nmpdu_sld0 << ", nmpdu_sld1=" << nmpdu_sld1 << ", BAW=" << BAW << "     " << std::endl;
                                
                                try {
                                    vector<int32_t> nmpdu_sld = {nmpdu_sld0, nmpdu_sld1};
                                    Result result = process_BAW(BAW, cfg, N1, N2, nmpdu_sld, p1, p2, tau_F, R);
                                    result.nmpdu_mld_max = nmpdu_mld_max;

                                    // 写入总CSV
                                    summary_csv << result.R[0] << "," << result.R[1] << ","
                                            << result.N1 << "," << result.N2 << ","  << result.nmpdu_sld[0] << "," << result.nmpdu_sld[1] << ","
                                            << result.BAW << "," << result.best_nmpdu[0] << "," << result.best_nmpdu[1] << ","
                                            << result.nmpdu_mld_max[0] << "," << result.nmpdu_mld_max[1] << ","
                                            << result.D1 << "," << result.D2 << "," << result.D_mlo << ","
                                            << result.T1 << "," << result.T2 << ","
                                            << result.p1 << "," << result.p2 << ","
                                            << result.alpha1 << "," << result.alpha2 << ","
                                            << result.lambda1 << "," << result.lambda2 << ","
                                            << result.D_slo << "," << result.lambda_slo << "\n";
                                } catch (const exception& e) {
                                    cerr << "\nError processing combination: " << e.what() << endl;
                                    continue;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    summary_csv.close();
    std::cout << "========== 扫描完成 ==========" << endl;
    std::cout << "result saved: " << csv_file << std::endl;

    return 0;
}