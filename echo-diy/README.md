# Echo DIY — 随身录音便签 v1.3

基于微雪 ESP32-S3-Touch-AMOLED-1.8 V2 的录音设备：
**按 BOOT 按钮 → 录音 → WiFi 上传电脑 → Whisper 自动转写为文字**

## 硬件
- 微雪 ESP32-S3-Touch-AMOLED-1.8 V2（板载麦克风 + ES8311 音频芯片 + 1.8" AMOLED 屏）
- 无需额外硬件，用板子自带的 BOOT 按钮

## 固件烧录

### 方式一：直刷镜像（推荐，最简单）
用 ESP32 Flash Download Tool 或 esptool 烧录 `firmware/echo_diy_v1.3_merged.bin`，地址 `0x0`：

```
esptool.py --port COM3 write_flash 0x0 echo_diy_v1.3_merged.bin
```

### 方式二：arduino-cli（需改配置时）
```
arduino-cli upload -p COM3 --fqbn "esp32:esp32:esp32s3:PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=custom" echo_diy_stage3
```

## 使用步骤

1. **电脑端**：先启动接收服务器
   ```
   python pc-server/recv_http.py 8080 recordings
   ```
   （可选）再启动自动转写
   ```
   python pc-server/transcribe.py recordings
   ```

2. **板子端**：上电后按 **BOOT 按钮** 开始录音（最长 10 秒）
   - 录音中再按一次 BOOT 可提前停止
   - 录音结束自动通过 WiFi 上传到电脑
   - 上传完成回到待机，可继续录

3. **查看结果**：录音保存在 `recordings/rec_时间戳.wav`，转写文本在同名 `.txt`

## 屏幕显示反馈
板载 1.8" AMOLED 实时显示状态（英文，颜色区分）：
- 待机：`Press BOOT / to record`（白）
- 录音中：`RECORDING... / X s`（红，**剩余秒数倒数**，每秒刷新）
- 上传中：`UPLOADING...`（黄）
- 成功：`UPLOAD OK / OK X s`（绿，显示 3 秒 + 录音时长回显）
- 失败：`UPLOAD FAIL / serial fallback`（红，自动回退串口）

> v1.3 修复：按 BOOT 触发后**等待按钮释放**再开始录音，避免"按下未松手被误判为提前停止"导致空文件。

## 转写说明
- `transcribe.py` 使用 faster-whisper **small** 模型（中文识别效果好）
- 首次运行自动下载模型（约 460MB，需联网）
- 录音已做 **8x 软件增益** + **最强去噪管线**：
  1. 带通滤波 80Hz-7kHz（去低频隆隆 + 高频摩擦刺耳声）
  2. noisereduce 强降噪 `prop_decrease=0.95`（用前 0.5s 作噪声样本）
  3. 峰值归一化（滤波衰减后拉回满幅）
  4. vad 过滤静音段
- 依赖：`pip install faster-whisper noisereduce scipy`
- 实测效果：播放"你好，这是录音测试，一二三四五，今天天气不错，我们出去走一走"
  识别为"今天天气不错我们出去走一走你好"

## 串口命令（调试用）
- 发送 `r`：等效按 BOOT 按钮，触发录音
- 板子会打印状态：`RECORDING...` → `recorded xxx bytes` → `HTTP POST -> 200` → `UPLOAD_DONE`

## 配置修改
固件源码在 `echo_diy_stage3/`，常用配置在文件顶部：
- `REC_SECONDS`：最长录音秒数（默认 10）
- `WIFI_SSID` / `WIFI_PASSWORD`：WiFi 凭据（在 secrets.h）
- `PC_HOST`：电脑 IP（默认 192.168.3.67，改成你电脑的局域网 IP）
- `PC_PORT`：服务器端口（默认 8080）

## 注意事项
- 电脑 IP 要填**活跃网卡**的 IP（`ipconfig` 查看，别填断开的 WLAN）
- 首次运行服务器，Windows 防火墙可能弹窗，选"允许访问"
- 若服务器用的 python 没有防火墙放行规则，需手动加规则或换有规则的 python
- WiFi 上传失败会自动回退到 USB 串口发送（帧协议 AA 55），用 `recv_serial.py` 接收

## 文件清单
```
firmware/
  echo_diy_v1.3_merged.bin   16MB 直刷镜像（推荐，按钮释放修复 + 倒数显示）
  echo_diy_v1.3_app.bin      app 分区固件（OTA/分区烧录用）
  echo_diy_v1.2_merged.bin   v1.2 直刷镜像（增益8x + 结果回显3s）
  echo_diy_v1.2_app.bin      v1.2 app 分区固件
  echo_diy_v1.1_merged.bin   v1.1 直刷镜像（增益16x，无时长回显）
  echo_diy_v1.1_app.bin      v1.1 app 分区固件
  echo_diy_v1.0_merged.bin   v1.0 直刷镜像（无屏幕显示）
  echo_diy_v1.0_app.bin      v1.0 app 分区固件
pc-server/
  recv_http.py               接收服务器（零依赖，仅标准库）
  transcribe.py              自动转写（faster-whisper + 最强去噪）
  recv_serial.py             串口接收备用（WiFi 失败回退时用）
```