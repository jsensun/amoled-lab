// LVGL 字库生成器 —— DOT 游戏工程
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
/* 中文字体也带上 ASCII 可打印区 + 中间点(·,0xB7), 支持中英混排 */
const cjkRangeFull = cjkRange + ',0x20-0x7E,0xB7';
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

conv(mont, 40,  'font_game_40', '0x30-0x39', 'font_game_40.c');   // 2048 方块数字 40px
conv(mont, 20,  'font_ui_20',   '0x20-0x5A,0xB7', 'font_ui_20.c'); // 小字UI 20px
conv(noto, 20,  'font_cjk_20',  cjkRangeFull, 'font_cjk_20.c');    // 中文黑体 20px (含ASCII)

console.log('ALL FONTS DONE');
