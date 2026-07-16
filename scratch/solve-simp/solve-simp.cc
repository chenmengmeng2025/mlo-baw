#include <bits/stdc++.h>
#include <filesystem>
using namespace std;

// ============================================================================
// Corresponding to Problem P1 (Section II-C / Section III-B)
//
// Objective:
//      min_{M_A^(l,M)}
//          (1/L) Σ_{l=1}^{L} ( T̄^(l,M) − (1/L)Σ_j T̄^(j,M) )²
//
// Subject to:
//      Σ_{l=1}^{L} M_A^(l,M) = W_BA
//
// where M_A^(l,M) denotes the maximum A-MPDU aggregation size assigned to
// link l and W_BA is the shared Block ACK window size.
//
// By expressing the average successful transmission period as
//
//      T̄^(l,M) = σ(A^(l) + B^(l)M_A^(l,M)),
//
// where
//
//      B^(l) = (L_P + L_MH + 80)/(R^(l)σ),
//
// and
//
//              1 + τ_F^(l)(1-p^(l,M))
//            + n^(l,S)(τ_T^(l,S)-τ_F^(l))p^(l,S)
//              (1-(p^(l,M))^(1/n^(l,S)))
// A^(l) = ------------------------------------- + T_OH^(l).
//          p^(l,M)-p^(l,S)(p^(l,M))^(1/n^(l,S))
//
// When n^(l,S)=0 (interference-free case),
//
//      A^(l) = (W^(l,M)+1)/2 + T_OH^(l).
//
// The optimization problem becomes a constrained linear allocation problem.
// Airtime alignment is achieved by equalizing T̄^(l,M) among all active links,
// thereby mitigating the BAW stall caused by the shared Block ACK mechanism.
//
// The resulting closed-form solution is
//
//      M_A^(l,M),* = (T*/σ − A^(l)) / B^(l)
//
//      T*/σ = (W_BA + Σ_l A^(l)/B^(l)) / Σ_l (1/B^(l))
//
// If the computed allocation of a link is non-positive, that link is removed
// from the active set and the closed-form solution is recomputed iteratively.
// ============================================================================

// Number of links.
// Change NUM_LINKS from 2 to 3 to switch between the two-link and three-link
// scenarios considered in the paper.
static constexpr int NUM_LINKS = 2;
std::filesystem::path filepath = __FILE__;
static string csv_name = "solve-" + to_string(NUM_LINKS) + "link" + ".csv";
const std::string csvpath = (filepath.parent_path() / csv_name).string();

// CSV output mode
enum class CsvMode
{
    OVERWRITE,
    APPEND
};
static constexpr CsvMode CSV_MODE = CsvMode::APPEND;

// ---------------------------------------------------------------------------
// Parameter scanning configuration used for numerical evaluation.
//
// Each vector corresponds to one system parameter investigated in the paper,
// including
//      n^(l,S)      : number of coexisting SLDs,
//      M_A^(l,S)    : SLD aggregation size,
//      W_BA         : shared Block ACK window,
//      R^(l)        : PHY transmission rates (Mbps),
//      PER          : packet error rate.
// ---------------------------------------------------------------------------
struct ScanConfig
{
    vector<int> N_values;
    vector<vector<int>> nmpdu_sld_values;
    vector<int> BAW_values = {256};
    vector<vector<double>> R_values;
    vector<vector<double>> per;

    ScanConfig(int L)
    {
        N_values.resize(L, 0);
        nmpdu_sld_values.resize(L, vector<int>{0});
        R_values.resize(L, vector<double>{100.0});
        per.resize(L, vector<double>{0.0});

        // Default configuration corresponding to the simulation setup
        // presented in Fig. 4 of the paper.
        //
        // Two-link Wi-Fi 7 network:
        //      Link 1 : four coexisting SLDs
        //      Link 2 : one coexisting SLD
        //
        // SLD aggregation sizes are fixed as
        //      M_A^(1,S)=24,
        //      M_A^(2,S)=232,
        // with W_BA = 256.
        if (L == 2)
        {
            N_values = {4, 1};
            nmpdu_sld_values = {{24}, {232}};
            BAW_values = {256};
            R_values = {{206.470592,
                         229.411766,
                         258.088236,
                         275.29412,
                         286.764706,
                         309.705884,
                         344.117648,
                         412.94118,
                         458.823532,
                         516.176472},
                        {1080.882354}};
        }
    }
};

// ---------------------------------------------------------------------------
// System parameters following IEEE 802.11be and the simulation settings
// adopted in the paper.
// ---------------------------------------------------------------------------
struct Config
{
    double sigma = 9.0; // Slot duration σ (μs)
    double L_subf =
        (1500 + 62 + 10) * 8.0; // Number of transmitted bits contained in one MPDU, including
                                // payload, MAC header and aggregation overhead.
    double L_P = 1500.0 * 8.0;  // Payload size (bits)
    double T_PH_D = 56.0;       // PHY preamble duration for data frames

    // Control frame transmission durations
    vector<double> T_RTS; // T_RTS = T_{PH,C} + L_R / R_B^{(l)}
    vector<double> T_CTS; // T_CTS = T_{PH,C} + L_C / R_B^{(l)}

    // Inter-frame spaces
    vector<double> T_SIFS;
    vector<double> T_DIFS;

    // EDCA contention parameters
    vector<int> K_mld;    // K^{(l,M)}
    vector<int> K_sld;    // K^{(l,S)}
    vector<double> W_mld; // W^{(l,M)}
    vector<double> W_sld; //  W^{(l,S)}

    Config(int L)
    {
        T_RTS.resize(L, 24.0);
        T_CTS.resize(L, 28.0);
        T_SIFS.resize(L, 16.0);

        K_mld.resize(L, 6);
        K_sld.resize(L, 6);
        W_mld.resize(L, 16.0);
        W_sld.resize(L, 16.0);
        T_DIFS.resize(L, 0.0);
    }
};

// ---------------------------------------------------------------------------
// Result container storing the optimal MPDU allocation together with the
// corresponding analytical performance metrics.
// ---------------------------------------------------------------------------
struct Result
{
    int L;
    vector<double> R;
    vector<int> N;
    int BAW;
    vector<int32_t> nmpdu_sld;
    vector<int32_t>
        best_nmpdu;        // Optimal MPDU allocation obtained by airtime alignment M_A^{(l,M),*}
    double D_mlo;          // Total MLO throughput
    vector<double> D;      // Per-link throughput
    vector<double> T;      // Average successful transmission period T̄^(l,M)
    double variance;       // Variance of T̄^(l,M), measuring the degree of airtime alignment
    vector<double> p;      // Fixed-point success probability p_A^(l,M)
    vector<double> alpha;  // Steady-state idle probability α^(l)
    vector<double> lambda; // Steady-state throughput λ_out^(l,M)

    // Single-Link Upper Bound (SL-UB)
    double D_slo;
    double lambda_slo;

    vector<double> per;
};

// ============================================================================
// Compute the steady-state idle probability α^(l) (Eq. (9)).
// ============================================================================
double
compute_alpha(double pM, double pS, double tau_F, double tau_T_M, double tau_T_S, int N)
{
    double pM_pow = pow(pM, 1.0 / N);
    double denom = 1.0 + tau_F + (tau_T_M - tau_F) * pM + N * (tau_T_S - tau_F) * pS -
                   (tau_T_M + N * (tau_T_S - tau_F)) * pS * pM_pow;
    return 1.0 / denom;
}

// ============================================================================
// Compute the common backoff-related term appearing in the fixed-point
// equations (Eq. (11)-(12)).
// ============================================================================
double
calc_p_part(double p, double W, int K)
{
    double numerator = 4.0 * (2.0 * p - 1.0);
    double denominator = W * (2.0 * p - pow(2.0 - 2.0 * p, K + 1));
    if (abs(denominator) < 1e-15)
        return 0.0;
    return numerator / denominator;
}

// ============================================================================
// Section III-A-3: Solve the fixed-point equations (Eq. (11)-(12)).
//
// This function computes the conditional success probabilities
//
//      p_A^(l,M) : MLD success probability,
//      p_A^(l,S) : SLD success probability,
//
// by iteratively solving the coupled nonlinear equations derived from the
// IEEE 802.11 contention model.
// ============================================================================
pair<double, double>
solve_p(int N,
        int K_mld,
        int K_sld,
        double W_mld,
        double W_sld,
        int max_iter = 2000,
        double tol = 1e-12,
        double damping = 0.4)
{
    // No competing SLD exists. The MLD always succeeds in transmission.
    if (N <= 0)
        return {1.0, 0.0};

    const double eps = 1e-14;
    double pM = 0.9, pS = 0.9;

    // Solve the coupled fixed-point equations using successive iteration.
    // A damping factor is applied to improve numerical stability and
    // accelerate convergence.
    for (int iter = 0; iter < max_iter; ++iter)
    {
        // Compute the backoff-related terms of the MLD and SLD.
        double pS_part = calc_p_part(pS, W_sld, K_sld);
        double pM_part = calc_p_part(pM, W_mld, K_mld);

        // Update the MLD conditional success probability.
        double pM_new_raw = pow(max(0.0, 1.0 - pS_part), N);
        double pM_new = (1.0 - damping) * pM + damping * pM_new_raw;
        pM_new = clamp(pM_new, eps, 1.0 - eps);

        // Update the SLD conditional success probability.
        double pS_cand = (1.0 - pM_part) *
                         pow(max(0.0, 1.0 - pS_part), N - 1);
        double pS_new = (1.0 - damping) * pS + damping * pS_cand;
        pS_new = clamp(pS_new, eps, 1.0 - eps);

        // Stop when both success probabilities converge.
        if (fabs(pM_new - pM) < tol && fabs(pS_new - pS) < tol)
        {
            return {pM_new, pS_new};
        }

        pM = pM_new;
        pS = pS_new;
    }

    // Return the last iteration if the maximum iteration count is reached.
    return {pM, pS};
}

// ============================================================================
// Compute the steady-state throughput of the MLD.
//
// According to the analytical model, this function evaluates
//      λ_out^(l,M) : steady-state throughput of the MLD,
// together with the steady-state idle probability α^(l).
// ============================================================================
tuple<double, double>
compute_lambda(int n,
               double pM,
               double pS,
               const vector<double>& tau_T,
               double tau_F,
               double W_mld)
{
    // No competing SLD exists. According to Eq. (6), every transmission
    // cycle consists of one average backoff period followed by one
    // successful A-MPDU transmission.
    if (n == 0)
        return {1 / ((W_mld + 1.0) / 2.0 + tau_T[0]), 1.0};

    // Compute the steady-state idle probability α^(l) using Eq. (7).
    double alpha = compute_alpha(pM, pS, tau_F, tau_T[0], tau_T[1], n);

    // Compute the aggregate SLD throughput and the MLD throughput
    // according to Eq. (8).
    double lambdaM = (1.0 - pS * pow(pM, 1.0 / n - 1.0)) * alpha * pM;

    return {lambdaM, alpha};
}

// ============================================================================
// Compute the effective data throughput.
//
// According to the analytical model, the throughput is obtained from the
// steady-state successful transmission rate λ_out by accounting for the
// packet error rate (PER) and the payload size.
// ============================================================================
double
get_throughput(double PL,
               double sigma,
               double lambda,
               double nmpdu,
               double per = 0.0)
{
    // No MPDU is allocated to the current link.
    if (nmpdu < 1e-9)
        return 0.0;

    // Compute the effective throughput according to Eq. (13).
    double tp = lambda * (1.0 - per) * PL / sigma;

    // The analytical model should never produce a negative throughput.
    if (tp < 0)
        throw runtime_error("Invalid throughput: " + to_string(tp));

    return tp;
}

// ============================================================================
// Compute the duration of one successful transmission τ_T^(l) (Eq. (3)).
// ============================================================================
double
calc_tau_T(double nmpdu, double L_subf, double rate, double sigma, double T_OH)
{
    return (nmpdu < 1e-9 ? 0.0 : ((nmpdu * L_subf) / rate)) / sigma + T_OH;
}

// ============================================================================
// Get the transmission duration of the Block ACK (BA) frame.
// T_{BA} = T_{PH,C} + L_{BA} / R_{B}^{(l)}
// ============================================================================
double
get_T_BA(int BAW)
{
    return (BAW <= 256) ? 40.0 : (BAW <= 512) ? 52.0 : 72.0;
}

// ============================================================================
// Compute the airtime variance among all active links.
//
// The average successful transmission period T̄^(l,M) is first calculated
// for every link, after which the variance is evaluated as
//      (1/L) Σ_l (T̄^(l,M) − T̄_avg)^2.
// This is exactly the optimization objective of Problem P1.
// ============================================================================
double
calc_variance(const vector<double>& T)
{
    int L = (int)T.size();
    if (L == 0)
        return 0.0;
    double mean = 0.0;
    for (double t : T)
        mean += t;
    mean /= L;
    double var = 0.0;
    for (double t : T)
        var += (t - mean) * (t - mean);
    return var / L;
}

// Link-specific analytical performance metrics.
struct LinkResult
{
    double lambdaM;   // Steady-state successful transmission rate λ_out^(l,M)
    double D;         // Effective throughput D^(l,M)
    double T;         // Average successful transmission period T̄^(l,M)
    double alpha;     // Steady-state idle probability α^(l)
};

// ============================================================================
// Evaluate the analytical performance of one MLO link.
//
// This function computes the analytical performance metrics of the current
// link, including the successful transmission rate, throughput, average
// transmission period, and steady-state idle probability.
// ============================================================================
LinkResult
calc_link(int link_idx,
          int32_t n,
          int32_t nmpdu_sld_l,
          double pM,
          double pS,
          int N,
          int BAW,
          const Config& cfg,
          const vector<double>& tau_F_vec,
          const vector<double>& R,
          double per = 0.0)
{
    double T_BA_mld = get_T_BA(BAW);
    double T_BA_sld = get_T_BA(BAW);

    double T_OH_mld = (cfg.T_SIFS[link_idx] * 3 + T_BA_mld + cfg.T_DIFS[link_idx] +
                       cfg.T_RTS[link_idx] + cfg.T_CTS[link_idx] + cfg.T_PH_D) /
                      cfg.sigma;
    double T_OH_sld = (cfg.T_SIFS[link_idx] * 3 + T_BA_sld + cfg.T_DIFS[link_idx] +
                       cfg.T_RTS[link_idx] + cfg.T_CTS[link_idx] + cfg.T_PH_D) /
                      cfg.sigma;

    // Compute the successful transmission durations τ_T^(l) of the MLD
    // and the competing SLD.
    vector<double> tau_T = {calc_tau_T(n, cfg.L_subf, R[link_idx], cfg.sigma, T_OH_mld),
                            calc_tau_T(nmpdu_sld_l, cfg.L_subf, R[link_idx], cfg.sigma, T_OH_sld)};

    // Compute the steady-state successful transmission rate λ_out^(l,M)
    // and the idle probability α^(l).
    auto [lambdaM, alpha] =
        compute_lambda(N, pM, pS, tau_T, tau_F_vec[link_idx], cfg.W_mld[link_idx]);

    // Compute the effective throughput and the average successful
    // transmission period of the current link.
    double D = get_throughput(cfg.L_P * n, cfg.sigma, lambdaM, n, per);
    double T = cfg.sigma / (lambdaM * (1 - per));
    return {lambdaM, D, T, alpha};
}

// Linear coefficients of the airtime model.
struct LinearCoef
{
    double A;    // Fixed airtime component A^(l)
    double B;    // Incremental airtime coefficient B^(l)
};

// ============================================================================
// Compute the coefficients A^(l) and B^(l) in the linear airtime model.
// ============================================================================
LinearCoef
calc_AB(int link_idx,
        int32_t nmpdu_sld_l,
        double pM,
        double pS,
        int N,
        int BAW,
        const Config& cfg,
        const vector<double>& tau_F_vec,
        const vector<double>& T_DIFS,
        const vector<double>& R)
{
    double T_BA_mld = get_T_BA(BAW);
    double T_OH_mld = (cfg.T_SIFS[link_idx] * 3 + T_BA_mld + T_DIFS[link_idx] +
                       cfg.T_RTS[link_idx] + cfg.T_CTS[link_idx] + cfg.T_PH_D) /
                      cfg.sigma;

    double B_l = cfg.L_subf / (R[link_idx] * cfg.sigma);

    if (N <= 0)
    {
        // No competing SLD exists. The average successful transmission period
        // reduces to one average backoff period plus the protocol overhead.
        double A_l = (cfg.W_mld[link_idx] + 1.0) / 2.0 + T_OH_mld;
        return {A_l, B_l};
    }

    // Competing SLD exists. Compute the fixed airtime coefficient A^(l) 
    // according to the analytical model.
    double T_BA_sld = get_T_BA(BAW);
    double T_OH_sld = (cfg.T_SIFS[link_idx] * 3 + T_BA_sld + T_DIFS[link_idx] +
                       cfg.T_RTS[link_idx] + cfg.T_CTS[link_idx] + cfg.T_PH_D) /
                      cfg.sigma;
    double tau_T_S = calc_tau_T(nmpdu_sld_l, cfg.L_subf, R[link_idx], cfg.sigma, T_OH_sld);

    double tau_F = tau_F_vec[link_idx];
    double pM_pow = pow(pM, 1.0 / N);
    double delta = pM - pS * pM_pow;

    if (delta <= 1e-15)
    {
        // Degenerate case. A non-positive denominator indicates that the
        // current link is no longer suitable for airtime allocation.
        return {numeric_limits<double>::max() / 2.0, B_l};
    }

    double numerator = 1.0 + tau_F * (1.0 - pM) + N * (tau_T_S - tau_F) * pS * (1.0 - pM_pow);
    double A_l = numerator / delta + T_OH_mld;

    return {A_l, B_l};
}

// ============================================================================
// Solve the airtime alignment problem using the closed-form solution.
// This function computes the optimal MPDU allocation for all links 
// while satisfying the shared Block ACK window constraint.
// ============================================================================
vector<int32_t>
solve_closed_form(const vector<LinearCoef>& coef, int BAW)
{
    int L = (int)coef.size();
    vector<bool> active(L, true);
    vector<int32_t> n_alloc(L, 0);

    while (true)
    {
        double sum_inv_B = 0.0, sum_A_over_B = 0.0;
        int num_active = 0;
        for (int l = 0; l < L; ++l)
        {
            if (!active[l])
                continue;
            sum_inv_B += 1.0 / coef[l].B;
            sum_A_over_B += coef[l].A / coef[l].B;
            ++num_active;
        }

        if (num_active == 0)
            break;

        // Compute the optimal common transmission period T*/σ.
        double T_star_over_sigma = (BAW + sum_A_over_B) / sum_inv_B;

        bool any_negative = false;
        vector<double> real_alloc(L, 0.0);

        // Compute the continuous MPDU allocation of each active link.
        for (int l = 0; l < L; ++l)
        {
            if (!active[l])
                continue;
            real_alloc[l] = (T_star_over_sigma - coef[l].A) / coef[l].B;
            // Links receiving a non-positive allocation are removed from
            // the active set, and the closed-form solution is recomputed.
            if (real_alloc[l] <= 0.5)
            {
                active[l] = false;
                any_negative = true;
            }
        }

        // Resolve the reduced optimization problem.
        if (any_negative)
            continue;

        // Convergence: All activation link assignments are positive, perform 
        // integer rounding and correct the rounding error
        int32_t total = 0;
        for (int l = 0; l < L; ++l)
        {
            if (active[l])
            {
                n_alloc[l] = (int32_t)llround(real_alloc[l]);
                // Ensure that every active link is assigned at least one MPDU.
                if (n_alloc[l] < 1)
                    n_alloc[l] = 1;
                total += n_alloc[l];
            }
            else
            {
                n_alloc[l] = 0;
            }
        }

        // Compensate for the rounding error so that
        //
        //      Σ_l M_A^(l,M) = W_BA.
        //
        // The adjustment is applied to the link with the smallest B^(l),
        // which has the smallest marginal impact on the objective.
        int32_t diff = BAW - total;
        if (diff != 0)
        {
            int adjust_idx = -1;
            double best_B = numeric_limits<double>::max();
            for (int l = 0; l < L; ++l)
            {
                if (active[l] && coef[l].B < best_B)
                {
                    best_B = coef[l].B;
                    adjust_idx = l;
                }
            }
            if (adjust_idx >= 0)
            {
                n_alloc[adjust_idx] += diff;
                if (n_alloc[adjust_idx] < 1)
                    n_alloc[adjust_idx] = 1; // Safety check.
            }
        }
        break;
    }

    return n_alloc;
}

// ============================================================================
// Evaluate the analytical performance for one Block ACK window size.
//
// This function performs the complete analytical evaluation of the proposed
// airtime alignment scheme for a given Block ACK window size and returns
// all performance metrics required by the numerical evaluation.
// ============================================================================
Result
process_BAW(int L,
            int BAW,
            const Config& cfg,
            const vector<int>& N_vec,
            const vector<int32_t>& nmpdu_sld,
            const vector<pair<double, double>>& p_vec,
            const vector<double>& tau_F,
            const vector<double>& R,
            const vector<double>& per_vec)
{
    vector<double> pM(L), pS(L);
    for (int i = 0; i < L; ++i)
    {
        pM[i] = p_vec[i].first;
        pS[i] = p_vec[i].second;
    }

    // Compute the linear airtime coefficients A^(l) and B^(l) used by
    // the closed-form airtime alignment algorithm.
    vector<LinearCoef> coef(L);
    for (int l = 0; l < L; ++l)
    {
        coef[l] = calc_AB(l, nmpdu_sld[l], pM[l], pS[l], N_vec[l], BAW, cfg, tau_F, cfg.T_DIFS, R);
    }

    // Solve the airtime alignment problem and obtain the optimal integer
    // MPDU allocation under the shared Block ACK window constraint.
    vector<int32_t> best_n = solve_closed_form(coef, BAW);

    // Re-evaluate every active link using the complete analytical model.
    vector<double> best_D(L, 0), best_T(L, 0), best_alpha(L, 0), best_lambda(L, 0);
    vector<double> active_T;
    for (int l = 0; l < L; ++l)
    {
        if (best_n[l] > 0)
        {
            auto res = calc_link(l,
                                 best_n[l],
                                 nmpdu_sld[l],
                                 pM[l],
                                 pS[l],
                                 N_vec[l],
                                 BAW,
                                 cfg,
                                 tau_F,
                                 R,
                                 per_vec[l]);
            best_D[l] = res.D;
            best_T[l] = res.T;
            best_alpha[l] = res.alpha;
            best_lambda[l] = res.lambdaM;
            active_T.push_back(res.T);
        }
    }
    double min_var = calc_variance(active_T);

    // Evaluate the SLO baseline, where the entire Block ACK window is
    // allocated to a single link.
    vector<double> D_slo_vec(L), lambda_slo_vec(L);
    for (int l = 0; l < L; ++l)
    {
        try
        {
            auto res = calc_link(l,
                                 BAW,
                                 nmpdu_sld[l],
                                 pM[l],
                                 pS[l],
                                 N_vec[l],
                                 BAW,
                                 cfg,
                                 tau_F,
                                 R,
                                 per_vec[l]);
            D_slo_vec[l] = res.D;
            lambda_slo_vec[l] = res.lambdaM;
        }
        catch (...)
        {
            D_slo_vec[l] = 0.0;
            lambda_slo_vec[l] = 0.0;
        }
    }

    // Select the best-performing SLO link for comparison.
    int best_slo_idx = (int)(max_element(D_slo_vec.begin(), D_slo_vec.end()) - D_slo_vec.begin());

    // Assemble all analytical results for the current operating point.
    Result res;
    res.L = L;
    res.R = R;
    res.N = N_vec;
    res.BAW = BAW;
    res.nmpdu_sld = nmpdu_sld;
    res.best_nmpdu = best_n;
    res.D = best_D;
    res.T = best_T;
    res.variance = min_var;
    res.p = pM;
    res.alpha = best_alpha;
    res.lambda = best_lambda;
    res.D_mlo = 0.0;
    for (double d : best_D) // Compute the aggregate MLO throughput.
        res.D_mlo += d;
    res.D_slo = D_slo_vec[best_slo_idx];
    res.lambda_slo = lambda_slo_vec[best_slo_idx];
    res.per = per_vec;
    return res;
}

string
make_csv_header(int L)
{
    ostringstream oss;
    for (int l = 0; l < L; ++l)
        oss << "R" << l << ",";
    for (int l = 0; l < L; ++l)
        oss << "nsld" << l << ",";
    for (int l = 0; l < L; ++l)
        oss << "ampdunumsld" << l << ",";
    for (int l = 0; l < L; ++l)
        oss << "fixedPER" << l << ",";
    oss << "bawsize,";
    for (int l = 0; l < L; ++l)
        oss << "maxampdunum" << l << ",";
    for (int l = 0; l < L; ++l)
        oss << "D" << l << ",";
    oss << "D_total,";
    for (int l = 0; l < L; ++l)
        oss << "T" << l << ",";
    oss << "Variance,";
    for (int l = 0; l < L; ++l)
        oss << "p" << l << ",";
    for (int l = 0; l < L; ++l)
        oss << "alpha" << l << ",";
    for (int l = 0; l < L; ++l)
        oss << "lambda" << l << ",";
    oss << "D_slo,lambda_slo";
    return oss.str();
}

ofstream
open_csv(const string& path, int L, CsvMode mode)
{
    string expected = make_csv_header(L);

    if (mode == CsvMode::APPEND && filesystem::exists(path))
    {
        ifstream fin(path);
        string first_line;
        if (getline(fin, first_line))
        {
            if (!first_line.empty() && first_line.back() == '\r')
                first_line.pop_back();

            auto normalize_header = [](const std::string& line) -> std::string {
                auto trim = [](const std::string& s) -> std::string {
                    size_t start = s.find_first_not_of(" \t");
                    if (start == std::string::npos)
                        return "";
                    size_t end = s.find_last_not_of(" \t");
                    return s.substr(start, end - start + 1);
                };

                std::stringstream ss(line);
                std::string token;
                std::string result;
                bool first = true;

                while (std::getline(ss, token, ','))
                {
                    if (!first)
                        result += ",";
                    result += trim(token);
                    first = false;
                }

                return result;
            };

            std::string normalized_first_line = normalize_header(first_line);
            std::string normalized_expected = normalize_header(expected);

            if (normalized_first_line != normalized_expected)
            {
                throw std::runtime_error(
                    "CSV header mismatch.\n"
                    "  File header      : " + first_line +
                    "\n  Normalized header: " + normalized_first_line +
                    "\n  Expected header  : " + expected +
                    "\nPlease ensure that NUM_LINKS matches the existing CSV "
                    "file, or delete the old file and rerun the program.");
            }
        }

        fin.close();

        ofstream fout(path, ios::app);
        if (!fout)
            throw runtime_error("Failed to open CSV file for appending: " + path);

        return fout;
    }
    else
    {
        ofstream fout(path, ios::out | ios::trunc);
        if (!fout)
            throw runtime_error("Failed to create CSV file: " + path);

        fout << expected << "\n";

        return fout;
    }
}

void
write_csv_row(ofstream& f, const Result& r)
{
    int L = r.L;
    for (int l = 0; l < L; ++l)
        f << r.R[l] << ",";
    for (int l = 0; l < L; ++l)
        f << r.N[l] << ",";
    for (int l = 0; l < L; ++l)
        f << r.nmpdu_sld[l] << ",";
    for (int l = 0; l < L; ++l)
        f << r.per[l] << ",";
    f << r.BAW << ",";
    for (int l = 0; l < L; ++l)
        f << r.best_nmpdu[l] << ",";
    for (int l = 0; l < L; ++l)
        f << r.D[l] << ",";
    f << r.D_mlo << ",";
    for (int l = 0; l < L; ++l)
        f << r.T[l] << ",";
    f << r.variance << ",";
    for (int l = 0; l < L; ++l)
        f << r.p[l] << ",";
    for (int l = 0; l < L; ++l)
        f << r.alpha[l] << ",";
    for (int l = 0; l < L; ++l)
        f << r.lambda[l] << ",";
    f << r.D_slo << "," << r.lambda_slo << "\n";
}

int
main()
{
    const int L = NUM_LINKS;

    // Initialize the IEEE 802.11 system parameters and the parameter
    // scanning configuration used for the analytical evaluation.
    Config cfg(L);
    ScanConfig scan_cfg(L);

    // Compute DIFS according to the IEEE 802.11 specification:
    //      DIFS = SIFS + 2σ.
    for (int i = 0; i < L; ++i)
    {
        cfg.T_DIFS[i] = cfg.T_SIFS[i] + 2 * cfg.sigma;
    }

    // Solve the fixed-point equations for each link. Since the contention
    // parameters remain unchanged during the parameter scan, the success
    // probabilities only need to be computed once.
    vector<pair<double, double>> global_p_vec(L);
    for (int i = 0; i < L; ++i)
    {
        global_p_vec[i] =
            solve_p(scan_cfg.N_values[i], cfg.K_mld[i], cfg.K_sld[i], cfg.W_mld[i], cfg.W_sld[i]);
    }

    // Create the CSV file for storing all analytical results.
    ofstream summary_csv;
    try
    {
        summary_csv = open_csv(csvpath, L, CSV_MODE);
    }
    catch (const exception& e)
    {
        cerr << "[Error] " << e.what() << endl;
        return 1;
    }

    cout << "========== Parameter Scan Started (" << L
         << " Links / Closed-form Solution / P1 Variance Minimization) ==========" << endl;

    cout << "[CSV Mode] " << (CSV_MODE == CsvMode::APPEND ? "Append" : "Overwrite") << endl;

    // Display the parameter ranges to be evaluated.
    for (int l = 0; l < L; ++l)
    {
        cout << "Link " << l << ": N=" << scan_cfg.N_values[l] << "  SLD_MPDU={";

        for (int v : scan_cfg.nmpdu_sld_values[l])
            cout << v << " ";

        cout << "}  R(" << scan_cfg.R_values[l].size() << " values)"
             << "  PER(" << scan_cfg.per[l].size() << " values)" << endl;
    }

    cout << "BAW values: ";
    for (int v : scan_cfg.BAW_values)
        cout << v << " ";
    cout << endl;

    // Calculate the total number of parameter combinations.
    long long total = 1;
    for (int l = 0; l < L; ++l)
        total *= (long long)scan_cfg.nmpdu_sld_values[l].size();
    for (int l = 0; l < L; ++l)
        total *= (long long)scan_cfg.R_values[l].size();
    for (int l = 0; l < L; ++l)
        total *= (long long)scan_cfg.per[l].size();
    total *= (long long)scan_cfg.BAW_values.size();

    cout << "Total parameter combinations: " << total << endl;
    cout << "=================================================" << endl;

    long long cur = 0;
    vector<double> R_cur(L);
    vector<int> sld_cur(L);
    vector<double> per_cur(L);

    // Recursively enumerate all parameter combinations across multiple links.
    function<void(int)> scan_links = [&](int l) {
        if (l == L)
        {
            vector<double> tau_F(L);

            // Evaluate every configured Block ACK window size.
            for (int BAW : scan_cfg.BAW_values)
            {
                ++cur;

                cout << "[" << cur << "/" << total << "] ";

                for (int i = 0; i < L; ++i)
                    cout << "R" << i + 1 << "=" << R_cur[i] << " ";

                cout << "SLD_MPDU=(";
                for (int i = 0; i < L; ++i)
                    cout << sld_cur[i] << (i < L - 1 ? "," : "");

                cout << ") PER=(";
                for (int i = 0; i < L; ++i)
                    cout << per_cur[i] << (i < L - 1 ? "," : "");

                cout << ") BAW=" << BAW << endl;

                try
                {
                    vector<int32_t> nmpdu_sld(sld_cur.begin(), sld_cur.end());
                    vector<double> R_vec(R_cur.begin(), R_cur.end());

                    // Perform the analytical evaluation and compute the
                    // optimal MPDU allocation using the closed-form solution.
                    Result result = process_BAW(L,
                                                BAW,
                                                cfg,
                                                scan_cfg.N_values,
                                                nmpdu_sld,
                                                global_p_vec,
                                                tau_F,
                                                R_vec,
                                                per_cur);

                    write_csv_row(summary_csv, result);
                }
                catch (const exception& e)
                {
                    cerr << "[Error] " << e.what() << endl;
                }
            }
            return;
        }

        // Traverse the parameter space of the current link.
        for (double R : scan_cfg.R_values[l])
        {
            R_cur[l] = R;

            for (int s : scan_cfg.nmpdu_sld_values[l])
            {
                sld_cur[l] = s;

                for (double p : scan_cfg.per[l])
                {
                    per_cur[l] = p;
                    scan_links(l + 1);
                }
            }
        }
    };

    // Start the recursive parameter scan.
    scan_links(0);

    summary_csv.close();

    cout << "========== Parameter Scan Completed ==========" << endl;
    cout << "Results saved to: " << csvpath << endl;

    return 0;
}