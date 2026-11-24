# -*- coding: utf-8 -*-
"""
生成《Flipper Cardputer 汉化对照选择表》
数据来源：D:\\esp\\ui_strings.json（从源码提取的 391 条菜单标签）+ 手工补充的通用提示语
"""
import os, sys, json, collections

try:
    import openpyxl
except ImportError:
    import subprocess
    subprocess.check_call([sys.executable, "-m", "pip", "install", "--quiet", "openpyxl>=3.1.0"])
    import openpyxl

from openpyxl.styles import Font, PatternFill, Alignment, Border, Side
from openpyxl.worksheet.datavalidation import DataValidation
from openpyxl.formatting.rule import FormulaRule
from openpyxl.utils import get_column_letter

WORK_DIR = r"D:\GEMMA4\Flipper-Zero-meets-M5Stack-Cardputer-1.16P1\Flipper-Zero-meets-M5Stack-Cardputer-1.16P1\.汉化对照选择表.ref"
OUT_XLSX = r"D:\GEMMA4\Flipper-Zero-meets-M5Stack-Cardputer-1.16P1\Flipper-Zero-meets-M5Stack-Cardputer-1.16P1\Flipper_Cardputer_汉化对照选择表.xlsx"
TITLE = "Flipper Cardputer 汉化对照选择表"

# ---------------------------------------------------------------- 颜色/样式
def xl_color(css_hex: str) -> str:
    v = css_hex.removeprefix("#").upper()
    if len(v) != 6:
        raise ValueError("Expected #RRGGBB, got: " + css_hex)
    return "FF" + v

XL_PRIMARY   = xl_color("#4472C4")
XL_LIGHT     = xl_color("#D9E2F3")
XL_ACCENT    = xl_color("#2F5597")
XL_BORDER    = xl_color("#BFBFBF")
XL_GREY_TXT  = xl_color("#808080")
XL_WARN_FILL = xl_color("#FFEB9C")
XL_WARN_TXT  = xl_color("#9C0006")
XL_KEEP_FILL = xl_color("#F2F2F2")

thin = Side(style="thin", color=XL_BORDER)
BOX = Border(left=thin, right=thin, top=thin, bottom=thin)
HDR_FONT = Font(name="微软雅黑", size=10, bold=True, color="FFFFFFFF")
HDR_FILL = PatternFill("solid", fgColor=XL_PRIMARY)
BODY_FONT = Font(name="微软雅黑", size=10)
MONO_FONT = Font(name="Consolas", size=10)
TITLE_FONT = Font(name="微软雅黑", size=14, bold=True, color="FF1A1A1A")

def style_header(ws, row, ncols):
    for c in range(1, ncols + 1):
        cell = ws.cell(row=row, column=c)
        cell.font = HDR_FONT
        cell.fill = HDR_FILL
        cell.alignment = Alignment(horizontal="center", vertical="center", wrap_text=True)
        cell.border = BOX

def style_body(ws, r1, r2, ncols, center_cols=()):
    for r in range(r1, r2 + 1):
        for c in range(1, ncols + 1):
            cell = ws.cell(row=r, column=c)
            cell.font = MONO_FONT if c in (3,) else BODY_FONT
            cell.border = BOX
            cell.alignment = Alignment(
                horizontal="center" if c in center_cols else "left",
                vertical="center", wrap_text=(c >= 3))

def keep_list_dv(ws, rng):
    dv = DataValidation(type="list", formula1='"汉化,保持英文"', allow_blank=False,
                        showDropDown=False, error="只能选择：汉化 / 保持英文",
                        errorTitle="输入无效", prompt="选 汉化 或 保持英文")
    dv.add(rng)
    ws.add_data_validation(dv)

# ---------------------------------------------------------------- 主菜单 30 项
# (appid, English, 建议中文)
MAIN_MENU = [
    ("subghz",          "Sub-GHz",           "Sub-GHz"),
    ("nfc",             "NFC",               "NFC"),
    ("infrared",        "Infrared",          "红外"),
    ("lfrfid",          "125 kHz RFID",      "125kHz RFID"),
    ("ibutton",         "iButton",           "iButton"),
    ("bad_usb",         "Bad USB",           "Bad USB"),
    ("gpio",            "GPIO",              "GPIO"),
    ("subghz_remote",   "Sub-GHz Remote",    "Sub-GHz 遥控"),
    ("nrf24",           "NRF 2.4GHz",        "NRF 2.4GHz"),
    ("wlan",            "WiFi",              "WiFi"),
    ("ble_spam",        "Bluetooth",         "蓝牙"),
    ("esp_now",         "ESP-Now",           "ESP-Now"),
    ("archive",         "Archive",           "文件管理"),
    ("clock",           "Clock",             "时钟"),
    ("mp3_player",      "MP3 Player",        "音乐播放器"),
    ("u2f",             "U2F",               "U2F"),
    ("doom",            "Doom",              "Doom"),
    ("wolf3d",          "Wolf3D",            "Wolf3D"),
    ("passport",        "Passport",          "Passport"),
    ("-",               "Settings",          "设置"),
    ("power_settings",  "Power",             "电源"),
    ("storage_settings","Storage",           "存储"),
    ("notification_settings", "Sound & Display", "声音与显示"),
    ("clock_settings",  "Clock & Alarm",     "时钟与闹钟"),
    ("desktop_settings","Desktop",           "桌面"),
    ("input_settings",  "Input",             "按键"),
    ("expansion_settings", "Expansion Modules", "扩展模块"),
    ("system_settings", "System",            "系统"),
    ("bt_settings",     "Bluetooth",         "蓝牙"),
    ("about",           "About",             "关于"),
]

# ---------------------------------------------------------------- 设置菜单
SETTINGS = [
    # desktop_settings
    ("桌面", "Auto Lock Time", "自动锁屏时间"),
    ("桌面", "Auto Lock disarm by active USB session", "USB 活动时解除自动锁"),
    ("桌面", "Battery View", "电池显示样式"),
    ("桌面", "Change Flipper Name", "修改设备名称"),
    ("桌面", "Change PIN", "修改 PIN"),
    ("桌面", "DummyMode - Down", "伪装模式-下"),
    ("桌面", "DummyMode - Down Long", "伪装模式-长按下"),
    ("桌面", "DummyMode - Left", "伪装模式-左"),
    ("桌面", "DummyMode - Left Long", "伪装模式-长按左"),
    ("桌面", "DummyMode - Ok", "伪装模式-确定"),
    ("桌面", "DummyMode - Ok Long", "伪装模式-长按确定"),
    ("桌面", "DummyMode - Right", "伪装模式-右"),
    ("桌面", "DummyMode - Right Long", "伪装模式-长按右"),
    ("桌面", "DummyMode - Up Long", "伪装模式-长按上"),
    ("桌面", "Favorite App - Left Long", "快捷应用-长按左"),
    ("桌面", "Favorite App - Left Short", "快捷应用-短按左"),
    ("桌面", "Favorite App - Ok Long", "快捷应用-长按确定"),
    ("桌面", "Favorite App - Right Long", "快捷应用-长按右"),
    ("桌面", "Favorite App - Right Short", "快捷应用-短按右"),
    ("桌面", "Happy Mode", "快乐模式"),
    ("桌面", "PIN Setup", "PIN 设置"),
    ("桌面", "Remove PIN", "移除 PIN"),
    ("桌面", "Set PIN", "设置 PIN"),
    ("桌面", "Show Clock", "显示时钟"),
    # notification_settings
    ("通知与显示", "Backlight Time", "背光时长"),
    ("通知与显示", "LCD Backlight", "屏幕背光"),
    ("通知与显示", "LED Brightness", "LED 亮度"),
    ("通知与显示", "LED Color", "LED 颜色"),
    ("通知与显示", "LED Effect", "LED 效果"),
    ("通知与显示", "LED Speed", "LED 速度"),
    ("通知与显示", "LED Timeout", "LED 超时"),
    ("通知与显示", "Night Shift", "夜间模式"),
    ("通知与显示", " . Start", "  · 开始"),
    ("通知与显示", " . End", "  · 结束"),
    ("通知与显示", "UI Background", "界面背景色"),
    ("通知与显示", "UI Foreground", "界面前景色"),
    ("通知与显示", "Volume", "音量"),
    # power
    ("电源", "Auto PowerOff", "自动关机"),
    ("电源", "Battery Info", "电池信息"),
    ("电源", "Firmware Upgrade", "固件升级"),
    ("电源", "Limit Charge", "限制充电"),
    ("电源", "Power OFF", "关机"),
    ("电源", "Reboot", "重启"),
    ("电源", "Reboot Flipper", "重启设备"),
    # system
    ("系统", "Date Format", "日期格式"),
    ("系统", "Debug", "调试"),
    ("系统", "File Naming", "文件命名"),
    ("系统", "Hand Orient", "手持方向"),
    ("系统", "Heap Trace", "堆内存跟踪"),
    ("系统", "Log Baud Rate", "日志波特率"),
    ("系统", "Log Device", "日志输出方式"),
    ("系统", "Log Level", "日志级别"),
    ("系统", "Sleep Method", "休眠方式"),
    ("系统", "Time Format", "时间格式"),
    ("系统", "Units", "单位"),
    # bt / input / expansion
    ("蓝牙", "Bluetooth", "蓝牙"),
    ("蓝牙", "Unpair All Devices", "解除所有配对"),
    ("按键", "Buttons Vibro", "按键震动"),
    ("按键", "Vibro Trigger", "震动触发条件"),
    ("扩展模块", "Listen UART", "监听 UART"),
]

# ---------------------------------------------------------------- 中文对照字典
ZH = {
    # Sub-GHz
    "Add Manually": "手动添加", "Add Manually [Advanced]": "手动添加[高级]",
    "Bin RAW": "Bin RAW", "Bruteforce": "暴力破解",
    "Counter Incr.": "计数器增量", "Counter Mode": "计数器模式",
    "Debug Pin": "调试引脚", "Decode": "解码", "Delete": "删除",
    "Delete old signals when memory is full": "满时覆盖旧信号",
    "Edit Button": "编辑按键", "Edit Counter": "编辑计数器",
    "Emulate": "模拟发射", "Export to USB": "导出到 USB",
    "Ext Amp & LEDs": "外部功放与 LED", "Freq Offset": "频率偏移",
    "Frequency": "频率", "Frequency Analyzer": "频率分析仪",
    "Hopping RSSI": "跳频 RSSI", "Ignore Alarms": "忽略报警器",
    "Ignore Nice Flor-S / Nice One": "忽略 Nice Flor-S/Nice One",
    "Ignore Princeton": "忽略 Princeton", "Ignore ReversRB2": "忽略 ReversRB2",
    "Ignore Sensors": "忽略传感器", "Jammer": "干扰器",
    "Lock Keyboard": "锁定键盘", "Modulation": "调制方式",
    "PT2262Code": "PT2262 码", "Playlist": "播放列表",
    "Protocol Names": "协议名称", "RSSI Threshold:": "RSSI 阈值：",
    "Radio Settings": "射频设置", "Read": "读取", "Read RAW": "读取 RAW",
    "Rename": "重命名", "Repeats": "重复次数", "Reset to default": "恢复默认",
    "Saved": "已保存", "Show Extra": "显示扩展项", "Signal Settings": "信号设置",
    "TX Power": "发射功率", "Te": "Te", "TimeDelay": "延时",
    # NFC
    "App info": "应用信息", "Auth As Ameebo": "伪装成 Ameebo",
    "Auth As TommyBox": "伪装成 TommyBox",
    "Auth As Xiaomi Air Purifier": "伪装成小米空气净化器",
    "Card info": "卡片信息", "Change UID": "修改 UID", "Clone": "克隆",
    "Crack nonces in MFKey32": "MFKey32 破解 nonce", "Debug": "调试",
    "Directory": "目录", "Edit UID": "编辑 UID", "Emulate UID": "模拟 UID",
    "Enter Password Manually": "手动输入密码", "Extra Actions": "更多操作",
    "Extract MFC Keys": "提取 MFC 密钥", "Field": "场强", "Info": "信息",
    "MIFARE Classic Keys": "MIFARE Classic 密钥",
    "MIFARE Ultralight C Keys": "MIFARE Ultralight C 密钥",
    "NFC-A 4-bytes UID": "NFC-A 4 字节 UID", "NFC-A 7-bytes UID": "NFC-A 7 字节 UID",
    "Read Specific Card Type": "读取指定卡类型",
    "Restore to Original State": "恢复原始状态", "Save": "保存",
    "Show Keys": "显示密钥", "Transactions": "交易记录", "Unlock": "解锁",
    "Unlock NTAG/Ultralight": "解锁 NTAG/Ultralight", "Unlock SLIX-L": "解锁 SLIX-L",
    "Unlock With Reader": "用读卡器解锁", "Unlock with Dictionary": "字典解锁",
    "Update from Initial Card": "从原始卡更新", "Wipe (Chameleon)": "擦除(Chameleon)",
    "Write": "写入", "Write (Copy Key)": "写入(复制密钥)",
    "Write (Keep Key)": "写入(保留密钥)",
    # 红外
    "+": "+", "5V on GPIO": "GPIO 输出 5V", "ACs": "空调",
    "Add Button": "添加按键", "Audio": "音响", "Debug RX": "调试接收",
    "Delete Button": "删除按键", "Delete Remote": "删除遥控器", "Edit": "编辑",
    "Fans": "风扇", "GPIO Settings": "GPIO 设置", "LEDs": "灯光",
    "Learn New Remote": "学习新遥控器", "Learn New Remote RAW": "学习 RAW 遥控",
    "Move Button": "移动按键", "Projectors": "投影仪",
    "Rename Button": "重命名按键", "Rename Remote": "重命名遥控器",
    "Saved Remotes": "已保存遥控器", "Signal Output": "信号输出",
    "TVs": "电视", "Universal Remotes": "通用遥控器",
    # RFID / iButton
    "Clear T5577 Password": "清除 T5577 密码", "Emulate RAW RFID data": "模拟 RAW 数据",
    "Read ASK (FDX,Regular)": "读 ASK(FDX,常规)", "Read PSK (Indala)": "读 PSK(Indala)",
    "Read RAW RFID data": "读 RAW 数据", "Write and set password": "写入并设置密码",
    "Data Info": "数据信息", "Full Write on Same Type": "同型号全写",
    "Write ID": "写入 ID",
    # Bad USB / HID
    "Connection": "连接方式", "Keyboard Layout (global)": "键盘布局(全局)",
    "Pairing Mode": "配对模式", "Persist Pairing": "保持配对",
    "Randomize MAC Address": "随机 MAC 地址", "Randomize VID and PID": "随机 VID/PID",
    "Remove BLE Pairing": "删除蓝牙配对", "Restore BLE Defaults": "恢复蓝牙默认",
    "Restore USB Defaults": "恢复 USB 默认", "Set Device Name": "设置设备名",
    "Set MAC Address": "设置 MAC 地址", "Set Manufacturer Name": "设置厂商名",
    "Set Product Name": "设置产品名", "Set VID and PID": "设置 VID/PID",
    "Apple Music macOS": "Apple Music (macOS)", "Bluetooth Remote Name": "蓝牙遥控名称",
    "Bluetooth Unpairing": "解除蓝牙配对", "Keyboard": "键盘",
    "Keynote": "Keynote", "Keynote Vertical": "Keynote 竖屏", "Media": "媒体",
    "Mouse": "鼠标", "Mouse Clicker": "鼠标连点", "Mouse Jiggler": "鼠标抖动",
    "Mouse Jiggler Stealth": "鼠标抖动(隐蔽)", "Movie": "影片", "Numpad": "数字键盘",
    "PushToTalk": "按住说话", "TikTok / YT Shorts": "TikTok/YT 短视频",
    # GPIO / NRF24 / ESP-NOW / BLE
    "Baudrate": "波特率", "DE/RE Pin": "DE/RE 引脚",
    "GPIO Manual Control": "GPIO 手动控制", "RTS/DTR Pins": "RTS/DTR 引脚",
    "UART Pins": "UART 引脚", "USB Channel": "USB 通道",
    "USB-UART Bridge": "USB-UART 桥",
    "Burst Pkts": "突发包数", "Data Rate": "数据速率", "Dwell": "驻留时间",
    "Hop Order": "跳频顺序", "MouseJacker": "鼠标劫持", "PA Level": "功放增益",
    "Spectrum Analyzer": "频谱分析仪", "Strategy": "策略", "automatic": "自动",
    "by Target": "按目标", "Sniff": "嗅探",
    "(no characteristics)": "(无特征)", "(no services)": "(无服务)",
    "Auto Walk": "自动遍历", "Spam": "泛洪", "Tracker": "追踪器", "Walk": "遍历",
    # Sub-GHz Remote
    "About": "关于", "Button": "按键", "Clear Default": "清除默认",
    "Clear Slot": "清空槽位", "Edit File": "编辑文件", "Edit Label": "编辑标签",
    "Edit Map File": "编辑映射文件", "New Map File": "新建映射文件",
    "Open Map File": "打开映射文件", "Remote_Debug": "遥控调试",
    "Set Default": "设为默认",
    # WiFi
    "Block Internet": "断开外网", "Bridge": "桥接", "Bridge Pwd": "桥接密码",
    "Bridge SSID": "桥接 SSID", "Capture Handshake": "抓取握手", "Channel": "信道",
    "Code": "代码", "Custom SSIDs": "自定义 SSID", "Deauth": "取消认证",
    "Disconnect": "断开连接", "Evil Portal": "钓鱼门户",
    "Funny SSIDs": "趣味 SSID", "Hopping": "跳频", "Inject": "注入",
    "Karma": "Karma 攻击", "MiTM": "中间人", "Package Sniffer": "数据包嗅探",
    "Payload": "载荷", "Port Scanner": "端口扫描", "Random SSIDs": "随机 SSID",
    "Rick Roll": "Rick Roll", "SSID": "SSID", "SSID Spam": "SSID 泛洪",
    "Save to": "保存到", "Sniffer": "嗅探器", "Start": "启动",
    "Store Cred": "保存凭据", "Switch Wifi": "切换 WiFi", "Template": "模板",
    "Throttle": "限速", "Update SD": "更新 SD",
    # Archive
    "Copy": "复制", "Cut": "剪切", "Move": "移动", "NewDir": "新建文件夹",
    "Paste": "粘贴", "Run": "运行", "Show": "显示",
    # 其它
    "Settings": "设置", "Bluetooth": "蓝牙", "Unpair All Devices": "解除所有配对",
    "Listen UART": "监听 UART", "Buttons Vibro": "按键震动",
    "Vibro Trigger": "震动触发条件",
    "Help": "帮助", "Load": "载入", "Measure": "测量",
    "Select Key Format": "选择密钥格式", "Video Instruction": "视频教程",
    # debug（仅备对照）
    "Assert": "断言", "Assert with message": "带消息断言", "Bias:": "偏置：",
    "Carrier": "载波", "Carrier test": "载波测试", "Check": "校验",
    "Check with message": "带消息校验", "Configure": "配置", "Contrast:": "对比度：",
    "Crash": "崩溃", "Disable": "禁用", "Dual VCP": "双 VCP", "Enable": "启用",
    "Error, then launch A": "报错后启动 A", "HID KB+Mouse": "HID 键盘+鼠标",
    "HID KB+Mouse custom ID": "HID 键鼠自定义 ID", "HID U2F": "HID U2F",
    "Halt": "停机", "Heap overflow": "堆溢出", "Heap underflow": "堆下溢",
    "Launch B": "启动 B", "Launch B, then A": "先启动 B 再 A", "Packet": "数据包",
    "Packet test": "数据包测试", "Receive Data": "接收数据", "Reg Ratio:": "稳压比：",
    "Restart": "重启", "Send Data": "发送数据", "Set Error Code": "设置错误码",
    "Single VCP": "单 VCP", "Static": "静态", "Test": "测试",
    "Test App Error": "测试应用错误", "Test Data Exchange": "测试数据交换",
    "Trigger error: GUI": "触发错误：界面", "Trigger error: silent": "触发错误：静默",
    "Tune": "调谐", "Set Error Text": "设置错误文本",
    "Airoha RACE": "Airoha RACE", "Do Nothing": "什么都不做", "Set Data": "设置数据",
    "Set MAC": "设置 MAC", "Switch View": "切换视图",
    # 第三方 FAP
    "Apple AirTag": "Apple AirTag", "Auto-Save:": "自动保存：",
    "Broadcast Interval": "广播间隔", "Configuration": "配置",
    "Frequency:": "频率：", "Google Find My": "Google Find My",
    "Hopping:": "跳频：", "Modulation:": "调制方式：",
    "OpenHaystack (.keys)": "OpenHaystack(.keys)",
    "Read Weather Station": "读取气象站", "Receive": "接收",
    "Register Tag": "注册标签", "Register Tag Manually": "手动注册标签",
    "Samsung SmartTag": "三星 SmartTag", "Saved Captures": "已保存记录",
    "Show MAC": "显示 MAC", "Sub Decode": "副解码", "TX Power:": "发射功率：",
    "Tile SmartTag": "Tile SmartTag", "Timing Tuner": "时序调节",
    "Transmit Power": "发射功率", "nRF Connect (.txt)": "nRF Connect(.txt)",
}

# 上下文差异覆盖（app 分组 -> {英文: 中文}）
ZH_OVERRIDE = {
    "main/subghz": {"Emulate": "模拟发射", "Frequency": "频率"},
    "main/nfc": {"Emulate": "模拟"},
    "main/lfrfid": {"Emulate": "模拟"},
    "main/ibutton": {"Emulate": "模拟"},
    "main/wlan_app": {"Start": "启动"},
    "components/archive": {"Run": "运行"},
}

# 建议保持英文（纯技术/品牌标识）
KEEP_EN = {
    "Bad USB", "Doom", "ESP-Now", "GPIO", "iButton", "125 kHz RFID", "NFC",
    "NRF 2.4GHz", "Passport", "Sub-GHz", "U2F", "WiFi", "Wolf3D",
    "Bin RAW", "Te", "+", "SSID", "Rick Roll", "Karma", "MiTM", "Code",
    "Airoha RACE", "Apple AirTag", "Samsung SmartTag", "Tile SmartTag",
    "OpenHaystack (.keys)", "nRF Connect (.txt)", "Google Find My",
    "Apple Music macOS", "Keynote", "TikTok / YT Shorts", "Remote_Debug",
    "Settings",
}

# 分组 -> (显示模块名, 是否默认纳入)
GROUP_META = {
    "main/subghz":                    ("Sub-GHz", "in"),
    "main/nfc":                       ("NFC", "in"),
    "main/infrared":                  ("红外", "in"),
    "main/lfrfid":                    ("125kHz RFID", "in"),
    "main/ibutton":                   ("iButton", "in"),
    "main/bad_usb":                   ("Bad USB", "in"),
    "main/gpio":                      ("GPIO", "in"),
    "main/subghz_remote":             ("Sub-GHz 遥控", "in"),
    "main/nrf24":                     ("NRF 2.4GHz", "in"),
    "main/wlan_app":                  ("WiFi", "in"),
    "main/ble_spam":                  ("蓝牙", "in"),
    "main/esp_now":                   ("ESP-Now", "in"),
    "components/archive":             ("文件管理", "in"),
    "system/hid_app":                 ("键盘布局(HID)", "in"),
    "applications_user":              ("第三方 FAP", "opt"),
    "applications/debug":             ("调试工具", "opt"),
    "applications/examples":          ("示例程序", "opt"),
    "port_apps/key_copier":           ("Key Copier", "opt"),
    "applications/services":          ("通用", "opt"),
    "components/loader":              ("通用", "opt"),
    "components/btshim":              ("通用", "opt"),
}
GROUP_ORDER = [k for k, v in GROUP_META.items() if v[1] == "in"] + \
              [k for k, v in GROUP_META.items() if v[1] == "opt"]

# ---------------------------------------------------------------- 通用与其他
COMMON_ITEMS = [
    ("底部按钮", "Config", "配置"),
    ("底部按钮", "Back", "返回"),
    ("底部按钮", "Save", "保存"),
    ("底部按钮", "Cancel", "取消"),
    ("底部按钮", "OK", "确定"),
    ("底部按钮", "Stop", "停止"),
    ("底部按钮", "Start", "启动"),
    ("底部按钮", "Select", "选择"),
    ("底部按钮", "Targets", "目标"),
    ("底部按钮", "Skip", "跳过"),
    ("底部按钮", "Resume", "继续"),
    ("底部按钮", "Reset", "重置"),
    ("底部按钮", "Next", "下一个"),
    ("底部按钮", "New", "新建"),
    ("底部按钮", "More", "更多"),
    ("底部按钮", "Erase", "擦除"),
    ("底部按钮", "Press to continue", "按任意键继续"),
    ("底部按钮", "Hold to repeat", "长按连发"),
    ("通用界面", "Locked", "已锁定"),
    ("通用界面", "Unlocked", "已解锁"),
    ("通用界面", "To unlock", "解锁请按"),
    ("通用界面", "Fixed scan...", "定频扫描..."),
    ("通用界面", "Hopper scan...", "跳频扫描..."),
    ("通用界面", "Decoding...", "解码中..."),
    ("通用界面", "Loading", "加载中"),
    ("通用界面", "Error", "错误"),
    ("通用界面", "Failed", "失败"),
    ("通用界面", "Success", "成功"),
    ("通用界面", "Unknown", "未知"),
    ("通用界面", "Yes", "是"),
    ("通用界面", "No", "否"),
    ("音乐播放器", "Play", "播放"),
    ("音乐播放器", "Pause", "暂停"),
    ("音乐播放器", "Playing", "播放中"),
    ("音乐播放器", "Paused", "已暂停"),
    ("音乐播放器", "Stopped", "已停止"),
    ("音乐播放器", "No tracks", "无曲目"),
    ("时钟", "Stop", "停止"),
    ("时钟", "Reset", "重置"),
]

# ---------------------------------------------------------------- 载入提取结果
with open(r"D:\esp\ui_strings.json", encoding="utf-8") as f:
    RAW = json.load(f)

app_rows = []
missing = []
for item in RAW:
    grp = item["group"]
    if grp not in GROUP_META:
        continue
    if grp.startswith("settings/"):     # 设置菜单单独成表
        continue
    label = item["label"]
    zh = ZH_OVERRIDE.get(grp, {}).get(label) or ZH.get(label)
    if zh is None:
        missing.append((grp, label))
        zh = label
    module = GROUP_META[grp][0]
    scope = GROUP_META[grp][1]
    rec = "保持英文" if (scope == "opt" or label in KEEP_EN) else "汉化"
    app_rows.append({"module": module, "group": grp, "en": label, "zh": zh, "rec": rec})

# 去重（同模块同英文）
_seen = set(); _tmp = []
for r in app_rows:
    k = (r["module"], r["en"])
    if k in _seen:
        continue
    _seen.add(k); _tmp.append(r)
app_rows = _tmp
app_rows.sort(key=lambda r: (GROUP_ORDER.index(r["group"]), r["en"].lower()))

if missing:
    print("!! 未翻译条目 %d 条：" % len(missing))
    for m in missing[:40]:
        print("   ", m)

# ---------------------------------------------------------------- 建簿
wb = openpyxl.Workbook()
wb.properties.title = TITLE

TOTAL_MAIN = len(MAIN_MENU)
TOTAL_SET = len(SETTINGS)
TOTAL_APP = len(app_rows)
TOTAL_CMN = len(COMMON_ITEMS)

# ============================================================ Sheet: 总览
ws = wb.active
ws.title = "总览"
ws.sheet_view.showGridLines = False
ws["A1"] = TITLE + " — 汉化范围总览"
ws.merge_cells("A1:H1")
ws["A1"].font = TITLE_FONT
ws["A1"].alignment = Alignment(horizontal="left", vertical="center")
ws.row_dimensions[1].height = 28

ws["A2"] = "先在下方确认整体范围；逐条取舍请到“主菜单 / 设置菜单 / 应用内菜单 / 通用与其他”四个明细表，那里已按建议预填“最终选择”。"
ws.merge_cells("A2:H2")
ws["A2"].font = Font(name="微软雅黑", size=9, color=XL_GREY_TXT)
ws["A2"].alignment = Alignment(vertical="center", wrap_text=True)
ws.row_dimensions[2].height = 26

# — 区块一：各明细表汇总（锚点：表头第 4 行，数据 5~8，合计 9）
ws["A4"] = "范围"; ws["B4"] = "总条目"; ws["C4"] = "建议汉化"; ws["D4"] = "当前已选"
ws["E4"] = "偏差"; ws["F4"] = "汉化占比"; ws["G4"] = "说明"
style_header(ws, 4, 7)

blocks = [
    ("主菜单",      "主菜单",      "C", "F", "G", TOTAL_MAIN, 3, "30 个主菜单入口名"),
    ("设置菜单",    "设置菜单",    "C", "H", "I", TOTAL_SET,  3, "桌面/通知/电源/系统等设置项"),
    ("应用内菜单",  "应用内菜单",  "C", "H", "I", TOTAL_APP,  3, "各应用的一级菜单项"),
    ("通用与其他",  "通用与其他",  "C", "H", "I", TOTAL_CMN,  3, "底部按钮与通用提示语"),
]
r = 5
for disp, sheet, ccol, hcol, icol, n, first, note in blocks:
    end = first + n - 1
    ws.cell(row=r, column=1, value=disp)
    ws.cell(row=r, column=2, value=f"=COUNTA({sheet}!{ccol}{first}:{ccol}{end})")
    ws.cell(row=r, column=3, value=f'=COUNTIF({sheet}!{hcol}{first}:{hcol}{end},"汉化")')
    ws.cell(row=r, column=4, value=f'=COUNTIF({sheet}!{icol}{first}:{icol}{end},"汉化")')
    ws.cell(row=r, column=5, value=f"=D{r}-C{r}")
    ws.cell(row=r, column=6, value=f'=IFERROR(D{r}/B{r},"")')
    ws.cell(row=r, column=7, value=note)
    r += 1

ws.cell(row=9, column=1, value="合计")
ws.cell(row=9, column=2, value="=SUM(B5:B8)")
ws.cell(row=9, column=3, value="=SUM(C5:C8)")
ws.cell(row=9, column=4, value="=SUM(D5:D8)")
ws.cell(row=9, column=5, value="=D9-C9")
ws.cell(row=9, column=6, value='=IFERROR(D9/B9,"")')
ws.cell(row=9, column=7, value="全部界面文案")
for c in range(1, 8):
    cell = ws.cell(row=9, column=c)
    cell.font = Font(name="微软雅黑", size=10, bold=True, color="FFFFFFFF")
    cell.fill = PatternFill("solid", fgColor=XL_ACCENT)
    cell.border = BOX
    cell.alignment = Alignment(horizontal="center" if c != 7 else "left", vertical="center")
for r in range(5, 9):
    for c in range(1, 8):
        cell = ws.cell(row=r, column=c)
        cell.font = BODY_FONT
        cell.border = BOX
        cell.alignment = Alignment(horizontal="center" if c != 7 else "left", vertical="center")
for r in list(range(5, 10)):
    ws.cell(row=r, column=6).number_format = "0.0%"

# — 区块二：应用内菜单按模块（锚点：小标题 11，表头 12，数据 13 起）
ws["A11"] = "应用内菜单 · 按模块"
ws.merge_cells("A11:F11")
ws["A11"].font = Font(name="微软雅黑", size=11, bold=True, color="FF1A1A1A")
ws["A12"] = "模块"; ws["B12"] = "条目数"; ws["C12"] = "建议汉化"; ws["D12"] = "当前已选"
ws["E12"] = "偏差"; ws["F12"] = "汉化占比"
style_header(ws, 12, 6)

mods = collections.OrderedDict()
for rw in app_rows:
    mods.setdefault(rw["module"], 0)
    mods[rw["module"]] += 1

APP_FIRST, APP_END = 3, 2 + TOTAL_APP
r = 13
for m, cnt in mods.items():
    ws.cell(row=r, column=1, value=m)
    ws.cell(row=r, column=2, value=cnt)
    ws.cell(row=r, column=3, value=f'=COUNTIFS(应用内菜单!$B${APP_FIRST}:$B${APP_END},$A{r},应用内菜单!$H${APP_FIRST}:$H${APP_END},"汉化")')
    ws.cell(row=r, column=4, value=f'=COUNTIFS(应用内菜单!$B${APP_FIRST}:$B${APP_END},$A{r},应用内菜单!$I${APP_FIRST}:$I${APP_END},"汉化")')
    ws.cell(row=r, column=5, value=f"=D{r}-C{r}")
    ws.cell(row=r, column=6, value=f'=IFERROR(D{r}/B{r},"")')
    r += 1
LAST_MOD = r - 1
ws.cell(row=r, column=1, value="小计")
ws.cell(row=r, column=2, value=f"=SUM(B13:B{LAST_MOD})")
ws.cell(row=r, column=3, value=f"=SUM(C13:C{LAST_MOD})")
ws.cell(row=r, column=4, value=f"=SUM(D13:D{LAST_MOD})")
ws.cell(row=r, column=5, value=f"=D{r}-C{r}")
ws.cell(row=r, column=6, value=f'=IFERROR(D{r}/B{r},"")')
for rr in range(13, r + 1):
    for cc in range(1, 7):
        cell = ws.cell(row=rr, column=cc)
        cell.border = BOX
        cell.font = BODY_FONT
        cell.alignment = Alignment(horizontal="center" if cc > 1 else "left", vertical="center")
    ws.cell(row=rr, column=6).number_format = "0.0%"
for cc in range(1, 7):
    cell = ws.cell(row=r, column=cc)
    cell.font = Font(name="微软雅黑", size=10, bold=True)
    cell.fill = PatternFill("solid", fgColor=XL_LIGHT)

# 偏差高亮
for rng in ("E5:E9", "E13:E%d" % r):
    ws.conditional_formatting.add(
        rng, FormulaRule(formula=[f"AND(ISNUMBER({rng.split(':')[0]}),{rng.split(':')[0]}<>0)"],
                         fill=PatternFill("solid", fgColor=XL_WARN_FILL), font=Font(color=XL_WARN_TXT)))

for col, w in zip("ABCDEFG", [16, 10, 12, 12, 8, 11, 34]):
    ws.column_dimensions[col].width = w

# ============================================================ Sheet: 主菜单
ws = wb.create_sheet("主菜单")
ws.sheet_view.showGridLines = False
ws["A1"] = "主菜单 / 设置入口名称（%d 项）" % TOTAL_MAIN
ws.merge_cells("A1:G1")
ws["A1"].font = TITLE_FONT
ws.row_dimensions[1].height = 26
ws["A2"] = "序号"; ws["B2"] = "appid"; ws["C2"] = "英文名称"; ws["D2"] = "建议中文"
ws["E2"] = "英文长"; ws["F2"] = "建议"; ws["G2"] = "最终选择"
style_header(ws, 2, 7)
for i, (appid, en, zh) in enumerate(MAIN_MENU):
    r = 3 + i
    ws.cell(row=r, column=1, value=i + 1)
    ws.cell(row=r, column=2, value=appid)
    ws.cell(row=r, column=3, value=en)
    ws.cell(row=r, column=4, value=zh)
    ws.cell(row=r, column=5, value=f"=LEN(C{r})")
    rec = "保持英文" if en in KEEP_EN else "汉化"
    ws.cell(row=r, column=6, value=rec)
    ws.cell(row=r, column=7, value=rec)
style_body(ws, 3, 2 + TOTAL_MAIN, 7, center_cols=(1, 5, 6, 7))
keep_list_dv(ws, f"F3:G{2 + TOTAL_MAIN}")
ws.freeze_panes = "A3"
ws.auto_filter.ref = f"A2:G{2 + TOTAL_MAIN}"
ws.conditional_formatting.add(f"G3:G{2 + TOTAL_MAIN}",
    FormulaRule(formula=['$G3="保持英文"'], fill=PatternFill("solid", fgColor=XL_KEEP_FILL),
                font=Font(color=XL_GREY_TXT)))
for col, w in zip("ABCDEFG", [6, 20, 20, 20, 8, 10, 11]):
    ws.column_dimensions[col].width = w

# ============================================================ 明细表工厂
def build_detail(sheet_name, title, rows, note):
    ws = wb.create_sheet(sheet_name)
    ws.sheet_view.showGridLines = False
    ws["A1"] = title
    ws.merge_cells("A1:I1")
    ws["A1"].font = TITLE_FONT
    ws.row_dimensions[1].height = 26
    ws["A2"] = note
    ws.merge_cells("A2:I2")
    ws["A2"].font = Font(name="微软雅黑", size=9, color=XL_GREY_TXT)
    hdr = ["序号", "所属应用", "英文原文", "建议中文", "英文长", "中文字数", "长度提示", "建议", "最终选择"]
    for c, h in enumerate(hdr, 1):
        ws.cell(row=3, column=c, value=h)
    style_header(ws, 3, len(hdr))
    for i, item in enumerate(rows):
        r = 4 + i
        ws.cell(row=r, column=1, value=i + 1)
        ws.cell(row=r, column=2, value=item["module"])
        ws.cell(row=r, column=3, value=item["en"])
        ws.cell(row=r, column=4, value=item["zh"])
        ws.cell(row=r, column=5, value=f"=LEN(C{r})")
        ws.cell(row=r, column=6, value=f"=LEN(D{r})")
        ws.cell(row=r, column=7, value=f'=IF(F{r}>9,"⚠ 偏长","")')
        ws.cell(row=r, column=8, value=item["rec"])
        ws.cell(row=r, column=9, value=item["rec"])
    end = 3 + len(rows)
    style_body(ws, 4, end, 9, center_cols=(1, 5, 6, 7, 8, 9))
    keep_list_dv(ws, f"H4:I{end}")
    ws.freeze_panes = "D4"
    ws.auto_filter.ref = f"A3:I{end}"
    ws.conditional_formatting.add(f"I4:I{end}",
        FormulaRule(formula=['$I4="保持英文"'], fill=PatternFill("solid", fgColor=XL_KEEP_FILL),
                    font=Font(color=XL_GREY_TXT)))
    ws.conditional_formatting.add(f"G4:G{end}",
        FormulaRule(formula=['$G4<>""'], font=Font(color=XL_WARN_TXT, bold=True)))
    for col, w in zip("ABCDEFGHI", [6, 18, 38, 30, 8, 9, 11, 10, 11]):
        ws.column_dimensions[col].width = w
    return end

build_detail("设置菜单", "设置菜单条目（%d 项）" % TOTAL_SET,
             [{"module": m, "en": en, "zh": zh,
               "rec": "保持英文" if en in KEEP_EN else "汉化"} for m, en, zh in SETTINGS],
             "桌面 / 通知与显示 / 电源 / 系统 / 蓝牙 / 按键 / 扩展模块。长度提示按 12px 汉字、每行约 9 字估算。")

build_detail("应用内菜单", "各应用一级菜单（%d 项）" % TOTAL_APP, app_rows,
             "按应用分组；“调试工具 / 示例程序 / 第三方 FAP / Key Copier”默认建议保持英文（不在主菜单或未编译进固件）。")

build_detail("通用与其他", "通用提示语与自定义界面（%d 项）" % TOTAL_CMN,
             [{"module": m, "en": en, "zh": zh,
               "rec": "保持英文" if en in KEEP_EN else "汉化"} for m, en, zh in COMMON_ITEMS],
             "底部按钮提示、扫描/锁定等通用文案，以及时钟、音乐播放器等自绘界面文字。")

# ============================================================ Sheet: 说明
ws = wb.create_sheet("说明与技术方案")
ws.sheet_view.showGridLines = False
ws["A1"] = "说明与技术方案"
ws.merge_cells("A1:B1")
ws["A1"].font = TITLE_FONT
ws.row_dimensions[1].height = 28
ws.column_dimensions["A"].width = 22
ws.column_dimensions["B"].width = 104

SECTIONS = [
    ("一、这张表怎么用", [
        ("第 1 步", "在“总览”看四项范围的总量、建议汉化数与占比，决定整体做到哪一层。"),
        ("第 2 步", "在“主菜单 / 设置菜单 / 应用内菜单 / 通用与其他”四个明细表里，看“英文原文 ↔ 建议中文”对照。"),
        ("第 3 步", "只改“最终选择”列（下拉：汉化 / 保持英文）。该列已按我的建议预填，你只需改动不同意的行。"),
        ("第 4 步", "改完把表发我，我按“汉化”的行实施；“总览”里的偏差列会自动显示与你选择的差异。"),
        ("小技巧", "想按应用批量改：点“所属应用”筛选 → 选中“最终选择”列区域 → 一次性下拉填充。"),
    ]),
    ("二、我的建议（先做 90 条）", [
        ("第一批", "主菜单 30 条 + 设置菜单 60 条 = 90 条。收益最高、风险最低，改完整个设备观感就变了。"),
        ("第二批", "Sub-GHz / NFC / 红外 / 125kHz RFID / iButton 五个射频应用的一级菜单（约 140 条）。"),
        ("第三批", "WiFi、蓝牙泛洪、Bad USB、文件管理、通用提示语等剩余部分。"),
        ("不建议译", "Bad USB、Sub-GHz、NFC、GPIO、iButton、U2F、WiFi、Doom、Passport 等品牌/协议名——译了反而看不懂。"),
    ]),
    ("三、中文怎么显示（技术方案）", [
        ("字体现状", "界面走 u8g2 位图字体，当前只用 u8g2_font_helvB08_tr 与 haxrcorp4089_tr，均为纯 ASCII，无法显示汉字。"),
        ("可行方案", "项目 components/u8g2/u8g2_fonts.c 里已内置 u8g2_font_wqy12_t_gb2312（文泉驿 12px，GB2312 全字库 6763 字）。"),
        ("字体体积", "该字体数组 208,526 字节（约 204 KB）。应用分区 6 MB，当前占用约 2.45 MB，剩余 3.5 MB，可容纳。"),
        ("绘制改造", "新增 canvas_draw_cjk()：逐字节判断（ASCII 单字节 / GB2312 双字节），解码成 16 位 glyph 编码后调 u8g2_DrawGlyph。约 100 行，不动现有 ASCII 路径。"),
        ("字符串存储", "源码保持 UTF-8 便于阅读，用脚本把译文转成 GB2312 字节表写入生成头文件，按字符串 ID 查表；避免运行时转码开销。"),
        ("编码限制", "GB2312 不含生僻字与 emoji。译文里出现 GB2312 外的字会在编译期报错，可及时替换。"),
    ]),
    ("四、布局约束（很关键）", [
        ("屏幕", "逻辑画布 128×64，实际放大到 232×116 显示，无裁切。"),
        ("菜单行宽", "行内可用宽度约 113px（左侧图标占 15px）。12px 汉字 → 每行最多约 9 个汉字。"),
        ("对比", "英文 6px/字符 → 约 18 字符。中文通常更短，一般不会更挤；但长协议名（如“忽略 Nice Flor-S/Nice One”）需缩写。"),
        ("长度提示", "明细表“长度提示”列对超过 9 个汉字的译文标 ⚠，请优先缩短这些。"),
        ("固定排版", "少数界面按固定字符数排布（状态栏、频率显示），改文案后需实机看一次是否对齐。"),
    ]),
    ("五、风险清单（已核对源码）", [
        ("① 名称即索引", "主菜单名字来自 .fam 的 name 字段，构建时生成 applications.c，同时用于菜单显示和按名启动。直接改 name 会让桌面“Passport”“Power”等字面量调用失效（desktop_scene_main.c:76、:148）。"),
        ("② 推荐做法", "不改 .fam name；在 loader_menu.c 里加 appid→中文名 映射，只在 menu_add_item 显示时取中文，回调仍用索引与原 name。零逻辑风险。"),
        ("③ 文案参与判断", "全仓库仅 14 处 strcmp 与字面量比较（RAW / CUSTOM / KeeLoq / Star Line / Official 等），均为文件内的协议标识，不在本次待译清单里。"),
        ("④ 协议名不可译", "Sub-GHz 保存文件里的 Protocol 字段、NFC 卡片类型名等会写进 .sub/.nfc 文件，译了会导致旧文件读不出、新文件不被识别。"),
        ("⑤ 中文宽度", "u8g2 按字模宽度推进，中英混排小字间距会略松；需在真机上看一眼状态栏是否挤。"),
        ("⑥ 回归面", "改动约 120+ 个 .c/.fam 文件，建议每批烧写后按“主菜单→各应用→设置”走一遍冒烟测试。"),
    ]),
    ("六、不在这张表里的东西", [
        ("文件名", "SD 卡目录名、资源路径（如 /ext/subghz/assets）不可译，译了资源加载失败。"),
        ("日志", "FURI_LOG_* 的英文日志保留，便于对照上游提交排错。"),
        ("协议/格式", ".sub / .nfc / .ir / .rfid 文件内的字段名保留英文，兼容官方固件。"),
        ("代码注释", "注释保留英文，便于后续同步上游。"),
    ]),
]

r = 3
for sec, items in SECTIONS:
    ws.cell(row=r, column=1, value=sec)
    ws.merge_cells(start_row=r, start_column=1, end_row=r, end_column=2)
    cell = ws.cell(row=r, column=1)
    cell.font = Font(name="微软雅黑", size=11, bold=True, color="FFFFFFFF")
    cell.fill = PatternFill("solid", fgColor=XL_PRIMARY)
    cell.alignment = Alignment(horizontal="left", vertical="center")
    ws.row_dimensions[r].height = 22
    r += 1
    for k, v in items:
        ws.cell(row=r, column=1, value=k).font = Font(name="微软雅黑", size=10, bold=True)
        ws.cell(row=r, column=1).alignment = Alignment(vertical="top", wrap_text=True)
        ws.cell(row=r, column=1).border = BOX
        c2 = ws.cell(row=r, column=2, value=v)
        c2.font = BODY_FONT
        c2.alignment = Alignment(vertical="top", wrap_text=True)
        c2.border = BOX
        lines = max(1, (len(v) // 46) + 1)
        ws.row_dimensions[r].height = 15 * lines + 4
        r += 1
    r += 1

os.makedirs(WORK_DIR, exist_ok=True)
wb.save(OUT_XLSX)
print("saved:", OUT_XLSX)
print("rows -> main:%d settings:%d app:%d common:%d" % (TOTAL_MAIN, TOTAL_SET, TOTAL_APP, TOTAL_CMN))
print("modules:", len(mods))
