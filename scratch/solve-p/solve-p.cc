#include <cmath>  
#include <vector>
#include <iostream>
#include <fstream>  // 用于文件操作
int max_iter = 100000;       // 扫描迭代次数，可调，增大可缩小步长

// q_i^(l,g) = 2 / (1 + W_i^(l,g))
double compute_q(int i, int K) {
    if (i > K) i = K; 
    double Wi = 16 * pow(2.0, i);
    return 2.0 / (1.0 + Wi);
}

double compute_alpha(double pM, double pS, double tau_F, 
                     double tau_T_M, double tau_T_S)
{
    double ln_pM = log(pM);
    double denom = 1.0 + tau_F 
                   + (tau_T_M - tau_F) * pM 
                   - tau_T_M * pS 
                   - (tau_T_S - tau_F) * pS * ln_pM;
    return 1.0 / denom;
}

double compute_alpha_fixed(double pM, double pS, double tau_F,
                           double tau_T_M, double tau_T_S, int N)
{
    double pM_pow = std::pow(pM, 1.0 / N);

    double denom =
        1.0
        + tau_F
        + (tau_T_M - tau_F) * pM
        + N * (tau_T_S - tau_F) * pS
        - (tau_T_M + N * (tau_T_S - tau_F)) * pS * pM_pow;

    return 1.0 / denom;
}

// ---- Main function: scan pM for a fixed pS -----
double solve_pM_for_fixed_pS(
        double pS,
        int N,
        int K,
        double tau_F,
        double tau_T_M,
        double tau_T_S)
{
    double sum_S1 = 0.0;
    for (int i = 0; i < K; i++)
        sum_S1 += pow(1 - pS, i) * pS / compute_q(i, K);

    // final term (1-pS)^K / q_K
    double sum_S2 = pow(1 - pS, K) / compute_q(K, K);

    // ----- Scan pM over [1e-5, 0.99999] -----
    double best_pM = 0;
    double best_err = 1e9;

    double pM_min = 1e-5;        
    double pM_max = 0.99999999;   
    double step = (pM_max - pM_min) / max_iter;

    for (int iter = 0; iter <= max_iter; iter++) 
    {
        double pM = pM_min + iter * step;
        // double alpha_val = compute_alpha_fixed(pM, pS, tau_F, tau_T_M, tau_T_S , N);
        // double denom_S = alpha_val * ( pS * tau_T_S + tau_F * (1 - pS) ) + sum_S1 + sum_S2;
        double denom_S = sum_S1 + sum_S2; // 对于SLD简化

        double RHS = pow(1.0 - 1.0 / denom_S, N);
        // double RHS = exp(- N / denom);   // 大n值近似

        double err = fabs(RHS - pM);
        if (err < best_err) {
            best_err = err;
            best_pM = pM;
        }
    }
    return best_pM;
}

double solve_pM_for_fixed_pS_simple(
        double pS,
        int N,
        int K,
        double tau_F,
        double tau_T_M,
        double tau_T_S)
{
    double sum1 = 0.0;
    for (int i = 0; i < K; i++)
        sum1 += pow(1 - pS, i) * pS / compute_q(i, K);

    // final term (1-pS)^K / q_K
    double sum2 = pow(1 - pS, K) / compute_q(K, K);

    // ----- Scan pM over [1e-5, 0.99999] -----
    double best_pM = 0;
    double best_err = 1e9;

    double pM_min = 1e-5;        
    double pM_max = 0.99999999;   
    double step = (pM_max - pM_min) / max_iter;

    for (int iter = 0; iter <= max_iter; iter++) 
    {
        double pM = pM_min + iter * step;
        double denom = sum1 + sum2;
        double RHS = exp(- N / denom);

        double err = fabs(RHS - pM);
        if (err < best_err) {
            best_err = err;
            best_pM = pM;
        }
    }
    return best_pM;
}

double solve_pS_for_fixed_pM(
        double pM,
        int N,
        int K,
        double tau_F,
        double tau_T_M,
        double tau_T_S)
{
    double sum_M1 = 0.0;
    for (int i = 0; i < K; i++)
        sum_M1 += pow(1 - pM, i) * pM / compute_q(i, K);

    double sum_M2 = pow(1 - pM, K) / compute_q(K, K);

    // ----- Scan pS over [1e-5, 0.99999] -----
    double best_pS = 0;
    double best_err = 1e9;

    double pS_min = 1e-5;          // pM最小值
    double pS_max = 0.99999999;    // pM最大值
    double step = (pS_max - pS_min) / max_iter; // 自动计算步长

    for (int iter = 0; iter <= max_iter; iter++) 
    {
        double pS = pS_min + iter * step;

        // double alpha_val = compute_alpha_fixed(pM, pS, tau_F, tau_T_M, tau_T_S , N);
        // double denom_M = alpha_val * ( pM * tau_T_M + tau_F * (1 - pM) ) + sum_M1 + sum_M2;
        double denom_M = sum_M1 + sum_M2; // 对于MLD简化

        double sum_S1 = 0.0;
        for (int i = 0; i < K; i++)
            sum_S1 += pow(1 - pS, i) * pS / compute_q(i, K);
        double sum_S2 = 0.0;
        sum_S2 = pow(1 - pS, K) / compute_q(K, K);
        // double denom_S = alpha_val * ( pS * tau_T_S + tau_F * (1 - pS) ) + sum_S1 + sum_S2;
        double denom_S = sum_S1 + sum_S2; // 对于SLD简化

        double RHS = (1.0 - 1.0 / denom_M) * pow(1.0 - 1.0 / denom_S, N - 1);
        // double RHS = (1.0 - 1.0 / denom_M) * exp(-(N - 1)/ denom_S); //大n值近似

        double err = fabs(RHS - pS);
        if (err < best_err) {
            best_err = err;
            best_pS = pS;
        }
    }
    return best_pS;
}

double solve_pS_for_fixed_pM_simple(
        double pM,
        int N,
        int K,
        double tau_F,
        double tau_T_M,
        double tau_T_S)
{
    double sum_M1 = 0.0;
    for (int i = 0; i < K; i++)
        sum_M1 += pow(1 - pM, i) * pM / compute_q(i, K);

    double sum_M2 = pow(1 - pM, K) / compute_q(K, K);

    // ----- Scan pS over [1e-5, 0.99999] -----
    double best_pS = 0;
    double best_err = 1e9;

    double pS_min = 1e-5;          // pM最小值
    double pS_max = 0.99999999;    // pM最大值
    double step = (pS_max - pS_min) / max_iter; // 自动计算步长

    for (int iter = 0; iter <= max_iter; iter++) 
    {
        double pS = pS_min + iter * step;
        double denom_M = sum_M1 + sum_M2;

        double sum_S1 = 0.0;
        for (int i = 0; i < K; i++)
            sum_S1 += pow(1 - pS, i) * pS / compute_q(i, K);
        double sum_S2 = 0.0;
        sum_S2 = pow(1 - pS, K) / compute_q(K, K);
        double denom_S = sum_S1 + sum_S2;

        double RHS = (1.0 - 1.0 / denom_M) * exp(-(N - 1)/ denom_S);

        double err = fabs(RHS - pS);
        if (err < best_err) {
            best_err = err;
            best_pS = pS;
        }
    }
    return best_pS;
}

int main() {
    std::vector<double> R = {344.118, 2882.353};
    std::vector<double> R_sld = R;

    double sigma = 9.0;
    double L_P = 1500.0 * 8.0;

    std::vector<double> T_PH = {56.0, 56.0};
    std::vector<double> T_RTS = {30, 24};
    std::vector<double> T_CTS = {34, 28};
    std::vector<double> T_SIFS = {10, 16};
    double L_subf_sld = 1572.0 * 8.0;  // 干扰设备SLD
    double L_subf_mld = 1572.0 * 8.0; 
    int N1 = 10;
    int N2 = 10;
    int K1 = 6;
    int K2 = 6;
    int nmpdu_sld = 1;
    if(nmpdu_sld == 1) L_subf_sld = 1570.0 * 8.0;
    std::vector<int32_t> nmpdu_mld = {54, 458};

    std::vector<double> W2 = {16, 16};
    std::vector<double> T_DIFS;
    for (double sifs : T_SIFS) T_DIFS.push_back(sifs + 2 * sigma);
    std::vector<double> tau_F;
    for (size_t i = 0; i < R.size(); ++i)
        tau_F.push_back((T_RTS[i] + T_DIFS[i]) / sigma);


    double BAW = 512;
    double T_BA0; 
    if (BAW <= 256) T_BA0 = 46.0;
    else if (BAW <= 512) T_BA0 = 58.0;  
    else T_BA0 = 78.0;  
    double T_BA1 = T_BA0 - 6.0;

    double T_BA_sld0; 
    double T_BA_sld1;
    if (nmpdu_sld == 1){
        T_BA_sld0 = 34;
        T_BA_sld1 = 28;
    } 
    else{
        T_BA_sld0 = T_BA0;
        T_BA_sld1 = T_BA1; 
    }

    std::vector<double> T_OH_mld;
    for (size_t i = 0; i < R.size(); ++i) {
        double value = (T_PH[0] + T_SIFS[i] * 3 + (!i ? T_BA0 : T_BA1) + T_DIFS[i] + T_RTS[i] + T_CTS[i]) / sigma;
        T_OH_mld.push_back(value);
    }

    std::vector<double> T_OH_sld;
    for (size_t i = 0; i < R.size(); ++i) {
        double value = (T_PH[1] + T_SIFS[i] * 3 + (!i ? T_BA_sld0 : T_BA_sld1) + T_DIFS[i] + T_RTS[i] + T_CTS[i]) / sigma;
        T_OH_sld.push_back(value);
    }

    std::vector<double> tau_T1(2);
    tau_T1[0] = floor(ceil((16 + nmpdu_mld[0] * L_subf_mld + 6)/4680)*13.6 + 6) / sigma + T_OH_mld[0];
    tau_T1[1] = floor(ceil((16 + nmpdu_sld * L_subf_sld + 6)/4680)*13.6 + 6) / sigma + T_OH_sld[0];

    std::vector<double> tau_T2(2);
    tau_T2[0] = floor(ceil((16 + nmpdu_mld[1] * L_subf_mld + 6)/39200)*13.6) / sigma + T_OH_mld[1]; 
    tau_T2[1] = floor(ceil((16 + nmpdu_sld * L_subf_sld + 6)/39200)*13.6) / sigma + T_OH_sld[1];

    std::string outFile1_titl =  "solve_pM_" + 
                                std::to_string(N2) + 
                                // "_" +  std::to_string(nmpdu_mld[1]) + "_" + std::to_string(nmpdu_sld) + 
                                ".csv";
    std::ofstream outFile1(outFile1_titl);
    outFile1 << "pS,pM\n";
    // 计算 pM 对应 pS 的值
    for (double pS = 0.5 + 1e-5; pS <= -0 + 0.99999; pS += 0.00001) {
        double pM = solve_pM_for_fixed_pS(
            pS,
            N2,
            K2,
            tau_F[1],
            tau_T2[0],
            tau_T2[1]); 
        outFile1 << pS << "," << pM << "\n";
    }
    outFile1.close();

    std::string outFile2_titl =  "solve_pS_" + 
                                std::to_string(N2) + 
                                // "_" +  std::to_string(nmpdu_mld[1]) + "_" + std::to_string(nmpdu_sld) + 
                                ".csv";
    std::ofstream outFile2(outFile2_titl);
    outFile2 << "pM,pS\n";
    // 计算 pS 对应 pM 的值
    for (double pM = 0.5 + 1e-5; pM <= -0 + 0.99999; pM += 0.00001) {
        double pS = solve_pS_for_fixed_pM(
            pM,
            N2,
            K2,
            tau_F[1],
            tau_T2[0],
            tau_T2[1]); 
        outFile2 << pM << "," << pS << "\n";
    }
    outFile2.close();

    std::cout << "Data has been written to " << outFile1_titl <<" and " << outFile2_titl << std::endl;
}



