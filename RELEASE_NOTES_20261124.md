# 固件 v2026.11.24-stable

## 适配硬件

- M5Stack Cardputer-Adv（ESP32-S3）
- Cap 扩展模块（CC1101 SubGHz、NFC、Grove 接口）
- 170×320 ST7789 屏幕、46 键键盘

## 本版本包含

- MP3 播放声音修复（ES8311 编解码器初始化）
- 中文歌名显示（FATFS UTF-8 编码 + WQY12 中文字库）
- NFC 读取修复（ST25R3916，ISO14443-3A 之后的卡型识别）
- SubGHz / Cap CC1101 三频段支持（RF_SW0/RF_SW1 + IOCFG2 修正）
- 菜单汉化

## 注意

- 本固件编译于 2026-11-24 12:10，**不含**随后新增的 Chain_ENV 环境监测与 NCIR2 红外测温菜单（这两项功能尚未编译与板测）。
- 对应源码提交：`dba8d7c`（README 更新）及此前的 `46f1acc`。

## 烧写方法

合并固件 `Flipper-Cardputer-Adv-Cap-merged-20261124.bin` 已包含 bootloader、分区表和应用，从 0x0 整片烧写：

```bash
esptool --chip esp32s3 write_flash 0x0 Flipper-Cardputer-Adv-Cap-merged-20261124.bin
```

## 校验

- SHA256: `460a766a376776c17f8e2a8cf60d997d4804847d1006a16e37f01bae69fe9bff`
- 大小: 2,906,880 字节
