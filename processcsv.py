import pandas as pd

def process_csv(input_csv, output_csv):
    """
    处理CSV文件：
    1. 保留 Best_nmpdu_mld_0 和 Best_nmpdu_mld_1 都不为 0 的行
    2. 按 (R1, R2, N1, N2, BAW) 分组
    3. 计算每组 nmpdu_sld0 的最小值和最大值
    4. 保存为新的 CSV 文件
    """

    # 读取 CSV
    df = pd.read_csv(input_csv)

    # 条件筛选：Best_nmpdu_mld_0 != 0 且 Best_nmpdu_mld_1 != 0
    df_filtered = df[
        (df['Best_nmpdu_mld_0'] != 0) &
        (df['Best_nmpdu_mld_1'] != 0)
    ].copy()

    # 分组并计算 min / max
    df_result = (
        df_filtered
        .groupby(['R1', 'R2', 'N1', 'N2', 'BAW'], as_index=False)
        .agg(
            nmpdu_sld0_min=('nmpdu_sld0', 'min'),
            nmpdu_sld0_max=('nmpdu_sld0', 'max')
        )
    )

    # 保存结果
    df_result.to_csv(output_csv, index=False)

    print(f"处理完成！结果已保存到: {output_csv}")

    return df_result


# 使用示例
if __name__ == "__main__":
    input_csv = "scratch/solve-1vn/bar-256.csv"
    output_csv = "scratch/solve-1vn/bar-256-processed.csv"

    result_df = process_csv(input_csv, output_csv)
