import os
import sys

# 支持的图片扩展名（可根据需要增减）
IMAGE_EXTENSIONS = {'.jpg', '.jpeg', '.png', '.gif', '.bmp', '.tiff', '.webp'}

def load_name_id_mapping(dzb_path='dzb.txt'):
    """
    从对照文件中加载姓名 -> 身份证号的映射。
    文件格式：每行 "姓名,身份证号" （逗号分隔，前后空白会被去除）
    """
    mapping = {}
    if not os.path.exists(dzb_path):
        print(f"错误：对照文件 {dzb_path} 不存在。")
        return mapping

    try:
        # 尝试 UTF-8 编码，若失败则使用 GBK（常用于中文 Windows）
        with open(dzb_path, 'r', encoding='utf-8') as f:
            lines = f.readlines()
    except UnicodeDecodeError:
        with open(dzb_path, 'r', encoding='gbk') as f:
            lines = f.readlines()

    for line_num, line in enumerate(lines, 1):
        line = line.strip()
        if not line:
            continue
        if ',' not in line:
            print(f"警告：第 {line_num} 行格式错误（缺少逗号），已跳过：{line}")
            continue
        # 以逗号分割，取前两部分
        parts = line.split(',', 1)
        name = parts[0].strip()
        id_num = parts[1].strip()
        if name and id_num:
            # 如果姓名重复，保留第一次出现的记录（可根据需要改为覆盖并警告）
            if name in mapping:
                print(f"警告：姓名 '{name}' 重复出现（第 {line_num} 行），将使用第一次出现的身份证号 {mapping[name]}")
            else:
                mapping[name] = id_num
        else:
            print(f"警告：第 {line_num} 行姓名或身份证号为空，已跳过：{line}")
    return mapping

def extract_name_from_filename(filename):
    """
    从文件名中提取姓名。
    规则：
      - 去掉扩展名后，若文件名中包含 '-'，则取 '-' 之前的部分；
      - 否则取整个文件名（不含扩展名）。
    """
    base = os.path.splitext(filename)[0]   # 不含扩展名的部分
    if '-' in base:
        name = base.split('-', 1)[0]       # 取第一个 '-' 之前的部分
    else:
        name = base
    return name.strip()

def rename_images_in_directory(directory, name_id_map):
    """
    遍历目录下的所有图片文件，根据映射表重命名。
    新文件名格式：身份证号.jpg（扩展名固定为 .jpg）
    若目标文件已存在或姓名不在映射表中，则跳过。
    """
    if not os.path.isdir(directory):
        print(f"错误：目录 '{directory}' 不存在或不是一个有效目录。")
        return

    renamed_count = 0
    skipped_count = 0

    for item in os.listdir(directory):
        file_path = os.path.join(directory, item)
        if not os.path.isfile(file_path):
            continue   # 忽略子目录

        # 检查是否为图片文件
        ext = os.path.splitext(item)[1].lower()
        if ext not in IMAGE_EXTENSIONS:
            continue

        # 提取姓名
        name = extract_name_from_filename(item)
        if not name:
            print(f"警告：文件名 '{item}' 无法提取有效姓名，跳过。")
            skipped_count += 1
            continue

        # 查找身份证号
        if name not in name_id_map:
            print(f"跳过：'{item}' 中的姓名 '{name}' 不在对照表中。")
            skipped_count += 1
            continue

        id_number = name_id_map[name]
        new_name = f"{id_number}.jpg"
        new_path = os.path.join(directory, new_name)

        # 检查目标文件是否已存在
        if os.path.exists(new_path):
            print(f"跳过：目标文件 '{new_name}' 已存在，无法重命名 '{item}'。")
            skipped_count += 1
            continue

        # 执行重命名
        try:
            os.rename(file_path, new_path)
            print(f"成功：'{item}' -> '{new_name}'")
            renamed_count += 1
        except Exception as e:
            print(f"错误：重命名 '{item}' 失败：{e}")
            skipped_count += 1

    print(f"\n处理完成：成功重命名 {renamed_count} 个文件，跳过 {skipped_count} 个文件。")

def main():
    print("=== 图片文件批量重命名工具（基于身份证对照表）===")
    # 获取用户输入的目录路径
    while True:
        dir_path = input("请输入要处理的目录路径：").strip()
        if dir_path:
            if os.path.isdir(dir_path):
                break
            else:
                print("目录不存在，请重新输入。")
        else:
            print("路径不能为空。")

    # 对照文件默认与脚本同目录下的 dzb.txt，也可改为绝对路径
    dzb_path = "dzb.txt"
    if not os.path.exists(dzb_path):
        # 尝试当前工作目录
        dzb_path = os.path.join(os.getcwd(), "dzb.txt")
        if not os.path.exists(dzb_path):
            print(f"错误：未找到对照文件 dzb.txt，请在当前目录或脚本目录下放置该文件。")
            sys.exit(1)

    print(f"正在加载对照文件：{dzb_path}")
    name_id_map = load_name_id_mapping(dzb_path)
    if not name_id_map:
        print("对照表为空，无法进行重命名。")
        sys.exit(1)

    print(f"已加载 {len(name_id_map)} 条姓名-身份证号记录。")
    rename_images_in_directory(dir_path, name_id_map)

if __name__ == "__main__":
    main()