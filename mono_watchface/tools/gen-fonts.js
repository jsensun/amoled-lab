// LVGL 字库生成器 v2 —— 尺寸对齐设计稿 + 自动修复 include 路径
// 用法: node gen-fonts.js
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..');
const SRC = path.join(ROOT, 'fonts', 'src');
const OUT = path.join(ROOT, 'fonts', 'generated');

const symbols = fs.readFileSync(path.join(__dirname, 'symbols.txt'), 'utf8').replace(/\s+/g, '');
const cps = [...new Set([...symbols])].map(c => '0x' + c.codePointAt(0).toString(16));
const cjkRange = cps.join(',');
/* 中文字体也带上 ASCII 可打印区, 支持 "2026年09月01日" 这类中英混排 */
const cjkRangeFull = cjkRange + ',0x20-0x7E';
console.log(`CJK charset: ${cps.length} glyphs`);

function fixInclude(file) {
  let t = fs.readFileSync(file, 'utf8');
  t = t.replace(/#include\s+"lvgl\/lvgl\.h"/g, '#include "lvgl.h"');
  fs.writeFileSync(file, t);
}

function conv(fontFile, size, name, range, outName, bpp) {
  console.log(`>> ${outName} (${size}px bpp${bpp || 4}, ${path.basename(fontFile)})`);
  const out = path.join(OUT, outName);
  execFileSync('npx', [
    '--yes', 'lv_font_conv@1.5.3',
    '--font', fontFile,
    '--size', String(size),
    '--bpp', String(bpp || 4),
    '--format', 'lvgl',
    '--range', range,
    '--no-compress',
    '--lv-font-name', name,
    '-o', out,
  ], { stdio: 'inherit', shell: true });
  fixInclude(out);
}

const mont = path.join(SRC, 'Montserrat-Medium.ttf');
const noto = path.join(SRC, 'NotoSansCJKsc-Medium.otf');   // 黑体(无衬线)
const serif = path.join(SRC, 'NotoSerifCJKsc-Medium.otf'); // 宋体(衬线, LIT 诗词页)

conv(mont, 118, 'font_time_118', '0x30-0x3A,0xB0', 'font_time_118.c'); // 大号时间+度符号
conv(mont, 46,  'font_temp_46',  '0x30-0x39,0xB0', 'font_temp_46.c'); // 温度
conv(mont, 30,  'font_temp_30',  '0x30-0x39,0x2E,0x25,0xB0', 'font_temp_30.c'); // 指标数值(30px, 3列排布)
conv(mont, 20,  'font_ui_20',    '0x20-0x5A,0xB7', 'font_ui_20.c');   // 小字UI: 20px(面板可读性临界点以上)
conv(noto, 20,  'font_cjk_20',   cjkRangeFull, 'font_cjk_20.c');      // 中文黑体 Medium (含ASCII)
conv(serif, 20, 'font_serif_20', cjkRangeFull, 'font_serif_20.c');    // 中文宋体 Medium (含ASCII)
conv(serif, 28, 'font_serif_28', cjkRangeFull, 'font_serif_28.c');    // 中文宋体 28px (含ASCII)

console.log('ALL FONTS DONE');
