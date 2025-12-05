import re
import matplotlib
matplotlib.use('Agg')  # 设置后端为Agg，避免GUI相关问题
import matplotlib.pyplot as plt
import numpy as np
import argparse
import os

def parse_log_file(file_path):
    """
    解析日志文件，提取所有的score值和对应的文件名
    
    Args:
        file_path (str): 日志文件路径
        
    Returns:
        tuple: (scores列表, 文件名列表)
    """
    scores = []
    filenames = []
    with open(file_path, 'r', encoding='utf-8') as f:
        content = f.read()
        
    # 使用正则表达式匹配File行和score值
    file_blocks = content.split('File: ')[1:]  # 分割成块，每块是一个文件的信息
    
    for block in file_blocks:
        lines = block.strip().split('\n')
        filename_line = lines[0]  # 第一行是文件名
        filename = filename_line.strip()
        
        # 查找该文件中的score值
        score_match = re.search(r'score:\s*(\d+)', block)
        if score_match:
            score = int(score_match.group(1))
            scores.append(score)
            filenames.append(filename)
        else:
            # 如果没有找到score，添加默认值0
            scores.append(0)
            filenames.append(filename)
    
    return scores, filenames

def save_filenames_by_score_ranges(scores, filenames, base_name="score"):
    """
    根据得分区间保存文件名到不同的文件中
    
    Args:
        scores (list): 得分列表
        filenames (list): 文件名列表
        base_name (str): 基础文件名前缀
    """
    # 定义得分区间
    bins = np.linspace(0, 100, 11)  # 0, 10, 20, ..., 100
    
    # 创建一个字典来存储每个区间的文件名和得分
    score_ranges = {}
    for i in range(len(bins)-1):
        range_key = f"{int(bins[i]):02d}-{int(bins[i+1]):02d}"
        score_ranges[range_key] = []
    
    # 将文件名和得分分配到对应的得分区间
    for score, filename in zip(scores, filenames):
        # 特殊处理得分为0的情况
        if score == 0:
            score_ranges["00-10"].append((filename, score))
        else:
            for i in range(len(bins)-1):
                if bins[i] <= score < bins[i+1] or (i == len(bins)-2 and score == bins[i+1]):  # 处理100分的情况
                    range_key = f"{int(bins[i]):02d}-{int(bins[i+1]):02d}"
                    score_ranges[range_key].append((filename, score))
                    break
    
    # 计算每个区间的文件数量
    hist, bin_edges = np.histogram(scores, bins=bins)
    
    # 保存每个区间的文件名到单独的文件中，文件名后跟得分
    for i, (range_key, files_in_range) in enumerate(score_ranges.items()):
        # 修改：按数字顺序排序文件名
        files_in_range.sort(key=lambda x: re.findall(r'\d+', x[0]))
        
        # 修改：总是为所有区间创建文件，即使为空
        output_filename = f"{base_name}_files_scores{range_key}_cnt-{hist[i]}.txt"
        with open(output_filename, 'w', encoding='utf-8') as f:
            for filename, score in files_in_range:
                f.write(f"{filename} score:{score}\n")
        print(f"得分区间 {range_key} 的文件名已保存到: {output_filename} (共{len(files_in_range)}个文件)")
    
    # 特别处理没有检测到目标的文件（Object count: 0 或 not detect）
    zero_detection_files = []
    # 修改这里，直接使用原始文件路径而不是构造新路径
    log_file_path = base_name + '_result.txt'
    if os.path.exists(log_file_path):
        with open(log_file_path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # 查找所有没有检测到目标的文件块
        file_blocks = content.split('File: ')[1:]
        for block in file_blocks:
            lines = block.strip().split('\n')
            filename_line = lines[0]
            filename = filename_line.strip()
            
            # 检查是否有"Object count: 0"或包含"not detect"的行
            has_zero_objects = any('Object count: 0' in line for line in lines)
            has_not_detect = any('not detect' in line.lower() for line in lines)
            
            if has_zero_objects or has_not_detect:
                zero_detection_files.append(filename)
        
        # 修改：按数字顺序排序文件名
        zero_detection_files.sort(key=lambda x: re.findall(r'\d+', x))
        
        # 保存没有检测到目标的文件列表
        if zero_detection_files:
            output_filename = f"{base_name}_files_no_detections_total-{len(zero_detection_files)}.txt"
            with open(output_filename, 'w', encoding='utf-8') as f:
                for filename in zero_detection_files:
                    f.write(f"{filename}\n")
            print(f"没有检测到目标的文件已保存到: {output_filename} (共{len(zero_detection_files)}个文件)")
    else:
        print(f"警告: 原始日志文件 {log_file_path} 不存在，无法处理无检测文件")

def count_files(file_path):
    """
    统计日志文件中的文件数量
    
    Args:
        file_path (str): 日志文件路径
        
    Returns:
        int: 文件数量
    """
    with open(file_path, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # 使用正则表达式匹配File行
    file_pattern = r'File:.*'
    file_matches = re.findall(file_pattern, content)
    return len(file_matches)

# 新增函数：统计包含"detected"字符串的文件数量
def count_detected_files(file_path):
    """
    统计日志文件中包含检测结果的文件数量
    
    Args:
        file_path (str): 日志文件路径
        
    Returns:
        int: 包含检测结果的文件数量
    """
    with open(file_path, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # 使用File分割符来分割文件块，然后检查每个文件块是否包含检测结果
    file_blocks = content.split('File: ')[1:]  # 分割成块，每块是一个文件的信息
    detected_count = 0
    
    for block in file_blocks:
        # 检查文件块中是否包含"detected"但不包含"not detect"
        if 'detected' in block and 'not detect' not in block:
            detected_count += 1
    
    return detected_count

def analyze_scores(scores, file_count, total_detections,label="Scores"):
    """
    分析scores并生成统计信息和图表
    
    Args:
        scores (list): score值列表
        file_count (int): 文件数量
        label (str): 数据集标签
    """
    if not scores:
        print("没有找到有效的score数据")
        return
    
    # 计算基本统计信息
    total = total_detections
    rate = total_detections / file_count
    mean_score = np.mean(scores)
    min_score = np.min(scores)
    max_score = np.max(scores)
    
    print(f"\n=== {label} 统计信息 ===")
    print(f"总计检测数: {total}")
    print(f"检测率: {rate:.2f}")
    print(f"平均得分: {mean_score:.2f}")
    print(f"最低得分: {min_score}")
    print(f"最高得分: {max_score}")
    print(f"总文件数: {file_count}")
    
    # 添加总检测数量统计
    print(f"检测目标总数: {total}")
    
    # 将分数分为10个等分区间 (0-100)
    bins = np.linspace(0, 100, 11)  # 0, 10, 20, ..., 100
    hist, bin_edges = np.histogram(scores, bins=bins)
    
    print(f"\n=== {label} 得分分布 ===")
    for i in range(len(hist)):
        bin_start = int(bin_edges[i])
        bin_end = int(bin_edges[i+1])
        count = hist[i]
        percentage = (count / file_count) * 100 if file_count > 0 else 0
        print(f"得分 {bin_start:2d}-{bin_end:2d}: {count:4d} 个 ({percentage:5.1f}%)")
    
    # 创建可视化图表，增大整体图形尺寸
    plt.figure(figsize=(15, 7))
    
    # 绘制直方图
    plt.subplot(1, 2, 1)
    bars = plt.bar(range(10), hist, tick_label=[f'{int(bin_edges[i])}-{int(bin_edges[i+1])}' for i in range(10)], 
                   color='skyblue', edgecolor='black')
    plt.xlabel('Score Interval')
    plt.ylabel('Quantity')
    plt.title(f'{label} Score Distribution Histogram')
    plt.xticks(rotation=45)
    
    # 在每个柱子上显示数值
    for i, bar in enumerate(bars):
        height = bar.get_height()
        plt.text(bar.get_x() + bar.get_width()/2., height,
                 f'{int(height)}',
                 ha='center', va='bottom')
    
    # 绘制饼图，将其画得更大
    # 创建更大的子图区域用于饼图
    plt.subplot(1, 2, 2)
    # 调整子图参数使饼图更大
    plt.subplots_adjust(wspace=0.3)
    
    # 修改：统一使用相同的bins和hist数据，保证一致性
    pie_labels = [f'{int(bin_edges[i])}-{int(bin_edges[i+1])}' for i in range(len(hist))]
    pie_sizes = hist.tolist()
    
    # 过滤掉0值区间，但保持标签和数据的一致性
    filtered_labels = []
    filtered_sizes = []
    for label, size in zip(pie_labels, pie_sizes):
        if size > 0:  # 只显示非零的区间
            filtered_labels.append(label)
            filtered_sizes.append(size)
    
    if filtered_sizes:
        colors = plt.cm.Paired(np.linspace(0, 1, len(filtered_sizes)))
        wedges, texts, autotexts = plt.pie(filtered_sizes, labels=filtered_labels, autopct='%1.1f%%', colors=colors, startangle=90)
        # 增大饼图标题字体
        plt.title(f'{label} Score Distribution Pie Chart', fontsize=14)
    
    # 添加统计信息到图表上
    stats_text = f'Total Samples: {file_count}\nTotal Detections: {total}\n Rate:{rate:.2f}\nAverage Score: {mean_score:.2f}\nMin Score: {min_score}\nMax Score: {max_score}'
    plt.figtext(0.98, 0.02, stats_text, fontsize=10, bbox=dict(boxstyle="round,pad=0.3", facecolor="lightgray"))
    
    plt.tight_layout()
    
    # 保存图像而不是显示
    output_file = f'{label.replace(" ", "_")}_score_distribution.png'
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"\n图表已保存为: {output_file}")
    plt.close()

def compare_logs(file_paths, total_detections):
    """
    比较多个日志文件的得分分布
    
    Args:
        file_paths (list): 日志文件路径列表
    """
    all_scores = []
    all_file_counts = []
    labels = []
    
    for file_path in file_paths:
        scores, _ = parse_log_file(file_path)  # 只需要scores
        file_count = count_files(file_path)
        all_scores.append(scores)
        all_file_counts.append(file_count)
        labels.append(os.path.basename(file_path).replace('_result.txt', '').capitalize())
    
    # 创建对比图表
    plt.figure(figsize=(15, 6))
    
    # 绘制重叠直方图
    plt.subplot(1, 2, 1)
    bins = np.linspace(0, 100, 11)
    
    for i, scores in enumerate(all_scores):
        plt.hist(scores, bins=bins, alpha=0.7, label=labels[i], edgecolor='black')
    
    plt.xlabel('score')
    plt.ylabel('Quantity')
    plt.title('Comparison of Score Distributions')
    plt.legend()
    plt.grid(True, alpha=0.3)
    
    # 绘制箱线图
    plt.subplot(1, 2, 2)
    plt.boxplot(all_scores, labels=labels)
    plt.ylabel('score')
    plt.title('Score Box Plot')
    plt.grid(True, alpha=0.3)
    
    plt.tight_layout()
    
    # 保存图像
    output_file = 'comparison_score_distribution.png'
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"\n对比图表已保存为: {output_file}")
    plt.close()
    
    # 打印详细统计信息
    for i, scores in enumerate(all_scores):
        analyze_scores(scores, all_file_counts[i], total_detections,labels[i])

def main():
    parser = argparse.ArgumentParser(description="分析检测日志文件中的score得分分布")
    parser.add_argument("files", nargs='+', help="日志文件路径")
    parser.add_argument("--compare", action="store_true", help="比较多个日志文件")
    args = parser.parse_args()
    total_detections = 0
    # 计算所有文件的总检测数量

    for file_path in args.files:
        if os.path.exists(file_path):
            # 修改为统计包含"detected"字符串的文件数量
            total_detections += count_detected_files(file_path)
    
    print(f"所有文件总检测数量: {total_detections}")

    if args.compare and len(args.files) > 1:
        compare_logs(args.files,total_detections)
    else:
        for file_path in args.files:
            if os.path.exists(file_path):
                scores, filenames = parse_log_file(file_path)  # 获取scores和filenames
                file_count = count_files(file_path)
                label = os.path.basename(file_path).replace('_result.txt', '').capitalize()
                analyze_scores(scores, file_count, total_detections,label)
                
                # 保存文件名按得分区间分类
                base_name = os.path.basename(file_path).replace('_result.txt', '')
                save_filenames_by_score_ranges(scores, filenames, base_name)
            else:
                print(f"文件不存在: {file_path}")

if __name__ == "__main__":
    main()