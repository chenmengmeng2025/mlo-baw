import math

def compute_alpha_fixed(pM, pS, tau_F, tau_T_M, tau_T_S, N):
    pM_pow = math.pow(pM, 1.0 / N)
    
    denom = (1.0 + tau_F + 
             (tau_T_M - tau_F) * pM + 
             N * (tau_T_S - tau_F) * pS - 
             (tau_T_M + N * (tau_T_S - tau_F)) * pS * pM_pow)
    
    return 1.0 / denom

def compute_alpha(pM, pS, tau_F, tau_T_M, tau_T_S, N):    
    denom = (1.0 + tau_F + 
             (tau_T_M - tau_F) * pM - tau_T_M * pS - (tau_T_S - tau_F) * pS * math.log(pM) )
    
    return 1.0 / denom

def compute_lambda(pM, pS, tau_T, alpha):
    tauT_M = tau_T[0]
    tauT_S = tau_T[1]

    # 计算 lambda_S 与 lambda_M
    lambdaS = -tauT_S * pS * math.log(pM) * alpha
    lambdaM =  tauT_M * (pM - pS) * alpha

    return lambdaS, lambdaM

def compute_lambda_fixed(pM, pS, tau_T, alpha ,N):
    tauT_M = tau_T[0]
    tauT_S = tau_T[1]

    # 计算 lambda_S 与 lambda_M
    lambdaS = N * (1 - math.pow(pM, 1.0 / N)) * alpha * pS * tauT_S
    lambdaM =  (1 - pS * math.pow(pM, 1.0 / N -1)) * alpha * pM * tauT_M

    return lambdaS, lambdaM

def compute_lambda_fixed2(pM, pS, tau_T, alpha ,N):
    tauT_M = tau_T[0]
    tauT_S = tau_T[1]

    sum_part = 0
    def Q(k):
        return 2 / (1 + 16 * (2 ** k))

    for k in range(6):
            sum_part += ((1 - pM) ** k) / Q(k)

    last_term = ((1 - pM) ** 6) / (pM * Q(6))
    denominator = sum_part + last_term

    # 计算 lambda_S 与 lambda_M
    lambdaM =  (alpha * tauT_M) / denominator
    lambdaS = 0

    return lambdaS, lambdaM

pS = pM = 0.601519
N1 = N2 = 0
if(N1 == 11 and N2 == 11):
    pS = pM = 0.588928
elif(N1 == 10 and N2 == 10):
    pS = pM = 0.601519
elif(N1 == 9 and N2 == 9):
    pS = pM = 0.615596
elif(N1 == 8 and N2 == 8):
    pS = pM = 0.63153
elif(N1 == 7 and N2 == 7):
    pS = pM = 0.649836
elif(N1 == 6 and N2 == 6):
    pS = pM = 0.671257
elif(N1 == 5 and N2 == 5):  
    pS = pM = 0.696898
elif(N1 == 4 and N2 == 4):
    pS = pM = 0.728464
elif(N1 == 3 and N2 == 3):
    pS = pM = 0.768672

BAW = 256
nmpdu_sld = 1
nmpdu_mld = [0, 255]

R = [344.118, 2882.353]
R_sld = R.copy()

sigma = 9.0
L_P = 1500.0 * 8.0

T_PH = 56.0
T_RTS = [30, 24]
T_CTS = [34, 28]
T_RTSCTS_PH = 20
T_SIFS = [10, 16]

L_subf_sld = 1572.0 * 8.0
L_subf_mld = 1572.0 * 8.0

K1 = 6
W2 = [16, 16]
K2 = 6

if nmpdu_sld == 1:
    L_subf_sld = 1570.0 * 8.0

T_DIFS = [sifs + 2 * sigma for sifs in T_SIFS]

tau_F = []
for i in range(len(R)):
    tau_F.append((T_RTS[i] + T_DIFS[i]) / sigma)
print("tau_F =", tau_F)

if BAW <= 256:
    T_BA0 = 46.0
elif BAW <= 512:
    T_BA0 = 58.0
else:
    T_BA0 = 78.0
T_BA1 = T_BA0 - 6.0

if nmpdu_sld == 1:
    T_BA_sld0 = 34.0
    T_BA_sld1 = 28.0
else: 
    T_BA_sld0 = T_BA0
    T_BA_sld1 = T_BA1

T_OH_mld = []
for i in range(len(R)):
    value = (T_PH + T_SIFS[i] * 3 + (T_BA0 if i == 0 else T_BA1) + T_DIFS[i] + T_RTS[i] + T_CTS[i]) / sigma
    T_OH_mld.append(value)

T_OH_sld = []
for i in range(len(R)):
    value = (T_PH + T_SIFS[i] * 3 + (T_BA_sld0 if i == 0 else T_BA_sld1) + T_DIFS[i] + T_RTS[i] + T_CTS[i]) / sigma
    T_OH_sld.append(value)
print("T_OH_mld =", T_OH_mld[0]* sigma, T_OH_mld[1]* sigma)
print("T_OH_sld =", T_OH_sld[0]* sigma, T_OH_sld[1]* sigma)

tau_T1 = [0.0, 0.0]
tau_T2 = [0.0, 0.0]


tau_T1[0] = math.floor(math.ceil((16 + nmpdu_mld[0] * L_subf_mld + 6) / (R[0] * 13.6)) * 13.6 + 6) / sigma + T_OH_mld[0]
tau_T1[1] = math.floor(math.ceil((16 + nmpdu_sld * L_subf_sld + 6) / (R[0] * 13.6)) * 13.6 + 6) / sigma + T_OH_sld[0]
tau_T2[0] = math.floor(math.ceil((16 + nmpdu_mld[1] * L_subf_mld + 6) / (R[1] * 13.6)) * 13.6) / sigma + T_OH_mld[1]
tau_T2[1] = math.floor(math.ceil((16 + nmpdu_sld * L_subf_sld + 6) / (R[1] * 13.6)) * 13.6) / sigma + T_OH_sld[1]

print("ppdu mld: ",math.floor(math.ceil((16 + nmpdu_mld[1] * L_subf_mld + 6) / 39200) * 13.6))
print("ppdu sld: ",math.floor(math.ceil((16 + nmpdu_sld * L_subf_sld + 6) / 39200) * 13.6) )
print("tau_T1 =", tau_T1)
print("tau_T2 =", tau_T2)

alpha = compute_alpha_fixed(pM, pS, tau_F[1], tau_T2[0], tau_T2[1], N2)

print("alpha =", alpha)

lamS, lamM = compute_lambda_fixed2(pM, pS, tau_T2, alpha, N2)
print("lamM =", lamM)

throughput = lamM *  L_P * nmpdu_mld[1] / tau_T2[0] / sigma
print(f"Throughput (Mbps) = {throughput:.4f}")
