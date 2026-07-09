#include <bits/stdc++.h>
#include <filesystem>
using namespace std;

// ═══════════════════════════════════════════════════════════════════════════════
//  双链路连续 AMPDU 优化脚本（基于 solve-link3.cc）
//
//  改动说明（相对于 solve-link3.cc）：
//    1. 固定 L=2（仅双链路）
//    2. 去除 PER 支持（per 恒为 0）
//    3. AMPDU 分配从整数枚举改为连续搜索：
//         在 [0, BAW] 上以固定步长 CONT_STEP 扫描 n0（浮点），
//         n1 = BAW - n0，直接求方差最小解
//    4. 去除 n=1 退化二次扫描（连续域中 n=1 不再是特殊奇点）
//
//  优化目标 P1（与 solve-link3 一致）：
//    min_{n0}  Var(T0, T1)  =  (1/2)*[(T0-mean)² + (T1-mean)²]
//    s.t.      n0 + n1 = BAW,   n0 > 0,  n1 > 0
// ═══════════════════════════════════════════════════════════════════════════════

static constexpr int  NUM_LINKS  = 2;
static bool           SIMPLE     = true;   // true: payload = n*L_subf/rate（简化模式）
static constexpr double CONT_STEP = 0.1;   // 连续扫描步长（帧数单位）

static string csv_file =
    "/home/cmm/mlo_hw/scratch/solve-1vn-theo/solve-2link-continuous-"
    + to_string(SIMPLE) + "simp-2.csv";

// ── CSV 写入模式 ─────────────────────────────────────────────────────────────
enum class CsvMode { OVERWRITE, APPEND };
static constexpr CsvMode CSV_MODE = CsvMode::APPEND;

// ── 扫描配置 ─────────────────────────────────────────────────────────────────
struct ScanConfig {
    // SLD 节点数（每条链路）
    vector<int> N_values = {7, 3};          // link0 (2.4G), link1 (5G)

    // SLD 的 AMPDU 帧数（每条链路）
    vector<vector<int>> nmpdu_sld_values = {
        {768},   // link0 (2.4G)
        {256},  // link1 (5G)
    };

    vector<int> BAW_values = {1024};

    // 每条链路的速率扫描值 (Mbps)
vector<vector<double>> R_values = {
    {108.088235, 113.182277, 118.516393, 124.101899, 129.950641,
     136.075026, 142.488045, 149.203299, 156.235034, 163.598165,
     171.308308, 179.381820, 187.835825, 196.688255, 205.957887,
     215.664383, 225.828332, 236.471293, 247.615842, 259.285617,
     271.505372, 284.301026, 297.699720, 311.729876, 326.421252,
     341.805012, 357.913786, 374.781743, 392.444663, 410.940010,
     430.307016, 450.586761, 471.822260, 494.058558, 517.342821,
     541.724437, 567.255123, 593.989033, 621.982874, 651.296024,
     681.990660, 714.131889, 747.787888, 783.030046, 819.933115,
     858.575372, 899.038783, 941.409175, 985.776423, 1032.234634,
     1080.882354, 1131.822770, 1185.163934, 1241.018989, 1299.506412,
     1360.750263, 1424.880447, 1492.032993, 1562.350342, 1635.981645,
     1713.083085, 1793.818203, 1878.358251, 1966.882548, 2059.578866,
     2156.643828, 2258.283320, 2364.712934, 2476.158421, 2592.856172,
     2715.053719, 2843.010259, 2976.997204, 3117.298760, 3264.212525,
     3418.050121, 3579.137861, 3747.817431, 3924.446625, 4109.400097,
     4303.070157, 4505.867605, 4718.222603, 4940.585584, 5173.428209,
     5417.244369, 5672.551230, 5939.890334, 6219.828741, 6512.960238,
     6819.906598, 7141.318894, 7477.878884, 7830.300458, 8199.331150,
     8585.753722, 8990.387829, 9414.091754, 9857.764229, 10322.346344,
     10808.823540},
    {1080.882354},
};
};

// ── 系统配置 ─────────────────────────────────────────────────────────────────
struct Config {
    int    aifsn         = 2;
    double sigma         = 9.0;
    double L_subf        = 1572.0 * 8.0;
    double L_subf_single = 1570.0 * 8.0;
    double L_P           = 1500.0 * 8.0;
    double T_PH          = 72.0;
    // 索引: 0=2.4G, 1=5G
    vector<double> T_RTS  = {24.0, 24.0};
    vector<double> T_CTS  = {28.0, 28.0};
    vector<double> T_SIFS = {10.0, 16.0};
    double maxPpduDuration = 5484.0;
    int    K               = 6;
    vector<double> W       = {16.0, 16.0};
};

// ── 结果结构体 ───────────────────────────────────────────────────────────────
struct Result {
    int             L;
    vector<double>  R;
    vector<int>     N;
    int             BAW;
    vector<int32_t> nmpdu_sld;
    vector<double>  best_nmpdu;       // 连续最优分配（浮点）
    double          D_mlo;
    vector<double>  D;
    vector<double>  T;
    double          variance;
};

// ═══════════════════════════════════════════════════════════════════════════════
//  基础数学工具（与 solve-link3 保持一致）
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

// calc_tau：对应公式 4(2p-1) / [W*(2p-(2-2p)^{K+1})]
double calc_tau(double p, double W, int K) {
    double numerator   = 4.0 * (2.0 * p - 1.0);
    double denominator = W * (2.0 * p - pow(2.0 - 2.0 * p, K + 1));
    if (abs(denominator) < 1e-15) return 0.0;
    return numerator / denominator;
}

pair<double, double> solve_p(int N, int K, const vector<double>& W,
                             int    max_iter = 2000,
                             double tol      = 1e-12,
                             double damping  = 0.4) {
    if (N <= 0) return {1.0, 1.0};
    const double eps = 1e-14;
    double pM = 0.9, pS = 0.9;
    for (int iter = 0; iter < max_iter; ++iter) {
        double tau_S    = calc_tau(pS, W[1], K);
        double tau_M    = calc_tau(pM, W[0], K);
        double pM_new   = (1.0 - damping) * pM + damping * pow(max(0.0, 1.0 - tau_S), N);
        pM_new          = clamp(pM_new, eps, 1.0 - eps);
        double pS_cand  = (1.0 - tau_M) * pow(max(0.0, 1.0 - tau_S), N - 1);
        double pS_new   = (1.0 - damping) * pS + damping * pS_cand;
        pS_new          = clamp(pS_new, eps, 1.0 - eps);
        if (fabs(pM_new - pM) < tol && fabs(pS_new - pS) < tol)
            return {pM_new, pS_new};
        pM = pM_new;
        pS = pS_new;
    }
    return {pM, pS};
}

tuple<double, double, double>
compute_lambda(int n_int, double pM, double pS,
               const vector<double>& tau_T, double tau_F,
               const vector<double>& W, int K = 6) {
    // n_int 仅用于 N=0 判断，实际 n 为连续值通过 tau_T 传入
    if (n_int == 0)
        return {0.0, tau_T[0] / ((W[0] + 1.0) / 2.0 + tau_T[0]), 1.0};
    double alpha   = compute_alpha(pM, pS, tau_F, tau_T[0], tau_T[1], n_int);
    double lambdaS = n_int * (1.0 - pow(pM, 1.0 / n_int)) * alpha * pS * tau_T[1];
    double lambdaM = (1.0 - pS * pow(pM, 1.0 / n_int - 1.0)) * alpha * pM * tau_T[0];
    return {lambdaS, lambdaM, alpha};
}

// 吞吐量：去除 per 参数
double get_throughput(const vector<double>& tau_T, double PL,
                      double sigma, double lambda, double nmpdu) {
    if (nmpdu < 1e-9) return 0.0;
    double tp = lambda * PL / tau_T[0] / sigma;
    if (tp < 0) throw runtime_error("Invalid throughput: " + to_string(tp));
    return tp;
}

// payload 时长：连续 nmpdu（浮点）
double calc_payload_duration(double nmpdu, double L_subf, double rate) {
    if (nmpdu < 1e-9) return 0.0;
    if (SIMPLE) {
        return (nmpdu * L_subf) / rate;
    } else {
        // 非 SIMPLE 模式下保留 SERVICE(16bit)+TAIL(6bit) 的对齐余量
        return (nmpdu * L_subf + 6.0) / rate;
    }
}

double calc_tau_T(double nmpdu, double L_subf, double rate,
                  double sigma, double T_OH, double T_PH) {
    double ppdu = calc_payload_duration(nmpdu, L_subf, rate) + T_PH;
    return ppdu / sigma + T_OH;
}

// T_BA：连续 nmpdu，以 1.5 为整数 1 的阈值
double calc_T_BA(double nmpdu, int BAW, bool is_link0) {
    if (nmpdu < 1.5){
        if(SIMPLE) return 28.0;
        else return is_link0 ? 34.0 : 28.0;
    }
    double T_BA = (BAW <= 256) ? 46.0 : (BAW <= 512) ? 58.0 : 78.0;
    if(SIMPLE) return T_BA - 6.0;
    else return is_link0 ? T_BA : T_BA - 6.0;
}

// 方差：(1/L)*Σ(T_l - mean)²
double calc_variance(const vector<double>& T) {
    int    L    = (int)T.size();
    double mean = 0.0;
    for (double t : T) mean += t;
    mean /= L;
    double var = 0.0;
    for (double t : T) var += (t - mean) * (t - mean);
    return var / L;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  单链路计算：接受浮点 n（连续 AMPDU 帧数）
// ═══════════════════════════════════════════════════════════════════════════════
struct LinkResult {
    double lambdaM, D, T, alpha;
};

LinkResult calc_link(int link_idx, double n, int32_t nmpdu_sld_l,
                     double pM, double pS,
                     int N, int BAW,
                     const Config&         cfg,
                     const vector<double>& tau_F_vec,
                     const vector<double>& T_DIFS,
                     const vector<double>& R) {
    bool   is0 = (link_idx == 0);
    // 子帧长度：连续场景下不区分 n==1 的单帧模式（1帧是退化点，连续搜索会自然跳过）
    double Lm  = cfg.L_subf;
    double Ls  = (nmpdu_sld_l == 1) ? cfg.L_subf_single : cfg.L_subf;

    double T_BA_mld = calc_T_BA(n,           BAW, is0);
    double T_BA_sld = calc_T_BA(nmpdu_sld_l, BAW, is0);

    double T_OH_mld = (cfg.T_SIFS[link_idx] * 3 + T_BA_mld + T_DIFS[link_idx]
                       + cfg.T_RTS[link_idx] + cfg.T_CTS[link_idx]) / cfg.sigma;
    double T_OH_sld = (cfg.T_SIFS[link_idx] * 3 + T_BA_sld + T_DIFS[link_idx]
                       + cfg.T_RTS[link_idx] + cfg.T_CTS[link_idx]) / cfg.sigma;

    vector<double> tau_T = {
        calc_tau_T(n,           Lm, R[link_idx], cfg.sigma, T_OH_mld, cfg.T_PH),
        calc_tau_T(nmpdu_sld_l, Ls, R[link_idx], cfg.sigma, T_OH_sld, cfg.T_PH)
    };

    // compute_lambda 内部用整数 N（站点数），n 的浮点效果已通过 tau_T 体现
    // 注意：pM^(1/n) 中 n 需为浮点，需要单独处理
    // 这里重写 compute_lambda 的浮点版本
    double lambdaM, alpha;
    if (N == 0) {
        lambdaM = tau_T[0] / ((cfg.W[0] + 1.0) / 2.0 + tau_T[0]);
        alpha   = 1.0;
    } else {
        alpha   = compute_alpha(pM, pS, tau_F_vec[link_idx], tau_T[0], tau_T[1], N);
        lambdaM = (1.0 - pS * pow(pM, 1.0 / N - 1.0)) * alpha * pM * tau_T[0];
    }

    double D = get_throughput(tau_T, cfg.L_P * n, cfg.sigma, lambdaM, n);
    double T = (lambdaM > 1e-15) ? tau_T[0] / lambdaM : 1e9;
    return {lambdaM, D, T, alpha};
}

// ═══════════════════════════════════════════════════════════════════════════════
//  主计算函数：连续扫描 n0 ∈ (0, BAW)，步长 CONT_STEP
// ═══════════════════════════════════════════════════════════════════════════════
Result process_BAW(int BAW, const Config& cfg,
                   const vector<int>&              N_vec,
                   const vector<int32_t>&          nmpdu_sld,
                   const vector<pair<double,double>>& p_vec,
                   const vector<double>&           tau_F,
                   const vector<double>&           R) {
    const int L = NUM_LINKS;

    vector<double> T_DIFS(L);
    for (int i = 0; i < L; ++i)
        T_DIFS[i] = cfg.T_SIFS[i] + cfg.aifsn * cfg.sigma;

    vector<double> pM(L), pS(L);
    for (int i = 0; i < L; ++i) { pM[i] = p_vec[i].first; pS[i] = p_vec[i].second; }

    double min_var = numeric_limits<double>::max();
    double best_n0 = CONT_STEP;   // 连续最优解
    vector<double> best_D(L, 0), best_T(L, 0);

    // 连续扫描：n0 从 CONT_STEP 到 BAW-CONT_STEP（排除端点 0 和 BAW，避免退化）
    for (double n0 = CONT_STEP; n0 < BAW - CONT_STEP / 2.0; n0 += CONT_STEP) {
        double n1 = BAW - n0;

        try {
            auto r0 = calc_link(0, n0, nmpdu_sld[0], pM[0], pS[0], N_vec[0], BAW, cfg, tau_F, T_DIFS, R);
            auto r1 = calc_link(1, n1, nmpdu_sld[1], pM[1], pS[1], N_vec[1], BAW, cfg, tau_F, T_DIFS, R);

            if (r0.D < 0 || r1.D < 0 || isnan(r0.D) || isnan(r1.D) ||
                isnan(r0.T) || isnan(r1.T) || isinf(r0.T) || isinf(r1.T))
                continue;

            double var = calc_variance({r0.T, r1.T});
            if (var < min_var) {
                min_var   = var;
                best_n0   = n0;
                best_D[0] = r0.D;  best_D[1] = r1.D;
                best_T[0] = r0.T;  best_T[1] = r1.T;
            }
        } catch (...) { continue; }
    }

    double best_n1 = BAW - best_n0;

    Result res;
    res.L          = L;
    res.R          = R;
    res.N          = N_vec;
    res.BAW        = BAW;
    res.nmpdu_sld  = nmpdu_sld;
    res.best_nmpdu = {best_n0, best_n1};
    res.D          = best_D;
    res.T          = best_T;
    res.variance   = min_var;
    res.D_mlo      = 0.0; for (double d : best_D) res.D_mlo += d;
    return res;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  CSV 工具
// ═══════════════════════════════════════════════════════════════════════════════

string make_csv_header(int L) {
    ostringstream oss;
    for (int l = 0; l < L; ++l) oss << "R"           << l << ",";
    for (int l = 0; l < L; ++l) oss << "nsld"        << l << ",";
    for (int l = 0; l < L; ++l) oss << "ampdunumsld" << l << ",";
    oss << "bawsize,";
    for (int l = 0; l < L; ++l) oss << "cont_n"      << l << ",";
    for (int l = 0; l < L; ++l) oss << "D"           << l << ",";
    oss << "D_total,";
    for (int l = 0; l < L; ++l) oss << "T"           << l << ",";
    oss << "Variance";
    return oss.str();
}

ofstream open_csv(const string& path, int L, CsvMode mode) {
    string expected = make_csv_header(L);

    if (mode == CsvMode::APPEND && filesystem::exists(path)) {
        ifstream fin(path);
        string first_line;
        if (getline(fin, first_line)) {
            if (!first_line.empty() && first_line.back() == '\r')
                first_line.pop_back();

            auto normalize = [](const string& line) {
                auto trim = [](const string& s) {
                    size_t a = s.find_first_not_of(" \t");
                    if (a == string::npos) return string{};
                    size_t b = s.find_last_not_of(" \t");
                    return s.substr(a, b - a + 1);
                };
                stringstream ss(line); string tok, res; bool first = true;
                while (getline(ss, tok, ',')) {
                    if (!first) res += ",";
                    res += trim(tok); first = false;
                }
                return res;
            };

            if (normalize(first_line) != normalize(expected))
                throw runtime_error("表头不匹配，无法追加！\n"
                    "  期望表头: " + expected + "\n  文件表头: " + first_line);
        }
        fin.close();
        ofstream fout(path, ios::app);
        if (!fout) throw runtime_error("无法打开文件（追加）: " + path);
        cout << "[CSV] 追加模式  →  " << path << "\n";
        return fout;
    } else {
        ofstream fout(path, ios::out | ios::trunc);
        if (!fout) throw runtime_error("无法创建文件: " + path);
        fout << expected << "\n";
        cout << (mode == CsvMode::APPEND ? "[CSV] 追加模式（新建）→  " : "[CSV] 覆盖模式  →  ")
             << path << "\n";
        return fout;
    }
}

void write_csv_row(ofstream& f, const Result& r) {
    int L = r.L;
    for (int l = 0; l < L; ++l) f << r.R[l]             << ",";
    for (int l = 0; l < L; ++l) f << r.N[l]          << ",";
    for (int l = 0; l < L; ++l) f << r.nmpdu_sld[l]  << ",";
    f << r.BAW << ",";
    for (int l = 0; l < L; ++l) f << r.best_nmpdu[l] << ",";
    for (int l = 0; l < L; ++l) f << r.D[l]          << ",";
    f << r.D_mlo << ",";
    for (int l = 0; l < L; ++l) f << r.T[l]          << ",";
    f << r.variance << "\n";
}

// ═══════════════════════════════════════════════════════════════════════════════
//  main
// ═══════════════════════════════════════════════════════════════════════════════
int main() {
    const int  L = NUM_LINKS;
    Config     cfg;
    ScanConfig scan_cfg;

    scan_cfg.N_values.resize(L);
    scan_cfg.nmpdu_sld_values.resize(L);
    scan_cfg.R_values.resize(L);

    ofstream summary_csv;
    try {
        summary_csv = open_csv(csv_file, L, CSV_MODE);
    } catch (const exception& e) {
        cerr << "[错误] " << e.what() << endl;
        return 1;
    }

    cout << "========== 开始参数扫描（" << L << " 链路 / 连续 AMPDU / P1 方差目标）==========\n";
    cout << "[模式] " << (CSV_MODE == CsvMode::APPEND ? "追加" : "覆盖") << "\n";
    cout << "[连续步长] " << CONT_STEP << " 帧\n";
    for (int l = 0; l < L; ++l) {
        cout << "Link" << l << " N=" << scan_cfg.N_values[l] << "  nmpdu_sld=";
        for (int v : scan_cfg.nmpdu_sld_values[l]) cout << v << " ";
        cout << "  R(" << scan_cfg.R_values[l].size() << " values)\n";
    }
    cout << "BAW: "; for (int v : scan_cfg.BAW_values) cout << v << " "; cout << "\n";

    long long total = 1;
    for (int l = 0; l < L; ++l) total *= (long long)scan_cfg.nmpdu_sld_values[l].size();
    for (int l = 0; l < L; ++l) total *= (long long)scan_cfg.R_values[l].size();
    total *= (long long)scan_cfg.BAW_values.size();
    cout << "Total combinations: " << total << "\n";
    cout << "=================================================\n";

    long long      cur = 0;
    vector<double> R_cur(L);
    vector<int>    sld_cur(L);

    function<void(int)> scan_links = [&](int l) {
        if (l == L) {
            vector<double>              tau_F(L);
            vector<pair<double,double>> p_vec(L);

            for (int i = 0; i < L; ++i) {
                tau_F[i] = (cfg.T_RTS[i] + cfg.T_SIFS[i] + cfg.aifsn * cfg.sigma) / cfg.sigma;
                p_vec[i] = solve_p(scan_cfg.N_values[i], cfg.K, cfg.W);
            }

            for (int BAW : scan_cfg.BAW_values) {
                ++cur;
                cout << "[" << cur << "/" << total << "] ";
                for (int i = 0; i < L; ++i) cout << "R" << i+1 << "=" << R_cur[i] << " ";
                cout << "sld=(";
                for (int i = 0; i < L; ++i) cout << sld_cur[i] << (i < L-1 ? "," : "");
                cout << ") BAW=" << BAW << endl;

                try {
                    vector<int32_t> nmpdu_sld(sld_cur.begin(), sld_cur.end());
                    vector<double>  R_vec(R_cur.begin(), R_cur.end());

                    Result result = process_BAW(BAW, cfg,
                                               scan_cfg.N_values,
                                               nmpdu_sld,
                                               p_vec, tau_F, R_vec);
                    write_csv_row(summary_csv, result);
                } catch (const exception& e) {
                    cerr << "Error: " << e.what() << endl;
                }
            }
            return;
        }

        for (double R : scan_cfg.R_values[l]) {
            R_cur[l] = R;
            for (int s : scan_cfg.nmpdu_sld_values[l]) {
                sld_cur[l] = s;
                scan_links(l + 1);
            }
        }
    };

    scan_links(0);

    summary_csv.close();
    cout << "========== 扫描完成 ==========\n";
    cout << "结果保存至: " << csv_file << endl;
    return 0;
}