# Chain ENV 环境监测 App — 详细设计文档

> 状态：**设计阶段，尚未写实现代码**。硬件未接线，设备 id 与实测读数待板测确认。
> 目标板：M5Stack Cardputer-ADV (ESP32-S3) + Cap CC1101 (SKU U219)
> 构建目标：`m5stack_cardputer_adv_cap` / `build_cap`
> 依据日期：2026-11-25

---

## 1. 需求

在 Flipper 风格主菜单新增「环境监测」app：

- 通过 Cap 的 Grove HY2.0-4P 口外接 **M5Stack Chain ENV (SKU U234)**
- 显示 **温度 / 湿度 / 气压 / 海拔** 四项读数，中文界面
- 「海拔校准」子菜单支持两种方式：
  1. 输入当地**海平面气压值**（hPa）
  2. 输入**当前海拔高度**（m），由设备反算海平面气压
- 校准结果写入 Chain ENV 自身 Flash，掉电保持

---

## 2. 关键取证结论（决定方案的硬事实）

| 事实 | 证据 | 影响 |
|---|---|---|
| Chain ENV 是 **UART 115200 8N1** 设备，内部 STM32G031 已完成 AHT20/SPA06→协议转换 | M5Stack 官方文档 + M5Chain 源码 | 不需驱动传感器，只需实现 Chain UART 协议 |
| Cap Grove J1 是 **I2C 直通**，黄线 SDA→**GPIO2**、白线 SCL→**GPIO1**，板上无其他 I2C 设备 | U219 原理图 V0.3 文本层；本项目 `board_m5stack_cardputer_adv.h:204-205`（`BOARD_PIN_QWIIC_SDA=2/SCL=1`） | 这两根线可临时改作 UART；不与键盘 I2C(GPIO8/9, I2C_NUM_0) 冲突（Grove 走 I2C_NUM_1） |
| **Cap 的 Grove 口是固定 I2C 直通，不可切换**——不像 CardputerZero 主板 HY2.0-4P 由 CM0 的 `G4_I2C/UART_SW` 电子开关切 UART(默认)/I2C | CardputerZero 官方文档；U219 无此开关 | 只能靠软件改 GPIO1/2 pinmux 作 UART（下方第 5 节），没有硬件开关可用 |
| ⚠️ 第三方硬件文档对本口 GPIO 存在**两个版本**：早期版写 SDA=G8/SCL=G9，2026-09-07 原理图修正版写 SDA=GPIO2/SCL=GPIO1 | `shaoguoji/hardware-doc` power-and-grove.md vs pinouts-and-buses.md | **以本项目板型定义 `QWIIC_SDA=2/SCL=1` 为准**（与实际编译值一致）；接线前仍建议实测确认 |
| **GPIO1 同时是红外接收 RX**（`BOARD_PIN_IR_RX=1`） | `board_m5stack_cardputer_adv.h:180` | GPIO1 被三方复用（Grove SCL / IR RX / 本 app UART RX），进出 app 必须保存/恢复 pinmux |
| 项目里 `furi_hal_serial.h` / `furi_hal_serial_control.h` **不存在**，`uart_echo` 等是未编译的遗留代码 | 全仓 find/grep 无此头文件；build_cap 无对应 obj | **不能**照抄 uart_echo 的串口 API |
| 项目已有 ESP-IDF UART driver 直接使用先例：`furi_hal_rfid.c` 用 `uart_driver_install/uart_set_pin` | `components/furi_hal/furi_hal_rfid.c:184-185` | **推荐照此模式**自建 UART |
| Chain 协议帧：`AA 55 len_lo len_hi id cmd data... crc8 55 AA`，CRC8=byte[4..size-4] 累加和 | `ChainCommon.cpp:calculateCRC/checkPacket/sendPacket` | 协议层可精确复刻 |
| 设备类型码 `CHAIN_ENV_TYPE_CODE=0x000F`；枚举 cmd=`0xFE`，心跳=`0xFD`，设备类型=`0xFB` | `ChainCommon.hpp` | 用于枚举并识别 ENV 节点 |
| 读数：AHT20 cmd=`0x30`（温度×100、湿度%）；SPA06 cmd=`0x31`（温度×100、气压 hPa×10、海拔 **cm**） | `ChainENV.cpp/hpp` | 注意单位换算 |
| 校准-写气压 cmd=`0x36`，data=u16LE(hPa×10)+save 标志；校准-按海拔 cmd=`0x38`，data=i32LE(**cm**)+save；范围 3000–11000 | `ChainENV.cpp` + `ENV_Calibrate_By_pressure.ino` | 海拔校准要求 timeout ≥5000ms（内部采样约 4s） |

---

## 3. 总体架构

```
┌──────────────────────── 应用进程 (FuriThread) ────────────────────────┐
│  env_monitor_app.c     场景机：主显示 / 校准子菜单 / 读数轮询          │
│  env_monitor_view.c    Canvas 绘制四行读数 + 状态                     │
│  env_chain.c           协议层：枚举/读数据/校准命令，帧收发+CRC         │
│  env_chain_uart.c      UART 层：ESP-IDF UART1 on GPIO1/2，pinmux 管理  │
└────────────────────────────────────────────────────────────────────────┘
                                  │
Cap Grove J1 ── SDA(GPIO2)=TX ─┐  │
            ── SCL(GPIO1)=RX ─┴──┴── Chain ENV (UART 115200)
```

**独立组件** `components/env_chain/`：`env_chain.c/.h`（协议）+ `env_chain_uart.c/.h`（硬件）。app 通过 API 调用，便于日后复用到其他 Chain 设备。

---

## 4. 逐文件改动清单

### 4.1 新建组件 `components/env_chain/`

| 文件 | 职责 |
|---|---|
| `env_chain_uart.h/.c` | UART1 初始化（115200/8N1，TX=GPIO2，RX=GPIO1），阻塞式 tx，带超时的 rx；`env_uart_enter()` / `env_uart_exit()` 保存与恢复 GPIO1/2 pinmux；引用 `driver/uart.h`、`driver/gpio.h` |
| `env_chain.h/.c` | Chain 协议：`chain_send(id,cmd,data,len)`、`chain_recv_expect(id,cmd,buf,&len,timeout_ms)`、`chain_crc8()`、`env_chain_enumerate(&id)`、`env_chain_read(env_data_t*)`、`env_chain_set_sealevel(hpa_x10)`、`env_chain_calib_by_altitude(altitude_cm)` |
| `CMakeLists.txt` | `idf_component_register(SRCS "env_chain.c" "env_chain_uart.c" INCLUDE_DIRS "." REQUIRES driver esp_driver_uart esp_timer)` |

协议层数据结构：
```c
typedef struct {
    int16_t temp_aht_x100;   /* 0.01 °C */
    uint8_t humidity;        /* % */
    uint16_t pressure_x10;   /* hPa × 10 */
    int32_t altitude_cm;     /* cm → 显示时 /100 得 m */
} env_data_t;
```

### 4.2 新建 app `applications/main/env_monitor/`

| 文件 | 职责 |
|---|---|
| `application.fam` | `appid="env_monitor"`，`name="环境监测"`，`apptype=FlipperAppType.MENUEXTERNAL`，`entry_point="env_monitor_app"`，`requires=["gui"]`，`stack_size=8*1024`，`order=86`，复用现成图标或新增 |
| `env_monitor_app.c` | 入口；FuriThread 轮询（2s 周期）读数据；ViewDispatcher + Submenu + TextInput + Popup；场景状态机 |
| `env_monitor_view.c/.h` | Canvas 绘制：标题「环境监测」+ 四行（温度/湿度/气压/海拔）+ 底部提示键位；未连接时显示「未检测到 Chain ENV」 |
| `icon.png` | 菜单图标（可先用现成 `A_Plugins_14` 占位） |

### 4.3 修改现有文件

| 文件 | 改动 |
|---|---|
| `applications/main/application.fam` | 确认新增 app 被包含（若 MENUEXTERNAL 自动发现则不改） |
| `components/furi_hal/boards/board_m5stack_cardputer_adv_cap.h` | 加注释说明 GPIO1/2 的三方复用（Grove I2C / IR RX / 本 app UART），不改宏值 |
| `README.md` | 在 Apps 段落加「环境监测」小节，说明需接 Chain ENV 到 Cap Grove 口 |

---

## 5. UART / pinmux 管理（最关键、最易出错）

### 5.1 进入 app
1. 保存 GPIO1、GPIO2 当前 pinmux（`REG_READ(GPIO_FUNC0_OUT_SEL_CFG_REG + 4*pin)`，与 `digital_sequence.c` 的 `rmt_out_sig` 缓存同法）。
2. 若红外接收线程正在运行，先停/`BOARD_HAS_IR_RX` 相关占用（设计上：进入 app 即认为用户接受 IR RX 暂不可用）。
3. `uart_driver_install(UART_NUM_1, rx_buf=256, tx_buf=0, queue=0, NULL, 0)`。
4. `uart_set_pin(UART_NUM_1, TX_IO=GPIO2, RX_IO=GPIO1, -1, -1)` + `uart_set_word_length(8)` / `parity disable` / `stop bits 1` / `uart_set_baudrate(115200)`。
   - 依据：`furi_hal_rfid.c:184-185` 同款调用。

### 5.2 退出 app
1. `uart_driver_delete(UART_NUM_1)`。
2. 恢复 GPIO1/2 的 pinmux 与方向（写回保存值，`gpio_set_direction(INPUT_OUTPUT)`）。
3. 恢复 IR RX / Qwiic I2C 原状态。

### 5.3 并发约束
- UART1 为独占资源：进入 app 后 `furi_mutex_acquire` 保护，读数线程与校准线程串行。
- 与 NFC/SubGHz 无引脚冲突（NFC=SPI3 G6/G4，SubGHz=SPI3 G5，键盘 I2C=G8/9）。

---

## 6. Chain 协议实现要点

### 6.1 帧结构（与官方 `ChainCommon.cpp` 逐字节一致）
```
offset: 0    1    2      3      4   5    6..         n-3    n-2 n-1
        AA   55   lenLo  lenHi  id  cmd  data...     crc8   55  AA
len = (lenHi<<8)|lenLo = 3 + data_len   (id+cmd+data，不含crc/尾)
crc8 = Σ byte[4 .. n-4]  (uint8 累加溢出自然回绕)
总长 = len + 6
```
- 发送：`sendPacket` 组帧，`uart_write_bytes()`。
- 接收：环形缓冲累积，`AA55` 同步 → 校验长度/尾/`crc8` → 匹配 id+cmd，超时返回 `CHAIN_TIMEOUT`。
- 枚举时接受广播 `CHAIN_ENUM_PLEASE(0xFC)` 与 `0xE0` 事件包，需忽略避免误配。

### 6.2 命令字节表

| 用途 | cmd | 请求 data | 响应 data 偏移 |
|---|---|---|---|
| 心跳探测 | `0xFD` | 无 | op status @6 |
| 枚举设备数 | `0xFE` | `0x00` | device count @6 |
| 查设备类型 | `0xFB` | 无 | type u16LE @6（0x000F=ENV） |
| 读温湿度 | `0x30` | 无 | temp i16LE @6 (×0.01°C)、humi @8 (%) |
| 读气压/海拔 | `0x31` | 无 | temp i16LE @6、pressure u16LE @8 (hPa×10)、altitude i32LE @10 (cm) |
| 写海平面气压 | `0x36` | u16LE(hPa×10) + save(0/1) | op status @6 |
| 读海平面气压 | `0x37` | 无 | u16LE @6 (hPa×10) |
| 按海拔校准 | `0x38` | i32LE(海拔 cm) + save(0/1) | op status @6 |

- 校准 save 标志：`CHAIN_SAVE_FLASH_ENABLE=1`（写入 ENV Flash，掉电保持）。
- 海平面气压合法范围：3000–11000（即 300.0–1100.0 hPa），UI 输入需校验。
- 按海拔校准 `timeout` 必须 ≥5000ms（官方 `CHAIN_ENV_CALIBRATION_MIN_TIMEOUT_MS=5000`，内部约 4s 采样）。

### 6.3 设备枚举流程（一次性，进 app 时）
1. `chain_send(0xFF, 0xFD, NULL)` 心跳确认链路活着。
2. `chain_send(0xFF, 0xFE, {0x00})` 取设备数 N。
3. 对 `id=1..N` 发 `0xFB`，找 `type==0x000F` 的 id 存为 `env_id`。
4. 找不到 → 显示「未检测到 Chain ENV，请检查接线与供电」。

> ⚠️ 官方 Chain Bridge 是否必需、以及直连时 ENV 的 id 是否为 1，**硬件未接，待板测确认**；枚举逻辑按 id 扫描，不硬编码 1。

---

## 7. UI 场景流（中文菜单）

```
[主菜单] ──「环境监测」──▶ [读数主界面]
                              │  温度 25.3 °C
                              │  湿度 61 %
                              │  气压 1008.4 hPa
                              │  海拔 42 m
                              │  [OK] 校准  [Back] 返回
                              │
                    OK ──▶ [校准子菜单]
                              ├─ 按海平面气压校准 ──▶ [数字输入 300.0~1100.0] ──▶ 发送 0x36 ──▶ [结果 Popup]
                              └─ 按当前海拔校准 ───▶ [数字输入 -500~9000 m] ────▶ 发送 0x38 ──▶ [等待~4s] ──▶ [结果 Popup]
```

- 数字输入：用 GUI 的 `TextInput` 模块（项目已有，`archive_scene_rename.c` 等在用）；header 中文，输入仅允许数字与小数点。
- 海拔输入单位 m，发送前 `×100` 转 cm；气压输入 hPa，发送前 `×10` 并四舍五入。
- 校准耗时：写气压约 100ms；按海拔约 4s，期间显示「正在校准，请稍候…」Popup，防止用户以为死机。
- 校准完自动回到读数主界面并刷新一次。

---

## 8. 构建与验证计划

### 8.1 可离线完成（本次设计后、接线前）
1. 协议层主机单测：把 `env_chain.c` 的组帧/解析函数做成可在 PC 编译的纯 C 测试，喂官方样例帧验证 CRC 与字段偏移。
2. 编译：`idf.py -B build_cap -DFLIPPER_BOARD=m5stack_cardputer_adv_cap build`（需用户终端跑，绕开 Agent 沙箱 file-read 拦截）。
3. 静态检查：pinmux 保存/恢复成对；`uart_driver_delete` 在退出路径；无栈上大缓冲。

### 8.2 待板测（接线后，用户执行）
1. 接线：Chain ENV 的 HY2.0-4P → Cap Grove J1，**注意线序**：ENV 的 TX/RX 要交叉接到 SDA/SCL 对应 GPIO；供电 5V/GND 取自 Grove。
2. 进 app 看是否枚举到 ENV（id 与 type 0x000F）。
3. 比对读数与另一台设备（或手机气压计）合理性。
4. 两种校准各测一次，断电重启看 Flash 保持。
5. 退出 app 后验证红外接收、Grove I2C 设备仍正常（pinmux 恢复）。

---

## 9. 风险与未决项

| 风险 | 说明 | 对策 |
|---|---|---|
| **GPIO1 三方复用** | Grove SCL / IR RX / UART RX 同一脚 | 进入 app 独占、退出严格恢复；app 内 IR RX 不可用（在 UI 注明） |
| **串口 HAL 缺失** | `furi_hal_serial.h` 不存在 | 用 ESP-IDF `driver/uart.h` 自建，照 `furi_hal_rfid.c` |
| **Chain Bridge 需求未明** | 官方示例经 Chain Bridge/主控，直连 Cardputer 是否需桥接未知 | 板测先试直连；不通则加 Bridge 或改接 Chain 主控方案 |
| **接线线序** | 同为 HY2.0-4P 但 ENV 是 UART、Grove 标 I2C，线序需实物核对 | 文档标明 TX↔SDA、RX↔SCL 交叉；板测用逻辑分析仪或换线验证 |
| **设备 id 未知** | 未接线，无法确认 id=1 | 枚举扫描全部 id，不硬编码 |
| **校准 Flash 寿命** | 官方提示避免反复写 | 仅用户显式点确认时 save=1；UI 加二次确认 |

---

## 10. 来源

- M5Stack 官方文档：Chain ENV <https://docs.m5stack.com/zh_CN/chain/Chain_ENV>
- M5Stack M5Chain 库：<https://github.com/m5stack/M5Chain>（`ChainCommon.cpp/hpp`、`ChainENV.cpp/hpp`、Calibrate 示例）
- Cap CC1101 硬件记录（原理图 V0.3 交叉）：`shaoguoji/hardware-doc` cap-cc1101
- 本项目：`components/furi_hal/furi_hal_rfid.c`（UART 用法）、`board_m5stack_cardputer_adv.h/.h_cap`（引脚）、`lib/digital_signal/digital_sequence.c`（pinmux 缓存模式）
