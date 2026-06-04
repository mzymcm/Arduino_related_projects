import os
import sys
import shutil
from io import BytesIO
from datetime import datetime

# 检查Pillow库
try:
    from PIL import Image
    PIL_AVAILABLE = True
except ImportError:
    PIL_AVAILABLE = False
    print("错误：未安装 Pillow 库，无法进行图片压缩/转换。请先执行 'pip install Pillow'")
    sys.exit(1)

IMAGE_EXTENSIONS = {'.jpg', '.jpeg', '.png', '.gif', '.bmp', '.tiff', '.webp'}

def convert_or_compress_to_jpeg(input_path, output_path, target_size_kb=50):
    """
    将任意图片转换为JPEG格式，如果原图 > target_size_kb 则压缩到该阈值以内。
    返回 True 表示成功，False 表示失败。
    """
    try:
        img = Image.open(input_path)
    except Exception as e:
        print(f"  无法读取图片 {input_path}: {e}")
        return False

    # 转换为RGB模式（处理透明通道和调色板）
    if img.mode in ('RGBA', 'LA', 'P'):
        background = Image.new('RGB', img.size, (255, 255, 255))
        if img.mode == 'P':
            img = img.convert('RGBA')
        if img.mode == 'RGBA':
            background.paste(img, mask=img.split()[-1])
        else:
            background.paste(img)
        img = background
    elif img.mode != 'RGB':
        img = img.convert('RGB')

    # 先检查原图（转RGB后）保存为JPEG的大小
    test_buffer = BytesIO()
    img.save(test_buffer, format='JPEG', quality=95, optimize=True)
    if test_buffer.tell() <= target_size_kb * 1024:
        # 直接保存高质量JPEG
        img.save(output_path, format='JPEG', quality=95, optimize=True)
        return True

    # 需要压缩：尝试降低质量
    for quality in (85, 75, 65, 55, 45, 35, 25, 15, 10):
        buffer = BytesIO()
        img.save(buffer, format='JPEG', quality=quality, optimize=True)
        if buffer.tell() <= target_size_kb * 1024:
            with open(output_path, 'wb') as f:
                f.write(buffer.getvalue())
            return True

    # 质量降到最低仍超限，开始缩小尺寸
    scale = 0.9
    min_dim = 100
    current_img = img
    while current_img.size[0] > min_dim and current_img.size[1] > min_dim:
        new_size = (int(current_img.size[0] * scale), int(current_img.size[1] * scale))
        current_img = current_img.resize(new_size, Image.LANCZOS)

        for quality in (85, 75, 65, 55, 45, 35, 25, 15, 10):
            buffer = BytesIO()
            current_img.save(buffer, format='JPEG', quality=quality, optimize=True)
            if buffer.tell() <= target_size_kb * 1024:
                with open(output_path, 'wb') as f:
                    f.write(buffer.getvalue())
                return True
        scale *= 0.9

    # 实在压不到50KB以内，用最低质量+最小尺寸保存最后一次尝试的结果
    current_img.save(output_path, format='JPEG', quality=10, optimize=True)
    final_size = os.path.getsize(output_path) / 1024
    print(f"  警告：压缩后仍为 {final_size:.1f}KB (超过50KB)，已尽力压缩。")
    return True


def load_name_id_mapping(dzb_path):
    """加载姓名->身份证号映射，返回字典，失败返回空字典"""
    mapping = {}
    if not os.path.exists(dzb_path):
        return mapping
    try:
        with open(dzb_path, 'r', encoding='utf-8') as f:
            lines = f.readlines()
    except UnicodeDecodeError:
        with open(dzb_path, 'r', encoding='gbk') as f:
            lines = f.readlines()

    for line_num, line in enumerate(lines, 1):
        line = line.strip()
        if not line or ',' not in line:
            continue
        parts = line.split(',', 1)
        name = parts[0].strip()
        id_num = parts[1].strip()
        if name and id_num and name not in mapping:
            mapping[name] = id_num
    return mapping


def extract_name_from_filename(filename):
    """从文件名（不含路径）提取姓名，规则同原脚本"""
    base = os.path.splitext(filename)[0]
    if '-' in base:
        return base.split('-', 1)[0].strip()
    return base.strip()


def get_unique_filepath(directory, basename, ext='.jpg'):
    """返回不重复的文件路径，若存在则添加 _1, _2 等"""
    original = os.path.join(directory, f"{basename}{ext}")
    if not os.path.exists(original):
        return original
    counter = 1
    while True:
        new_name = f"{basename}_{counter}{ext}"
        new_path = os.path.join(directory, new_name)
        if not os.path.exists(new_path):
            return new_path
        counter += 1


def scale_images(input_dir, output_dir):
    """
    将 input_dir 中的所有图片转换成JPEG格式并保存到 output_dir，
    若原图 >50KB 则压缩到50KB以内，否则仅转格式。
    返回 (总处理数, 成功数)
    """
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    total = 0
    success = 0
    for filename in os.listdir(input_dir):
        file_path = os.path.join(input_dir, filename)
        if not os.path.isfile(file_path):
            continue
        ext = os.path.splitext(filename)[1].lower()
        if ext not in IMAGE_EXTENSIONS:
            continue

        total += 1
        # 输出文件名：去除原扩展名后加 .jpg
        base_name = os.path.splitext(filename)[0]
        out_path = get_unique_filepath(output_dir, base_name, '.jpg')

        print(f"处理: {filename} -> {os.path.basename(out_path)}")
        if convert_or_compress_to_jpeg(file_path, out_path, target_size_kb=50):
            success += 1
        else:
            print(f"  转换失败: {filename}")
    return total, success


def rename_by_id(output_dir, name_id_map):
    """
    在 output_dir 中，根据文件名提取姓名，对照映射表重命名为 身份证号.jpg
    如果姓名不在映射表中，则跳过。若目标文件已存在，添加 _序号 避免冲突。
    """
    renamed = 0
    skipped = 0
    for filename in os.listdir(output_dir):
        if not filename.lower().endswith('.jpg'):
            continue
        file_path = os.path.join(output_dir, filename)

        name = extract_name_from_filename(filename)
        if not name:
            print(f"跳过：无法从 '{filename}' 提取姓名")
            skipped += 1
            continue

        if name not in name_id_map:
            print(f"跳过：姓名 '{name}' 不在对照表中 (文件: {filename})")
            skipped += 1
            continue

        new_name = f"{name_id_map[name]}.jpg"
        new_path = os.path.join(output_dir, new_name)
        if os.path.exists(new_path) and new_path != file_path:
            # 目标已存在，使用序号避免覆盖
            base = name_id_map[name]
            counter = 1
            while True:
                candidate = f"{base}_{counter}.jpg"
                candidate_path = os.path.join(output_dir, candidate)
                if not os.path.exists(candidate_path):
                    new_name = candidate
                    new_path = candidate_path
                    break
                counter += 1

        try:
            os.rename(file_path, new_path)
            print(f"重命名: {filename} -> {new_name}")
            renamed += 1
        except Exception as e:
            print(f"重命名失败: {filename} -> {new_name}, 错误: {e}")
            skipped += 1
    return renamed, skipped


def main():
    print("=== 图片处理工具（缩放+可选重命名）===")
    # 获取源目录
    while True:
        src_dir = input("请输入图片所在目录路径：").strip()
        if src_dir and os.path.isdir(src_dir):
            break
        print("目录不存在，请重新输入")

    # 创建输出目录（在原目录下加时间戳避免冲突）
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    default_out_dir = os.path.join(src_dir, f"scaled_{timestamp}")
    out_dir = input(f"请输入输出目录（直接回车使用 {default_out_dir}）：").strip()
    if not out_dir:
        out_dir = default_out_dir

    print(f"\n步骤1：缩放并转换所有图片为JPEG格式（>50KB自动压缩）")
    print(f"源目录: {src_dir}")
    print(f"输出目录: {out_dir}")
    total, success = scale_images(src_dir, out_dir)
    print(f"缩放完成：共 {total} 个图片文件，成功转换 {success} 个")

    # 检查对照文件
    dzb_path = "dzb.txt"
    if not os.path.exists(dzb_path):
        dzb_path = os.path.join(os.getcwd(), "dzb.txt")
    name_id_map = load_name_id_mapping(dzb_path)

    if name_id_map:
        print(f"\n步骤2：检测到有效对照文件（{len(name_id_map)}条记录），开始批量重命名...")
        renamed, skipped = rename_by_id(out_dir, name_id_map)
        print(f"重命名完成：成功 {renamed} 个，跳过 {skipped} 个")
    else:
        print("\n未检测到有效的对照文件（dzb.txt），仅执行缩放，不进行重命名。")
        print(f"处理后的图片已保存在: {out_dir}")

    print("\n全部处理完毕。")


if __name__ == "__main__":
    main()