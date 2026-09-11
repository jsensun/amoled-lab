Add-Type -AssemblyName System.Speech
$synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
$synth.SelectVoice("Microsoft Huihui Desktop")
$synth.Volume = 100
$synth.Rate = -2
for ($i = 0; $i -lt 3; $i++) {
    $synth.Speak("你好，这是录音测试，一二三四五，今天天气不错，我们出去走一走")
    Start-Sleep -Milliseconds 800
}
