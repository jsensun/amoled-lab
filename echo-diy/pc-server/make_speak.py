# -*- coding: utf-8 -*-
"""生成带 UTF-8 BOM 的 speak.ps1 (PowerShell 5.1 需要 BOM 才能正确读中文)"""
content = '''Add-Type -AssemblyName System.Speech
$synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
$synth.SelectVoice("Microsoft Huihui Desktop")
$synth.Volume = 100
$synth.Rate = -2
for ($i = 0; $i -lt 3; $i++) {
    $synth.Speak("你好，这是录音测试，一二三四五，今天天气不错，我们出去走一走")
    Start-Sleep -Milliseconds 800
}
'''
p = r'C:\Users\jsens_ft8b9ud\amoled-lab\echo-diy\pc-server\speak.ps1'
with open(p, 'wb') as f:
    f.write(b'\xef\xbb\xbf')  # UTF-8 BOM
    f.write(content.encode('utf-8'))
print('written with BOM')