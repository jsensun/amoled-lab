#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
sync-symbols.py —— 从固件源码自动提取字库字符集

为什么需要它：
  手工维护 symbols.txt 极易漏字。一旦代码里加了新文案却忘了同步字符集，
  LVGL 找不到字形就会画空心方框（就是那个"很多长方形"的 bug）。
  本项目已经因为这件事踩过两次坑。

  更致命的是编码问题：Windows 上用 PowerShell 改写含中文的文件会乱码，
  乱码的字符集会生成出一整套"废字库"（357 个码点全是错误字符，与正确
  字符集零重合）。本脚本全程用 UTF-8 显式读写，杜绝该问题。

用法：
  python sync-symbols.py           # 更新 symbols.txt（合并现有字符）
  python sync-symbols.py --check   # 只检查，不写文件（用于 CI / 提交前自检）

注意：
  本文件自身必须保存为 UTF-8；禁止用 PowerShell 的 Get-Content/Set-Content
  或 Out-File 读写它或 symbols.txt。
"""

import io
import re
import sys
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC_INO = os.path.join(ROOT, "mono_watchface.ino")
SYMBOLS = os.path.join(HERE, "symbols.txt")

# 排序权重：汉字在前，其次标点，最后 ASCII。保证每次生成的文件稳定不抖动。
def sort_key(ch):
    cp = ord(ch)
    if 0x4E00 <= cp <= 0x9FFF:      # CJK 基本区
        return (0, ch)
    if 0x3000 <= cp <= 0x303F:      # 中文标点
        return (1, ch)
    if 0x2010 <= cp <= 0x203B:      # 连接号/破折号/省略号
        return (2, ch)
    if 0xFF00 <= cp <= 0xFFEF:      # 全角标点
        return (3, ch)
    return (4, ch)


def strip_comments(src):
    """移除 C/C++ 注释，避免把注释里的汉字误当作用到的字。"""
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    src = re.sub(r"//[^\n]*", "", src)
    return src


def extract_used(src):
    """提取运行时真正会显示的字符串字面量中的字符。"""
    noc = strip_comments(src)
    used = set()
    # 匹配双引号字符串（支持转义）
    for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', noc):
        # 处理常见转义，避免把 \n 的 n 当成字符
        lit = lit.replace("\\n", "").replace("\\t", "").replace('\\"', '"')
        used |= set(lit)
    # 去掉空白与控制字符
    used = {c for c in used if c.isprintable() and not c.isspace()}
    return used


def main():
    check_only = "--check" in sys.argv

    src = io.open(SRC_INO, encoding="utf-8").read()
    used = extract_used(src)

    existing = set()
    if os.path.exists(SYMBOLS):
        existing = {
            c
            for c in io.open(SYMBOLS, encoding="utf-8").read()
            if c.isprintable() and not c.isspace()
        }

    missing = sorted(used - existing, key=sort_key)
    han_used = sorted(c for c in used if 0x4E00 <= ord(c) <= 0x9FFF)

    print("=" * 56)
    print("字库字符集自检")
    print("=" * 56)
    print(f"源码会显示的字符   : {len(used)} 个（其中汉字 {len(han_used)} 个）")
    print(f"symbols.txt 现有   : {len(existing)} 个")
    print(f"缺失（会导致方框）: {len(missing)} 个")
    if missing:
        print("  >> " + "".join(missing))
    else:
        print("  >> 无，字符集已完整覆盖")

    if check_only:
        return 1 if missing else 0

    # 合并：保留现有字符（可能有预留备用字）+ 补上缺失的
    combined = sorted(existing | used, key=sort_key)
    io.open(SYMBOLS, "w", encoding="utf-8", newline="\n").write("".join(combined))

    print()
    print(f"已写入 symbols.txt : {len(combined)} 个字符（新增 {len(combined) - len(existing)} 个）")
    print("下一步：运行 node tools/gen-fonts.js 重新生成字库")
    return 0


if __name__ == "__main__":
    sys.exit(main())
