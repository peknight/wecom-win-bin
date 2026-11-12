#!/usr/bin/env python3
"""从文泉驿微米黑派生出族名为 Tahoma 的字体。

企业微信的 CEF 输入框向 DirectWrite 请求 Tahoma，而这套 deepin-wine 的
dwrite 没有中文回退链——缺字就画 .notdef 方框。让 Tahoma 本身带汉字是
影响面最小的解法，比给整个 Wine 前缀换字体或者改系统字体文件都干净。

用法：make-tahoma.py <wqy-microhei.ttc> <输出目录>
"""

import sys
from pathlib import Path

from fontTools.ttLib import TTFont

FAMILY = "Tahoma"

# (文件名, 子族名, 完整名, PostScript 名, 字重, 是否粗体)
VARIANTS = (
    ("tahoma.ttf", "Regular", "Tahoma", "Tahoma", 400, False),
    ("tahomabd.ttf", "Bold", "Tahoma Bold", "Tahoma-Bold", 700, True),
)


def rename(font, subfamily, full_name, ps_name, weight, bold):
    table = font["name"]
    for record in list(table.names):
        args = (record.platformID, record.platEncID, record.langID)
        if record.nameID == 1:
            table.setName(FAMILY, 1, *args)
        elif record.nameID == 2:
            table.setName(subfamily, 2, *args)
        elif record.nameID == 3:
            table.setName(f"{full_name};WQY-derived", 3, *args)
        elif record.nameID == 4:
            table.setName(full_name, 4, *args)
        elif record.nameID == 6:
            table.setName(ps_name, 6, *args)

    # 排版族名会让 DirectWrite 把字体重组成 "Tahoma / Regular" 两个族，
    # 与我们要的固定映射冲突，直接去掉。
    for name_id in (16, 17, 18, 21, 22):
        table.removeNames(nameID=name_id)

    font["OS/2"].usWeightClass = weight
    if bold:
        font["head"].macStyle |= 1
    else:
        font["head"].macStyle &= ~1


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)

    ttc_path = Path(sys.argv[1])
    out_dir = Path(sys.argv[2])
    out_dir.mkdir(parents=True, exist_ok=True)

    for filename, subfamily, full_name, ps_name, weight, bold in VARIANTS:
        font = TTFont(ttc_path, fontNumber=0)
        rename(font, subfamily, full_name, ps_name, weight, bold)
        target = out_dir / filename
        font.save(target)
        print(f"{target}: {len(font.getBestCmap())} codepoints")


if __name__ == "__main__":
    main()
