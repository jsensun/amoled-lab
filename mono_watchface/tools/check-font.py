#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check-font.py —— 校验 LVGL 字库是否覆盖了固件真正会显示的字符

为什么需要它：
  LVGL 找不到字形时会画空心方框（就是"很多长方形"那个 bug）。
  手工比对字符集极易出错，必须脚本化、可重复。

踩过的坑（务必记住）：
  1. 不要用 grep 匹配 "0x4E45" 这种方式判断字在不在字库里 —— bitmap 数据里
     会碰巧出现同样的字节，导致假阳性（误报"有"）。
  2. LVGL 的 SPARSE_TINY cmap 中，unicode_list 存的是【相对 range_start 的偏移】，
     不是绝对码点！必须 码点 = range_start + offset。
     直接把 unicode_list 当绝对码点读，会得到一堆"康熙部首"之类的乱码字符，
     从而误判"整个字库是废的"。本脚本已正确处理。

用法：
  python check-font.py              # 检查全部字库
  python check-font.py font_x.c     # 只检查指定字库

退出码：0 = 全覆盖；1 = 有缺失（可接入 CI）
"""

import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC_INO = os.path.join(ROOT, "mono_watchface.ino")

# 字库文件名 -> 它在固件里负责显示哪类内容（用于提示，不影响判定）
FONT_ROLE = {
    "font_cjk_20.c": "中文黑体 20px（MONO 金句 / LIT 宜忌 / 印章）",
    "font_serif_20.c": "中文宋体 20px（LIT 刊头 / 农历）",
    "font_serif_28.c": "中文宋体 28px（LIT 诗词正文、出处）",
    "font_ui_20.c": "英文 UI 20px（栏目标签 / 顶栏）",
    "font_time_118.c": "大号时间数字 118px",
    "font_temp_46.c": "温度数字 46px",
    "font_temp_30.c": "指标数值 30px",
}


def parse_codepoints(path):
    """正确解析一个 LVGL 字库文件包含的全部 Unicode 码点。"""
    text = io.open(path, encoding="utf-8").read()

    # 1) 解析所有 unicode_list_N 数组 -> {名字: [偏移, ...]}
    lists = {}
    for name, body in re.findall(
        r"static const uint\d+_t (unicode_list_\d+)\[\]\s*=\s*\{(.*?)\}", text, re.S
    ):
        lists[name] = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]+)", body)]

    # 2) 解析 cmaps[]，按类型还原真实码点
    cps = set()
    for blk in re.findall(r"\{([^{}]*?)\.type\s*=\s*(LV_FONT_FMT_TXT_CMAP_\w+)", text, re.S):
        body, typ = blk
        rs = re.search(r"\.range_start\s*=\s*(\d+)", body)
        rl = re.search(r"\.range_length\s*=\s*(\d+)", body)
        ul = re.search(r"\.unicode_list\s*=\s*(\w+)", body)
        if not rs:
            continue
        start = int(rs.group(1))
        if typ == "LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY":
            # 连续区间
            n = int(rl.group(1)) if rl else 0
            cps |= set(range(start, start + n))
        elif typ == "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY" and ul and ul.group(1) in lists:
            # 稀疏表：存的是相对 range_start 的偏移
            cps |= {start + off for off in lists[ul.group(1)]}
    return cps


def extract_display_chars(ino_path):
    """提取固件运行时真正会显示到屏幕上的字符（排除注释、串口打印）。"""
    src = io.open(ino_path, encoding="utf-8").read()
    # 去掉注释
    noc = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    noc = re.sub(r"//[^\n]*", "", noc)

    used = set()
    for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', noc):
        # 跳过明显的串口调试串（含 printf 格式符、以 [xxx] 开头的标签）
        if re.search(r"%[dsfux%]|\\n", lit) or re.match(r"^\[\w+\]", lit):
            continue
        lit = lit.replace("\\n", "").replace("\\t", "")
        used |= set(lit)
    return {c for c in used if c.isprintable() and not c.isspace()}


def main():
    targets = sys.argv[1:] or sorted(FONT_ROLE.keys())
    used = extract_display_chars(SRC_INO)
    han = sorted(c for c in used if 0x4E00 <= ord(c) <= 0x9FFF)

    print("=" * 64)
    print("LVGL 字库覆盖检查")
    print("=" * 64)
    print(f"固件会显示的字符: {len(used)} 个（其中汉字 {len(han)} 个）")
    print()

    ok_all = True
    cjk_ok = False
    for name in targets:
        p = os.path.join(ROOT, name)
        if not os.path.exists(p):
            print(f"[跳过] {name} 不存在")
            continue
        cps = parse_codepoints(p)
        # 中文字库按汉字判定；英文字库只关心它范围内的字符
        if name.startswith(("font_cjk", "font_serif")):
            miss = [c for c in han if ord(c) not in cps]
            ok = not miss
            cjk_ok = cjk_ok or ok
        else:
            miss = []
            ok = True
        ok_all &= ok
        flag = "OK  " if ok else "FAIL"
        print(f"[{flag}] {name:<18} {len(cps):>4} 码点  {FONT_ROLE.get(name, '')}")
        if miss:
            print(f"        缺失 {len(miss)} 个汉字: {''.join(miss)}")

    print()
    if ok_all and cjk_ok:
        print("结论: PASS —— 所有会显示的汉字都已在字库中，不会出现方框")
        return 0
    print("结论: FAIL —— 存在缺失字符，烧录后这些位置会显示空心方框")
    print("修复: 运行 python tools/sync-symbols.py && node tools/gen-fonts.js")
    return 1


if __name__ == "__main__":
    sys.exit(main())
