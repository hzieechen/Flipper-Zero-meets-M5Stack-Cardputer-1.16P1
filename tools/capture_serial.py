#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""抓取 ESP32-S3 串口日志到文件，用于定位 MP3 播放器崩溃。

用法:
    python tools/capture_serial.py [串口] [秒数] [输出文件]

不传串口时自动探测 /dev/ttyACM* 与 /dev/ttyUSB* 的第一个。
推荐（用带 pyserial 的 IDF 虚拟环境，全路径最稳妥）:
    /home/chenli/.espressif/python_env/idf5.4_py3.13_env/bin/python \
        tools/capture_serial.py /dev/ttyACM0 45 mp3_crash_log.txt
"""

import glob
import sys
import time

import serial


def pick_port(explicit):
    if explicit:
        return explicit
    candidates = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))
    return candidates[0] if candidates else None


def main():
    explicit = None
    seconds = 45
    out_path = "mp3_crash_log.txt"
    for arg in sys.argv[1:]:
        if arg.endswith(".txt"):
            out_path = arg
        elif arg.isdigit():
            seconds = int(arg)
        else:
            explicit = arg

    port = pick_port(explicit)
    if not port:
        print("找不到串口设备。先确认设备已插好，再运行: ls /dev/ttyACM* /dev/ttyUSB*")
        print("然后把找到的节点作为第一个参数传入，例如:")
        print("  python tools/capture_serial.py /dev/ttyACM0 45 mp3_crash_log.txt")
        sys.exit(1)

    print("串口: %s | 波特率: 115200 | 时长: %ds | 输出: %s" % (port, seconds, out_path))
    print("=" * 56)
    print("脚本现在开始抓日志。请紧接着在设备上复现崩溃:")
    print("  1) 打开 MP3 Player")
    print("  2) 选一首 MP3 按 OK（WAV 也可各抓一次）")
    print("  3) 等它崩溃重启，再多等几秒让启动日志打完")
    print("=" * 56)
    sys.stdout.flush()

    try:
        ser = serial.Serial(port, 115200, timeout=0.2)
    except Exception as exc:
        print("打开串口失败: %s" % exc)
        print("若提示权限不足，先运行: sudo chmod 666 %s" % port)
        sys.exit(1)

    started = time.time()
    written = 0
    try:
        with open(out_path, "wb") as log:
            while time.time() - started < seconds:
                waiting = ser.in_waiting
                data = ser.read(waiting) if waiting else ser.read(1)
                if data:
                    log.write(data)
                    log.flush()  # 关键：实时落盘，崩溃/中断也不丢数据
                    written += len(data)
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()
    finally:
        ser.close()

    print("\n" + "=" * 56)
    print("抓取完成: %s (%d 字节)" % (out_path, written))
    print("把这个文件的完整路径告诉我，我来分析崩溃点。")


if __name__ == "__main__":
    main()
