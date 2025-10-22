#include <ostream>
#include <vector>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <iostream>
#include <numeric>
#include <cmath>
#include <fstream>
#include "ns3/command-line.h"
#include <filesystem>

std::vector<double> T_RTS = {34,28};
std::vector<double> T_CTS = {34, 28};

// Calculate T_OH,i, T_BO,i, and T_BO_prime (for TB)
std::tuple<std::vector<double>, std::vector<double>, std::vector<double>> calculateOverheadAndBackoff(
    const std::vector<double>& T_SIFS,
    const std::vector<double>& T_BA,
    const std::vector<double>& T_DIFS,
    const std::vector<int>& CW_min,
    const std::vector<int>& CW_min_tb,
    double T_PH,
    double sigma) {
    
    int M = CW_min.size();
    if (T_SIFS.size() != M || T_BA.size() != M || T_DIFS.size() != M) {
        throw std::invalid_argument("All input vectors must have the same size");
    }

    std::vector<double> T_OH(M);
    std::vector<double> T_BO(M);
    std::vector<double> T_BO_prime(M); // For TB

    for (int i = 0; i < M; ++i) {
        T_OH[i] = (T_PH + T_SIFS[i] * 3 + T_BA[i] + T_DIFS[i] + T_RTS[i] + T_CTS[i]) / sigma;
        T_BO[i] = (CW_min[i] - 1) / 2.0;
        T_BO_prime[i] = (CW_min_tb[i] - 1) / 2.0; // For TB
    }

    return {T_OH, T_BO, T_BO_prime};
}

// Calculate T_ack,i
std::vector<double> calculateT_ack(
    int N,
    int L_subf_tb,
    const std::vector<double>& R,
    double sigma,
    const std::vector<double>& T_OH,
    const std::vector<double>& T_BO_prime) {
    
    int M = R.size();
    std::vector<double> T_ack(M);

    for (int i = 0; i < M; ++i) {
        T_ack[i] = (N * L_subf_tb / R[i]) / sigma + T_OH[i] + T_BO_prime[i];
    }

    return T_ack;
}

// Calculate TtcpBest
std::tuple<double, std::string, bool> calculateTtcpBest(
    int W,
    int L_subf,
    const std::vector<int>& CW_min,
    const std::vector<int>& CW_min_tb,
    const std::vector<double>& R,
    double sigma,
    const std::vector<int>& n_max,
    double T_PH,
    const std::vector<double>& T_SIFS,
    const std::vector<double>& T_BA,
    const std::vector<double>& T_DIFS,
    int L_subf_tb) {
    
    auto [T_OH, T_BO, T_BO_prime] = calculateOverheadAndBackoff(T_SIFS, T_BA, T_DIFS, CW_min, CW_min_tb, T_PH, sigma);
    auto T_ack = calculateT_ack(W, L_subf_tb, R, sigma, T_OH, T_BO_prime);

    int M = CW_min.size();
    if (R.size() != M) {
        throw std::invalid_argument("R vector must have the same size as CW_min");
    }

    // Calculate first term: average time term
    double numerator = W * L_subf;
    double denominator = 0.0;
    std::string terms;

    for (int i = 0; i < M; ++i) {
        numerator += (T_OH[i] + T_BO[i]) * R[i] * sigma;
        denominator += R[i] * sigma;
    }
    // Add T_ack term for the first link
    numerator += T_ack[0] * R[0] * sigma;
    double first_term = numerator / denominator;
    terms += "first_term: " + std::to_string(first_term) + "; ";

    // Calculate second term: min node time term (only for i >= 2)
    double second_term = std::numeric_limits<double>::max();
    terms += "second_term:";
    for (int i = 1; i < M; ++i) { // Start from i=1 as per formula
        double current_term = T_OH[i] + T_BO[i] + (n_max[i] * L_subf) / (R[i] * sigma);
        terms += " " + std::to_string(current_term);
        if (current_term < second_term) {
            second_term = current_term;
        }
    }

    // Determine which term was selected
    double TtcpBest;
    bool useFirstTerm;

    if (first_term < second_term) {
        TtcpBest = first_term;
        useFirstTerm = true;
    } else {
        TtcpBest = second_term;
        useFirstTerm = false;
    }

    return {TtcpBest, terms, useFirstTerm};
}

// Calculate n_iBest for TCP
std::vector<int> calculateN_iBest_TCP(
    double TtcpBest,
    const std::vector<double>& T_OH,
    const std::vector<double>& T_BO,
    const std::vector<double>& R,
    double sigma,
    int L_subf,
    int L_subf_tb,
    const std::vector<int>& n_max,
    std::vector<double> T_BO_prime,
    int W,
    bool sumW) {
    
    int M = T_OH.size();
    std::vector<int> n_iBest(M);
    double total = 0.0;
    std::vector<double> T_ack(M);
    
    if(sumW) {
        T_ack = calculateT_ack(W, L_subf_tb, R, sigma, T_OH, T_BO_prime);

        // Calculate for i=1 (special case)
        double temp1 = ((TtcpBest - T_OH[0] - T_BO[0] - T_ack[0]) * R[0] * sigma) / L_subf;
        n_iBest[0] = std::min(static_cast<int>(std::round(temp1)), n_max[0]);
        total += n_iBest[0];
        
        // Calculate for i>1
        for (int i = 1; i < M; ++i) {
            double temp = ((TtcpBest - T_OH[i] - T_BO[i]) * R[i] * sigma) / L_subf;
            n_iBest[i] = std::min(static_cast<int>(std::round(temp)), n_max[i]);
            total += n_iBest[i];
        }
    }
    else{
        n_iBest[1] = n_max[1];
        double temp1 = (TtcpBest - 2*T_OH[0] - T_BO[0] - T_BO_prime[0]) * R[0] * sigma / (L_subf + L_subf_tb) - n_iBest[1]*L_subf_tb/(L_subf + L_subf_tb);
        n_iBest[0] = std::min(static_cast<int>(std::round(temp1)), n_max[0]);
    }

    
    // Adjust to sum to W if needed
    if(sumW) {
        int remaining = W - total;
        while (remaining > 0) {
            bool adjusted = false;
            
            // Sort indices by rate (descending)
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
            if (!adjusted) break;
        }
    }

    return n_iBest;
}

// Calculate D_tcp_max_mlo
double calculateD_tcpMaxMlo(
    int L_P,
    double TtcpBest,
    double sigma,
    const std::vector<int>& n_iBest) {
    
    double sum = 0.0;
    for (double n : n_iBest) {
        sum += n;
    }
    
    return (L_P / (TtcpBest * sigma)) * sum;
}

// Calculate D_{tcp,i}^{slo} and D_{tcp,max}^{slo}
std::tuple<std::vector<double>, double> calculateD_tcpSlo(
    int W,
    int L_P,
    int L_subf,
    int L_subf_tb,
    const std::vector<double>& R,
    double sigma,
    const std::vector<double>& T_OH,
    const std::vector<double>& T_BO,
    const std::vector<double>& T_BO_prime,
    const std::vector<int>& n_max) {
    
    int M = R.size();
    std::vector<double> D_tcp_i_slo(M);
    
    for (int i = 0; i < M; ++i) {
        // Calculate n_slo,i = min{W, n_max[i]}
        int n_slo_i = std::min(W, n_max[i]);
        
        // Calculate numerator: n_slo_i * L_P
        double numerator = n_slo_i * L_P;
        
        // Calculate denominator:
        //   (n_slo_i * (L_subf + L_subf_tb) / R_i) 
        //   + (2*T_OH[i] + T_BO[i] + T_BO_prime[i]) * sigma
        double denominator = (n_slo_i * (L_subf + L_subf_tb) / R[i]) 
                           + (2*T_OH[i] + T_BO[i] + T_BO_prime[i]) * sigma;
        
        D_tcp_i_slo[i] = numerator / denominator;
    }
    
    // Find maximum value
    double D_tcp_max_slo = *std::max_element(D_tcp_i_slo.begin(), D_tcp_i_slo.end());
    
    return {D_tcp_i_slo, D_tcp_max_slo};
}

int main(int argc, char* argv[]) {
    int L_subf = 3080 * 8;  // bits
    int L_subf_tb = 96 * 8; // bits for TB
    int L_P = 1448 * 8 * 2; // bits
    std::vector<int> CW_min = {16, 16};
    std::vector<int> CW_min_tb = {16, 16}; // For TB
    std::vector<double> R = {344.118, 2882.353}; // Mbps
    double sigma = 9; // us
    double T_PH = 56; // us
    std::vector<double> T_SIFS = {10, 16}; // us
    std::vector<int> n_max = {75, 634};

    // Open output CSV file
    std::string title;
    title = "results_tcp_RTS";
    title += "_cwmin_" + std::to_string(CW_min[0]) + "_r1_" + std::to_string(R[0]) + "_r2_" + std::to_string(R[1]);
    std::ofstream outFile(title + ".csv");
    if (!outFile.is_open()) {
        std::cerr << "Error opening output file!" << std::endl;
        return 1;
    }

    // Write CSV header
    outFile << "W,TtcpBest,isFirstTerm,D_tcp_max_slo,D_tcp_max_mlo,n_iBest_1,n_iBest_2\n";

    // Process W values from 64 to 1024 with step 2
    for (int W = 64; W <= 1024; W += 2) {
        // 根据W值确定T_BA
        std::vector<double> T_BA;
        if (W <= 256) {
            T_BA = {46, 40};  // W ≤ 256
        } else if (W <= 512) {
            T_BA = {58, 52};  // 256 < W ≤ 512
        } else {
            T_BA = {78, 72};  // W > 512
        }

        // Calculate T_DIFS
        std::vector<double> T_DIFS;
        for (size_t i = 0; i < T_SIFS.size(); ++i) {
            T_DIFS.push_back(T_SIFS[i] + 2 * sigma);
        }

        try {
            // Calculate TtcpBest
            auto [TtcpBest, terms, sumW] = calculateTtcpBest(
                W, L_subf, CW_min, CW_min_tb, R, sigma, n_max, 
                T_PH, T_SIFS, T_BA, T_DIFS, L_subf_tb);
            
            // Calculate T_OH, T_BO, T_BO_prime
            auto [T_OH, T_BO, T_BO_prime] = calculateOverheadAndBackoff(
                T_SIFS, T_BA, T_DIFS, CW_min, CW_min_tb, T_PH, sigma);
            
            // Calculate n_iBest for TCP
            auto n_iBest = calculateN_iBest_TCP(
                TtcpBest, T_OH, T_BO, R, sigma, 
                L_subf, L_subf_tb, n_max, T_BO_prime, W, sumW);
                
            // Calculate D_tcp_max_mlo
            double D_tcpMaxMlo = calculateD_tcpMaxMlo(L_P, TtcpBest, sigma, n_iBest);
            
            // Calculate D_{tcp,i}^{slo} and D_{tcp,max}^{slo}
            auto [D_tcp_i_slo, D_tcp_max_slo] = calculateD_tcpSlo(
                W, L_P, L_subf, L_subf_tb, R, sigma, 
                T_OH, T_BO, T_BO_prime, n_max);
            
            // Write results to CSV
            outFile << W << ","
                   << TtcpBest << ","
                   << (sumW ? "true" : "false") << ","
                   << D_tcp_max_slo << ","
                   << D_tcpMaxMlo << ","
                   << n_iBest[0] << ","
                   << n_iBest[1] << "\n";
            
        } catch (const std::exception& e) {
            std::cerr << "Error for W=" << W << ": " << e.what() << std::endl;
            outFile << W << ",ERROR,ERROR,ERROR,ERROR,ERROR,ERROR\n";
        }
    }

    outFile.close();
    std::cout << "Results written to result_tcp.csv" << std::endl;

    return 0;
}