import cv2
import os
import argparse
import numpy as np

def bgr_to_nv12(image):
    """
    将BGR图像转换为NV12格式
    
    Args:
        image: BGR格式的numpy数组 (height, width, 3)
        
    Returns:
        NV12格式的字节数据
    """
    height, width = image.shape[:2]
    
    # 先转换为YUV格式
    yuv_image = cv2.cvtColor(image, cv2.COLOR_BGR2YUV)
    
    # 提取Y通道
    y_channel = yuv_image[:, :, 0]
    
    # 提取U和V通道并下采样(隔行隔列采样)
    u_channel = yuv_image[::2, ::2, 1]
    v_channel = yuv_image[::2, ::2, 2]
    
    # U和V通道交错排列形成UV平面
    uv_interleaved = np.zeros((u_channel.shape[0], u_channel.shape[1] * 2), dtype=np.uint8)
    uv_interleaved[:, ::2] = u_channel
    uv_interleaved[:, 1::2] = v_channel
    
    # 将Y和UV数据连接
    nv12_data = np.concatenate([y_channel.flatten(), uv_interleaved.flatten()])
    
    return nv12_data.tobytes()

def video_to_images(video_path, output_dir, frame_interval=1, width=640, height=360, output_format='jpeg'):
    """
    将视频解码为指定格式的图片
    
    Args:
        video_path (str): 视频文件路径
        output_dir (str): 输出图片目录
        frame_interval (int): 帧间隔，默认为1（每帧都保存）
        width (int): 输出图像宽度，默认为640
        height (int): 输出图像高度，默认为360
        output_format (str): 输出图像格式，支持 'jpeg', 'yuv', 'rgb'，默认为 'jpeg'
    """
    
    # 创建输出目录
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
    
    # 打开视频文件
    cap = cv2.VideoCapture(video_path)
    
    # 检查视频是否成功打开
    if not cap.isOpened():
        print(f"错误：无法打开视频文件 {video_path}")
        return
    
    # 获取视频的基本信息
    fps = cap.get(cv2.CAP_PROP_FPS)
    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    
    print(f"视频FPS: {fps}")
    print(f"总帧数: {total_frames}")
    print(f"输出分辨率: {width}x{height}")
    print(f"输出格式: {output_format}")
    
    frame_count = 0
    saved_count = 0
    
    while True:
        # 读取一帧
        ret, frame = cap.read()
        
        # 如果没有更多帧，则退出循环
        if not ret:
            break
        
        # 根据帧间隔保存图片
        if frame_count % frame_interval == 0:
            # 调整帧大小为指定分辨率
            resized_frame = cv2.resize(frame, (width, height))
            
            # 根据指定格式保存图像
            if output_format.lower() == 'jpeg':
                output_filename = os.path.join(output_dir, f"frame_{saved_count:06d}.jpg")
                cv2.imwrite(output_filename, resized_frame, [cv2.IMWRITE_JPEG_QUALITY, 95])
            elif output_format.lower() == 'yuv':
                output_filename = os.path.join(output_dir, f"frame_{saved_count:06d}.yuv")
                # 转换BGR到NV12格式
                nv12_frame = bgr_to_nv12(resized_frame)
                with open(output_filename, 'wb') as f:
                    f.write(nv12_frame)
            elif output_format.lower() == 'rgb':
                output_filename = os.path.join(output_dir, f"frame_{saved_count:06d}.rgb")
                # 转换BGR到RGB
                rgb_frame = cv2.cvtColor(resized_frame, cv2.COLOR_BGR2RGB)
                with open(output_filename, 'wb') as f:
                    f.write(rgb_frame.tobytes())
            else:
                print(f"不支持的格式: {output_format}，使用默认JPEG格式")
                output_filename = os.path.join(output_dir, f"frame_{saved_count:06d}.jpg")
                cv2.imwrite(output_filename, resized_frame, [cv2.IMWRITE_JPEG_QUALITY, 95])
            
            print(f"已保存: {output_filename}")
            saved_count += 1
        
        frame_count += 1
    
    # 释放视频捕获对象
    cap.release()
    
    print(f"完成！共保存 {saved_count} 张图片到 {output_dir}")

# 新增功能：将目录下的所有图片文件转换为YUV格式
def images_to_yuv(input_dir, output_dir, width=640, height=360):
    """
    将目录下的所有图片文件转换为YUV(NV12)格式
    
    Args:
        input_dir (str): 输入图片目录
        output_dir (str): 输出YUV图片目录
        width (int): 输出图像宽度，默认为640
        height (int): 输出图像高度，默认为360
    """
    
    # 支持的图片格式
    supported_formats = ('.jpg', '.jpeg', '.png', '.bmp', '.tiff')
    
    # 创建输出目录
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
    
    # 获取所有支持的图片文件
    image_files = [f for f in os.listdir(input_dir) if f.lower().endswith(supported_formats)]
    
    if not image_files:
        print(f"在目录 {input_dir} 中未找到支持的图片文件")
        return
    
    print(f"找到 {len(image_files)} 个图片文件")
    print(f"输出分辨率: {width}x{height}")
    
    converted_count = 0
    
    for image_file in image_files:
        # 构建完整的文件路径
        input_path = os.path.join(input_dir, image_file)
        
        # 读取图片
        image = cv2.imread(input_path)
        
        if image is None:
            print(f"警告：无法读取图片 {input_path}")
            continue
        
        # 调整图片大小
        resized_image = cv2.resize(image, (width, height))
        
        # 转换为NV12格式
        nv12_data = bgr_to_nv12(resized_image)
        
        # 生成输出文件名
        filename_without_ext = os.path.splitext(image_file)[0]
        output_filename = os.path.join(output_dir, f"{filename_without_ext}.yuv")
        
        # 保存YUV数据
        with open(output_filename, 'wb') as f:
            f.write(nv12_data)
        
        print(f"已转换: {input_path} -> {output_filename}")
        converted_count += 1
    
    print(f"完成！共转换 {converted_count} 个图片文件到 {output_dir}")


def main():
    parser = argparse.ArgumentParser(
        description="将视频解码为指定格式的图片或将图片目录转换为YUV格式",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
使用示例:
  视频转图片: python3 video_to_images.py video.mp4 -o output_dir -f yuv
  图片目录转YUV: python3 video_to_images.py image_dir -o output_dir --images-to-yuv
        """)
    
    parser.add_argument("input_path", help="输入视频文件路径或图片目录路径")
    parser.add_argument("-o", "--output", default="output_images", help="输出图片目录 (默认: output_images)")
    parser.add_argument("-i", "--interval", type=int, default=1, help="帧间隔 (默认: 1)")
    parser.add_argument("-w", "--width", type=int, default=640, help="输出图像宽度 (默认: 640)")
    parser.add_argument("-H", "--height", type=int, default=360, help="输出图像高度 (默认: 360)")
    parser.add_argument("-f", "--format", choices=['jpeg', 'yuv', 'rgb'], default='jpeg', 
                        help="输出图像格式: jpeg, yuv, rgb (默认: jpeg)")
    parser.add_argument("--images-to-yuv", action="store_true", 
                        help="将输入目录中的所有图片转换为YUV格式")
    
    args = parser.parse_args()
    
    # 检查输入路径是否存在
    if not os.path.exists(args.input_path):
        print(f"错误：输入路径不存在 {args.input_path}")
        return
    
    # 自动判断输入路径是文件还是目录
    if os.path.isfile(args.input_path):
        # 输入是文件，当作视频处理
        if args.images_to_yuv:
            print("警告：输入路径是文件，忽略 --images-to-yuv 参数，直接处理为视频")
        video_to_images(args.input_path, args.output, args.interval, args.width, args.height, args.format)
    elif os.path.isdir(args.input_path):
        # 输入是目录，当作图片目录处理
        images_to_yuv(args.input_path, args.output, args.width, args.height)
    else:
        print(f"错误：输入路径既不是文件也不是目录 {args.input_path}")
        return

if __name__ == "__main__":
    main()
