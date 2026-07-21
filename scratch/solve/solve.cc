#include <bits/stdc++.h>
#include <filesystem>
using namespace std;

// ═══════════════════════════════════════════════════════════════════════════════
//  统一求解脚本：支持 L=2（双链路）和 L=3（三链路）
//
//  优化目标 P1：
//
//    min_{M_A^(l,M)}   (1/L) * Σ_{l=1}^{L} ( T̄^(l,M) − (1/L)*Σ_j T̄^(j,M) )²
//    s.t.              Σ_{l=1}^{L} M_A^(l,M) = W_BA
//
//  运行时通过 NUM_LINKS 控制链路数（设为 2 或 3）。
//  通过 CSV_MODE 控制写入模式（OVERWRITE / APPEND）。
// ═══════════════════════════════════════════════════════════════════════════════

// ── 全局链路数 ────────────────────────────────────────
static constexpr int NUM_LINKS = 2;   // ← 改为 3 即切换到三链路模式
static bool SIMPLE = true; // ← true 使用简化的 payload duration 计算（nmpdu * L_subf / rate）

std::filesystem::path filepath = __FILE__;
static string csv_name = "solve-" + to_string(NUM_LINKS) + "link-" + to_string(SIMPLE) + "simp" + ".csv";
const std::string csvpath = (filepath.parent_path() / csv_name).string();
// ── CSV 写入模式 ─────────────────────────────────────────────────────────────
//   OVERWRITE : 每次运行清空文件，重新写表头 + 数据（默认）
//   APPEND    : 文件已存在则验证表头后直接追加数据行；
//               文件不存在则新建并写表头
enum class CsvMode { OVERWRITE, APPEND };
static constexpr CsvMode CSV_MODE = CsvMode::APPEND;  // ← 改为 APPEND 启用追加
// ── 扫描配置 ─────────────────────────────────────────────────────────────────
struct ScanConfig {
    // SLD 节点数（每条链路）
    vector<int> N_values = {4, 1, 0};          // 索引对应 link0/1/2

    // SLD 的 AMPDU 帧数（每条链路）
    vector<vector<int>> nmpdu_sld_values = {
        {24},   // link0 (2.4G)
        {232},  // link1 (5G)
        {0},  // link2 (6G)
    };

    vector<int>    BAW_values = {256};

    // 每条链路的速率扫描值 (Mbps)
    vector<vector<double>> R_values = {
        {206.470592,229.411766,258.088236,275.29412,286.764706,309.705884,344.117648,412.94118,458.823532,516.176472},
        // {206.470592,275.29412,309.705884,344.117648,412.94118,458.823532,516.176472,573.529412,619.411768,688.235296},  // link0 (2.4G)
        {1080.882354},  // link1 (5G)
        {0},  // link2 (6G)
    };

    // 每条链路的PER扫描值
    vector<vector<double>> per = {
        {0}, // link0
        {0}, // link1
        {0} // link2
    };

};
// ── 系统配置 ─────────────────────────────────────────────────────────────────
struct Config {
    int    aifsn          = 2;
    double sigma          = 9.0;
    double L_subf         = 1572.0 * 8.0;
    double L_subf_single  = 1570.0 * 8.0;
    double L_P            = 1500.0 * 8.0;
    double T_PH_D           = 72.0;
    // 索引: 0=2.4G, 1=5G, 2=6G
#if SIMPLE
    vector<double> T_RTS  = {24.0, 24.0, 24.0};
    vector<double> T_CTS  = {28.0, 28.0, 28.0};
#else
    vector<double> T_RTS  = {30.0, 24.0, 24.0};
    vector<double> T_CTS  = {34.0, 28.0, 28.0};
#endif
    vector<double> T_SIFS = {10.0, 16.0, 16.0};
    double maxPpduDuration = 5484.0;
    int    K               = 6;
    vector<double> W       = {16.0, 16.0, 16.0};
};

// ── 结果结构体 ───────────────────────────────────────────────────────────────
struct Result {
    int    L;                        // 实际链路数
    vector<double>  R;               // 各链路速率
    vector<int>     N;               // 各链路 SLD 数
    int    BAW;
    vector<int32_t> nmpdu_sld;       // 各链路 SLD AMPDU 长度
    vector<int32_t> best_nmpdu;      // 最优分配 [n0, n1(, n2)]
    vector<int32_t> nmpdu_mld_max;   // 各链路最大可用帧数
    double D_mlo;                    // 总吞吐量
    vector<double>  D;               // 各链路吞吐量
    vector<double>  T;               // 各链路平均传输时间
    double variance;                 // P1 目标函数值（方差）
    vector<double>  p;               // 各链路碰撞概率 pM
    vector<double>  alpha;           // 各链路 alpha
    vector<double>  lambda;          // 各链路 lambdaM
    double D_slo;                    // SLO 基准吞吐量
    double lambda_slo;               // SLO 基准 lambda
    vector<double>  per;             // 各链路 PER
};

// ═══════════════════════════════════════════════════════════════════════════════
//  基础数学工具
// ═══════════════════════════════════════════════════════════════════════════════

inline double compute_q(int i, double W0, int K) {
    if (i > K) i = K;
    return 2.0 / (1.0 + W0 * pow(2.0, i));
}

double compute_alpha(double pM, double pS, double tau_F,
                     double tau_T_M, double tau_T_S, int N) {
    double pM_pow = pow(pM, 1.0 / N);
    double denom  = 1.0 + tau_F
                    + (tau_T_M - tau_F) * pM
                    + N * (tau_T_S - tau_F) * pS
                    - (tau_T_M + N * (tau_T_S - tau_F)) * pS * pM_pow;
    return 1.0 / denom;
}

double denom_sum(double p, double W0, int K) {
    double s = 0.0;
    for (int i = 0; i < K; ++i)
        s += pow(1.0 - p, i) * p / compute_q(i, W0, K);
    s += pow(1.0 - p, K) / compute_q(K, W0, K);
    return s;
}

// pair<double, double> solve_p(int N, int K, const vector<double>& W,
//                               int max_iter = 2000, double tol = 1e-12,
//                               double damping = 0.4) {
//     if (N <= 0) return {0.0, 0.0};
//     const double eps = 1e-12;
//     double pM = 0.5, pS = 0.5;
//     for (int iter = 0; iter < max_iter; ++iter) {
//         double D_S    = denom_sum(pS, W[1], K);
//         double pM_new = (D_S <= 1.0 + 1e-15) ? eps : pow(1.0 - 1.0 / D_S, N);
//         pM_new = clamp(pM_new, eps, 1.0 - eps);
//         double D_M    = denom_sum(pM_new, W[0], K);
//         double pS_cand = (D_M <= 0) ? eps
//                         : (1.0 - 1.0 / D_M) * pow(1.0 - 1.0 / D_S, N - 1);
//         pS_cand = clamp(pS_cand, eps, 1.0 - eps);
//         double pS_new = (1.0 - damping) * pS + damping * pS_cand;
//         if (fabs(pM_new - pM) < tol && fabs(pS_new - pS) < tol)
//             return {pM_new, pS_new};
//         pM = pM_new;
//         pS = pS_new;
//     }
//     return {pM, pS};
// }

// 辅助函数：计算公式中括号内的发送概率项 tau
// 对应公式中 4(2p-1) / [W * (2p - (2-2p)^(K+1))]
double calc_p_part(double p, double W, int K) {
    double numerator = 4.0 * (2.0 * p - 1.0);
    double denominator = W * (2.0 * p - pow(2.0 - 2.0 * p, K + 1));
    
    // 防止除零或极小值导致溢出
    if (abs(denominator) < 1e-15) return 0.0;
    
    return numerator / denominator;
}

pair<double, double> solve_p(int N, int K, const vector<double>& W,
                           int max_iter = 2000, double tol = 1e-12,
                           double damping = 0.4) {
    // 假设 W[0] 是 W(l,M), W[1] 是 W(l,S)
    // 假设 n(l,S) 即为 N
    if (N <= 0) return {1.0, 1.0}; // 无竞争时成功概率为1
    
    const double eps = 1e-14;
    double pM = 0.9, pS = 0.9; // 初始值通常设为接近1的数

    for (int iter = 0; iter < max_iter; ++iter) {
        // 1. 计算 tau_S 和 tau_M
        double tau_S = calc_p_part(pS, W[1], K);
        double tau_M = calc_p_part(pM, W[0], K);

        // 2. 根据公式 (8) 更新 pM
        // pM = (1 - tau_S)^N
        double pM_new_raw = pow(max(0.0, 1.0 - tau_S), N);
        double pM_new = (1.0 - damping) * pM + damping * pM_new_raw;
        pM_new = clamp(pM_new, eps, 1.0 - eps);

        // 3. 根据公式 (9) 更新 pS
        // pS = (1 - tau_M) * (1 - tau_S)^(N-1)
        double pS_cand = (1.0 - tau_M) * pow(max(0.0, 1.0 - tau_S), N - 1);
        double pS_new = (1.0 - damping) * pS + damping * pS_cand;
        pS_new = clamp(pS_new, eps, 1.0 - eps);

        // 4. 收敛检查
        if (fabs(pM_new - pM) < tol && fabs(pS_new - pS) < tol) {
            return {pM_new, pS_new};
        }

        pM = pM_new;
        pS = pS_new;
    }

    return {pM, pS};
}

tuple<double, double, double>
compute_lambda(int n, double pM, double pS,
               const vector<double>& tau_T, double tau_F,
               const vector<double>& W, int K = 6) {
    if (n == 0)
        return {0.0, tau_T[0] / ((W[0] + 1.0) / 2.0 + tau_T[0]), 1.0};
    double alpha   = compute_alpha(pM, pS, tau_F, tau_T[0], tau_T[1], n);
    double lambdaS = n * (1.0 - pow(pM, 1.0 / n)) * alpha * pS * tau_T[1];
    double lambdaM = (1.0 - pS * pow(pM, 1.0 / n - 1.0)) * alpha * pM * tau_T[0];
    return {lambdaS, lambdaM, alpha};
}

double get_throughput(const vector<double>& tau_T, double PL,
                      double sigma, double lambda, double nmpdu, double per = 0.0) {
    if (nmpdu < 1e-9) return 0.0;
    double tp = lambda * (1- per) * PL / tau_T[0] / sigma;
    if (tp < 0) throw runtime_error("Invalid throughput: " + to_string(tp));
    return tp;
}

double calc_payload_duration(double nmpdu, double L_subf, double rate, bool is0 = false) {
    if (nmpdu < 1e-9) return 0.0;
    if(SIMPLE) {
        return (nmpdu * L_subf) / rate;
    }
    else {
        if (is0) {
            return (nmpdu * L_subf) / rate + 6;
        } else {
            return (nmpdu * L_subf) / rate;
        }
    }
    // return (nmpdu * L_subf) / rate;
}

double calc_tau_T(double nmpdu, double L_subf, double rate,
                  double sigma, double T_OH, double T_PH_D, bool is0) {
    double ppdu = calc_payload_duration(nmpdu, L_subf, rate, is0) + T_PH_D;
    return ppdu / sigma + T_OH;
}

// link0 (2.4G) 的 BA 比其余链路多 6 µs
double get_T_BA(double nmpdu, int BAW, bool is_link0) {
    if (nmpdu < 1.5){
        if(SIMPLE) return 28.0;
        else return is_link0 ? 34.0 : 28.0;
    }
    double T_BA = (BAW <= 256) ? 46.0 : (BAW <= 512) ? 58.0 : 78.0;
    if(SIMPLE) return T_BA - 6.0;
    else return is_link0 ? T_BA : T_BA - 6.0;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  P1 目标函数：各链路传输时间的方差
//    Var = (1/L) * Σ_l ( T_l - mean(T) )²
// ═══════════════════════════════════════════════════════════════════════════════
double calc_variance(const vector<double>& T) {
    int L = (int)T.size();
    double mean = 0.0;
    for (double t : T) mean += t;
    mean /= L;
    double var = 0.0;
    for (double t : T) var += (t - mean) * (t - mean);
    return var / L;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  单链路辅助计算：给定 AMPDU 帧数 n，返回 {lambdaM, D, T, alpha}
//  link_idx: 0=2.4G, 1=5G, 2=6G
// ═══════════════════════════════════════════════════════════════════════════════
struct LinkResult {
    double lambdaM, D, T, alpha;
};

LinkResult calc_link(int link_idx, int32_t n, int32_t nmpdu_sld_l,
                     double pM, double pS,
                     int N, int BAW,
                     const Config& cfg,
                     const vector<double>& tau_F_vec,
                     const vector<double>& T_DIFS,
                     const vector<double>& R,
                     double per = 0.0) {
    bool is0   = (link_idx == 0);
    double Lm  = (n           == 1) ? cfg.L_subf_single : cfg.L_subf;
    double Ls  = (nmpdu_sld_l == 1) ? cfg.L_subf_single : cfg.L_subf;

    double T_BA_mld = get_T_BA(n,           BAW, is0);
    double T_BA_sld = get_T_BA(nmpdu_sld_l, BAW, is0);

    double T_OH_mld = (cfg.T_SIFS[link_idx]*3 + T_BA_mld + T_DIFS[link_idx]
                       + cfg.T_RTS[link_idx] + cfg.T_CTS[link_idx]) / cfg.sigma;
    double T_OH_sld = (cfg.T_SIFS[link_idx]*3 + T_BA_sld + T_DIFS[link_idx]
                       + cfg.T_RTS[link_idx] + cfg.T_CTS[link_idx]) / cfg.sigma;

    vector<double> tau_T = {
        calc_tau_T(n, Lm, R[link_idx], cfg.sigma, T_OH_mld, cfg.T_PH_D, is0),
        calc_tau_T(nmpdu_sld_l, Ls, R[link_idx], cfg.sigma, T_OH_sld, cfg.T_PH_D, is0)
    };

    auto [lambdaS, lambdaM, alpha] = compute_lambda(N, pM, pS, tau_T,
                                                    tau_F_vec[link_idx],
                                                    cfg.W, cfg.K);
    double D = get_throughput(tau_T, cfg.L_P * n, cfg.sigma, lambdaM, n, per);
    double T = tau_T[0] / (lambdaM * (1- per));   // 传输时间仍基于原始 lambdaM（反映信道占用）
    return {lambdaM, D, T, alpha};
}

// ═══════════════════════════════════════════════════════════════════════════════
//  主计算函数：统一处理 L 条链路
//  枚举所有满足 Σ n_l = BAW 的整数分配，最小化 P1 方差目标
// ═══════════════════════════════════════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════════════════════════
//  主计算函数：增加对 n=1 情况的二次扫描逻辑
// ═══════════════════════════════════════════════════════════════════════════════
Result process_BAW(int L, int BAW, const Config& cfg,
                   const vector<int>&           N_vec,
                   const vector<int32_t>&       nmpdu_sld,
                   const vector<pair<double,double>>& p_vec,
                   const vector<double>&        tau_F,
                   const vector<double>&        R,
                   const vector<double>&        per_vec)
{
    vector<double> T_DIFS(L);
    for (int i = 0; i < L; ++i) T_DIFS[i] = cfg.T_SIFS[i] + cfg.aifsn * cfg.sigma;

    vector<double> pM(L), pS(L);
    for (int i = 0; i < L; ++i) { pM[i] = p_vec[i].first; pS[i] = p_vec[i].second; }

    // 辅助 lambda：计算特定分配下的方差和数据
    auto evaluate_config = [&](const vector<int32_t>& n_cur) {
        vector<double> D_l(L, 0), T_l(L, 0), alpha_l(L, 0), lambda_l(L, 0);
        vector<double> active_T; 
        bool valid = true;
        try {
            for (int l = 0; l < L; ++l) {
                if (n_cur[l] > 0) {
                    auto res = calc_link(l, n_cur[l], nmpdu_sld[l], pM[l], pS[l], N_vec[l], BAW, cfg, tau_F, T_DIFS, R, per_vec[l]);
                    if (res.D < 0 || isnan(res.D) || isnan(res.T) || isinf(res.T)) { valid = false; break; }
                    D_l[l] = res.D; T_l[l] = res.T; alpha_l[l] = res.alpha; lambda_l[l] = res.lambdaM;
                    active_T.push_back(res.T);
                }
            }
        } catch (...) { valid = false; }
        
        double var = active_T.empty() ? numeric_limits<double>::max() : calc_variance(active_T);
        return make_tuple(valid, var, D_l, T_l, alpha_l, lambda_l);
    };

    double min_var = numeric_limits<double>::max();
    vector<int32_t> best_n(L, 0);
    vector<double> best_D(L, 0), best_T(L, 0), best_alpha(L, 0), best_lambda(L, 0);

    // 1. 初始全链路枚举 (允许 n >= 1)
    vector<int32_t> n_cur(L, 0);
    function<void(int, int32_t)> enumerate = [&](int depth, int32_t remaining) {
        if (depth == L - 1) {
            n_cur[depth] = remaining;
            if(L == 3) {
                for(int l=0; l<L; ++l) if(n_cur[l] < 1) return; 
            }
            auto [valid, var, D_l, T_l, alpha_l, lambda_l] = evaluate_config(n_cur);
            if (valid && var < min_var) {
                min_var = var; best_n = n_cur; best_D = D_l; best_T = T_l; best_alpha = alpha_l; best_lambda = lambda_l;
            }
            return;
        }
        for (int32_t v = 1; v <= remaining - (L - 1 - depth); ++v) {
            n_cur[depth] = v;
            enumerate(depth + 1, remaining - v);
        }
    };
    enumerate(0, BAW);

    // 2. 检查是否有 n=1 的链路，并进行退化路径扫描
    vector<int> ones_indices;
    for(int i=0; i<L; ++i) if(best_n[i] == 1) ones_indices.push_back(i);

    if (!ones_indices.empty() && L == 2) {
        for (int close_idx : ones_indices) {
            int keep_idx = 1 - close_idx;   // L==2 时另一条链路下标固定
            vector<int32_t> n_sub(L, 0);
            n_sub[keep_idx] = BAW;
            auto [valid, var, D_l, T_l, alpha_l, lambda_l] = evaluate_config(n_sub);
            if (valid && var <= min_var) {   // 单链路方差为0，通常更优
                min_var = var; best_n = n_sub; best_D = D_l; best_T = T_l;
                best_alpha = alpha_l; best_lambda = lambda_l;
            }
        }
    }

    if (!ones_indices.empty() && L == 3) {
        // 定义需要尝试关闭的索引组合 (Power Set of ones_indices)
        // 例如：如果有两个链路为 1，则尝试：关闭 A, 关闭 B, 同时关闭 A&B
        int num_ones = ones_indices.size();
        for (int i = 1; i < (1 << num_ones); ++i) {
            vector<int> to_close;
            vector<int> to_keep;
            for (int j = 0; j < num_ones; ++j) {
                if ((i >> j) & 1) to_close.push_back(ones_indices[j]);
            }
            
            // 找出哪些是真正保持开启的链路索引
            for(int k=0; k<L; ++k) {
                bool closed = false;
                for(int c : to_close) if(k == c) closed = true;
                if(!closed) to_keep.push_back(k);
            }

            if (to_keep.empty()) continue;

            // 在剩下的 to_keep 链路中重新分配全部 BAW
            if (to_keep.size() == 1) {
                // 退化为单链路 (SLO 模式)
                vector<int32_t> n_sub(L, 0);
                n_sub[to_keep[0]] = BAW;
                auto [valid, var, D_l, T_l, alpha_l, lambda_l] = evaluate_config(n_sub);
                if (valid && var <= min_var) { // 单链路方差为0，通常更优
                    min_var = var; best_n = n_sub; best_D = D_l; best_T = T_l; best_alpha = alpha_l; best_lambda = lambda_l;
                }
            } else if (to_keep.size() == 2) {
                // 退化为双链路扫描
                for (int32_t v = 1; v < BAW; ++v) {
                    vector<int32_t> n_sub(L, 0);
                    n_sub[to_keep[0]] = v;
                    n_sub[to_keep[1]] = BAW - v;
                    auto [valid, var, D_l, T_l, alpha_l, lambda_l] = evaluate_config(n_sub);
                    if (valid && var < min_var) {
                        min_var = var; best_n = n_sub; best_D = D_l; best_T = T_l; best_alpha = alpha_l; best_lambda = lambda_l;
                    }
                }
            }
        }
    }

    // ── SLO 基准计算保持不变 ──
    vector<double> D_slo_vec(L), lambda_slo_vec(L);
    for (int l = 0; l < L; ++l) {
        try {
            auto res = calc_link(l, BAW, nmpdu_sld[l], pM[l], pS[l], N_vec[l], BAW, cfg, tau_F, T_DIFS, R, per_vec[l]);
            D_slo_vec[l] = res.D; lambda_slo_vec[l] = res.lambdaM;
        } catch (...) { D_slo_vec[l] = 0.0; lambda_slo_vec[l] = 0.0; }
    }
    int best_slo_idx = (int)(max_element(D_slo_vec.begin(), D_slo_vec.end()) - D_slo_vec.begin());

    Result res;
    res.L = L; res.R = R; res.N = N_vec; res.BAW = BAW; res.nmpdu_sld = nmpdu_sld;
    res.best_nmpdu = best_n; res.D = best_D; res.T = best_T; res.variance = min_var;
    res.p = pM; res.alpha = best_alpha; res.lambda = best_lambda;
    res.D_mlo = 0.0; for (double d : best_D) res.D_mlo += d;
    res.D_slo = D_slo_vec[best_slo_idx]; res.lambda_slo = lambda_slo_vec[best_slo_idx];
    res.per = per_vec;
    return res;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  CSV 工具
// ═══════════════════════════════════════════════════════════════════════════════

// 生成标准表头字符串（不含换行）
string make_csv_header(int L) {
    ostringstream oss;
    for (int l = 0; l < L; ++l) oss << "R"      << l << ",";
    for (int l = 0; l < L; ++l) oss << "nsld"      << l << ",";
    for (int l = 0; l < L; ++l) oss << "ampdunumsld" << l << ",";
    for (int l = 0; l < L; ++l) oss << "fixedPER"    << l << ",";
    oss << "bawsize,";
    for (int l = 0; l < L; ++l) oss << "maxampdunum" << l << ",";
    for (int l = 0; l < L; ++l) oss << "Max_n"  << l << ",";
    for (int l = 0; l < L; ++l) oss << "D"      << l << ",";
    oss << "D_total,";
    for (int l = 0; l < L; ++l) oss << "T"      << l << ",";
    oss << "Variance,";
    for (int l = 0; l < L; ++l) oss << "p"      << l << ",";
    for (int l = 0; l < L; ++l) oss << "alpha"  << l << ",";
    for (int l = 0; l < L; ++l) oss << "lambda" << l << ",";
    oss << "D_slo,lambda_slo";
    return oss.str();
}

// 打开 CSV 文件：处理表头写入 / 追加验证
// 若追加模式下表头不匹配，抛出 runtime_error
ofstream open_csv(const string& path, int L, CsvMode mode) {
    string expected = make_csv_header(L);

    if (mode == CsvMode::APPEND && filesystem::exists(path)) {
        // ── 追加模式，文件已存在：先读第一行验证表头 ────────────────────────
        ifstream fin(path);
        string first_line;
        if (getline(fin, first_line)) {
            // 兼容 Windows \r\n
            if (!first_line.empty() && first_line.back() == '\r')
                first_line.pop_back();

            // 去掉表头中每一列前后的多余空格，并重新拼接
            auto normalize_header = [](const std::string& line) -> std::string {
                auto trim = [](const std::string& s) -> std::string {
                    size_t start = s.find_first_not_of(" \t");
                    if (start == std::string::npos) return "";
                    size_t end = s.find_last_not_of(" \t");
                    return s.substr(start, end - start + 1);
                };

                std::stringstream ss(line);
                std::string token;
                std::string result;
                bool first = true;

                while (std::getline(ss, token, ',')) {
                    if (!first) result += ",";
                    result += trim(token);
                    first = false;
                }

                return result;
            };

            std::string normalized_first_line = normalize_header(first_line);
            std::string normalized_expected   = normalize_header(expected);

            if (normalized_first_line != normalized_expected) {
                throw std::runtime_error(
                    "表头不匹配，无法追加！\n"
                    "  文件表头 : " + first_line + "\n"
                    "  规范后表头: " + normalized_first_line + "\n"
                    "  期望表头 : " + expected + "\n"
                    "请检查 NUM_LINKS 是否与文件一致，或删除旧文件后重试。");
            }
        }
        fin.close();

        ofstream fout(path, ios::app);
        if (!fout) throw runtime_error("无法打开文件（追加）: " + path);
        cout << "[CSV] 追加模式  →  " << path
             << "  （已有数据将保留，新数据追加到末尾）\n";
        return fout;

    } else {
        // ── 覆盖模式，或追加但文件不存在：新建并写表头 ──────────────────────
        ofstream fout(path, ios::out | ios::trunc);
        if (!fout) throw runtime_error("无法创建文件: " + path);
        fout << expected << "\n";

        if (mode == CsvMode::APPEND)
            cout << "[CSV] 追加模式（新建）→  " << path << "\n";
        else
            cout << "[CSV] 覆盖模式  →  " << path << "\n";
        return fout;
    }
}

// 写一行数据
void write_csv_row(ofstream& f, const Result& r) {
    int L = r.L;
    for (int l = 0; l < L; ++l) f << r.R[l]              << ",";
    for (int l = 0; l < L; ++l) f << r.N[l]              << ",";
    for (int l = 0; l < L; ++l) f << r.nmpdu_sld[l]      << ",";
    for (int l = 0; l < L; ++l) f << r.per[l]            << ",";
    f << r.BAW << ",";
    for (int l = 0; l < L; ++l) f << r.best_nmpdu[l]     << ",";
    for (int l = 0; l < L; ++l) f << r.nmpdu_mld_max[l]  << ",";
    for (int l = 0; l < L; ++l) f << r.D[l]              << ",";
    f << r.D_mlo << ",";
    for (int l = 0; l < L; ++l) f << r.T[l]              << ",";
    f << r.variance << ",";
    for (int l = 0; l < L; ++l) f << r.p[l]              << ",";
    for (int l = 0; l < L; ++l) f << r.alpha[l]          << ",";
    for (int l = 0; l < L; ++l) f << r.lambda[l]         << ",";
    f << r.D_slo << "," << r.lambda_slo << "\n";
}

// ═══════════════════════════════════════════════════════════════════════════════
//  main
// ═══════════════════════════════════════════════════════════════════════════════
int main() {
    const int L = NUM_LINKS;   // 2 或 3
    Config     cfg;
    ScanConfig scan_cfg;

    // 截断到实际链路数（保证 scan_cfg 中多余的 6G 列在 L=2 时不被使用）
    scan_cfg.N_values.resize(L);
    scan_cfg.nmpdu_sld_values.resize(L);
    scan_cfg.R_values.resize(L);
    scan_cfg.per.resize(L);

    ofstream summary_csv;
    try {
        summary_csv = open_csv(csvpath, L, CSV_MODE);
    } catch (const exception& e) {
        cerr << "[错误] " << e.what() << endl;
        return 1;
    }

    // ── 打印扫描配置 ─────────────────────────────────────────────────────────
    cout << "========== 开始参数扫描（" << L << " 链路 / P1 方差目标）==========\n";
    cout << "[模式] " << (CSV_MODE == CsvMode::APPEND ? "追加" : "覆盖") << "\n";
    for (int l = 0; l < L; ++l) {
        cout << "Link" << l << " N=" << scan_cfg.N_values[l]
             << "  nmpdu_sld=";
        for (int v : scan_cfg.nmpdu_sld_values[l]) cout << v << " ";
        cout << "  R(" << scan_cfg.R_values[l].size() << " values)"
             << "  per(" << scan_cfg.per[l].size() << " values)\n";
    }
    cout << "BAW: "; for (int v : scan_cfg.BAW_values) cout << v << " "; cout << "\n";

    long long total = 1;
    for (int l = 0; l < L; ++l) total *= (long long)scan_cfg.nmpdu_sld_values[l].size();
    for (int l = 0; l < L; ++l) total *= (long long)scan_cfg.R_values[l].size();
    for (int l = 0; l < L; ++l) total *= (long long)scan_cfg.per[l].size();
    total *= (long long)scan_cfg.BAW_values.size();
    cout << "Total combinations: " << total << "\n";
    cout << "=================================================" << endl;

    long long       cur = 0;
    vector<double>  R_cur(L);
    vector<int>     sld_cur(L);
    vector<double>  per_cur(L);

    function<void(int)> scan_links = [&](int l) {
        if (l == L) {
            vector<double>  tau_F(L);
            vector<int32_t> nmpdu_mld_max(L);
            vector<pair<double,double>> p_vec(L);

            for (int i = 0; i < L; ++i) {
                tau_F[i] = (cfg.T_RTS[i] + cfg.T_SIFS[i] + cfg.aifsn * cfg.sigma) / cfg.sigma;
                // 二分搜索最大 AMPDU 帧数
                int32_t mx = 1, lo = 1, hi = 10000;
                while (lo <= hi) {
                    int32_t mid = (lo + hi) / 2;
                    double ppdu = calc_payload_duration(mid, cfg.L_subf, R_cur[i], i == 0) + cfg.T_PH_D;
                    if (ppdu < cfg.maxPpduDuration) { mx = mid; lo = mid + 1; }
                    else                            {           hi = mid - 1; }
                }
                nmpdu_mld_max[i] = mx;
                p_vec[i]         = solve_p(scan_cfg.N_values[i], cfg.K, cfg.W);
            }

            // 遍历 BAW
            for (int BAW : scan_cfg.BAW_values) {
                ++cur;
                cout << "[" << cur << "/" << total << "] ";
                for (int i = 0; i < L; ++i) cout << "R" << i+1 << "=" << R_cur[i] << " ";
                cout << "sld=(";
                for (int i = 0; i < L; ++i) cout << sld_cur[i] << (i<L-1?",":"");
                cout << ") per=(";
                for (int i = 0; i < L; ++i) cout << per_cur[i] << (i<L-1?",":"");
                cout << ") BAW=" << BAW << endl;

                try {
                    vector<int32_t> nmpdu_sld(sld_cur.begin(), sld_cur.end());
                    vector<double>  R_vec(R_cur.begin(), R_cur.end());

                    Result result = process_BAW(L, BAW, cfg,
                                                scan_cfg.N_values,
                                                nmpdu_sld,
                                                p_vec, tau_F, R_vec,
                                                per_cur);
                    result.nmpdu_mld_max = nmpdu_mld_max;
                    write_csv_row(summary_csv, result);
                } catch (const exception& e) {
                    cerr << "Error: " << e.what() << endl;
                }
            }
            return;
        }

        // 枚举第 l 条链路的速率、SLD 帧数和 PER
        for (double R  : scan_cfg.R_values[l]) {
            R_cur[l] = R;
            for (int s : scan_cfg.nmpdu_sld_values[l]) {
                sld_cur[l] = s;
                for (double p : scan_cfg.per[l]) {
                    per_cur[l] = p;
                    scan_links(l + 1);
                }
            }
        }
    };

    scan_links(0);

    summary_csv.close();
    cout << "========== 扫描完成 ==========" << endl;
    cout << "结果保存至: " << csvpath << endl;
    return 0;
}