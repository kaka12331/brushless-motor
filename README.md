# 无刷电机串口驱动 · 双机平滑往返（STM32）

> **平台**：STM32F103C8（标准外设库）  
> **执行器**：串口协议型无刷电机 / 一体式无刷舵机模组（双电机）  
> **通信**：USART2 @ **115200**，帧格式 `0x7A … BCC … 0x7B`  
> **接收**：DMA + 空闲中断解析位置反馈  
> **演示**：多圈位置 + **T 型轨迹**，0 ↔ 18000 平滑往返，OLED 显示实时位置  

本仓库为电赛备赛中的 **无刷电机开源工程**：MCU 不直接做六步换相，而是通过串口驱动外部无刷驱动器，完成使能、模式、速度、加速度、位置与反馈。

---

## 一、项目能做什么

上电后依次完成：

1. 初始化 OLED、USART2（115200）与 DMA 接收  
2. 双电机：使能 → 多圈位置 + T 型轨迹模式 → 设加速度 → 设规划速度  
3. 主循环在目标位置 **`0`** 与 **`18000`** 之间周期切换，两台电机同步往返  
4. 轮询请求位置反馈，在 OLED 上显示 M1 / M2 实时位置  

适合作为：

- 串口协议型无刷模组的 **驱动层模板**  
- 电赛中需要「平滑定位 / 双轴齐步」时的 **执行机构例程**  
- 与视觉云台、云台底座等方案对接的底层通信参考  

> **注意**：`18000` 是驱动器内部位置单位，不是角度本身，需按减速比与编码器分辨率实机标定。

---

## 二、系统架构

```text
┌────────────┐   USART2 115200   ┌──────────────────┐
│ STM32F103  │  PA2 TX ───────►  │  无刷驱动器 M1   │──► 无刷电机 1
│  + OLED    │  PA3 RX ◄───────  │  无刷驱动器 M2   │──► 无刷电机 2
└────────────┘   GND 共地        └──────────────────┘
       │
       │ 软件 I2C
       ▼
   OLED 0.96"
```

### 软件数据流

```text
main 配置：Enable → Mode(T型多圈) → Acc → Speed
        │
        ├─ 周期：BLDC_Set_Position(id, 0 或 18000)
        │
        └─ 周期：BLDC_Req_Feedback(id)
                      │
              DMA + IDLE 收满一帧
                      │
              解析位置 → Motorx.Current_Position
                      │
              OLED 显示
```

---

## 三、串口协议

### 3.1 帧格式

```text
帧头 0x7A | ID 1B | CMD 1B | 数据区 N 字节 | BCC(XOR) | 帧尾 0x7B
```

- **BCC**：从帧头到数据区末字节的 **逐字节异或（XOR）**  
- 发送与接收校验方式一致  

### 3.2 本工程使用的指令码（以代码为准，详细以电机手册为准）

| CMD  | 含义 |
|------|------|
| `0x00` | 设置模式 |
| `0x01` | 设置速度 |
| `0x02` | 设置位置 |
| `0x06` | 电机使能 |
| `0x07` | 设置加速度 |
| `0x0E` | 请求反馈（位置等） |

### 3.3 控制模式

| 宏 | 含义 |
|----|------|
| `MOTOR_MODE_SPEED` | 速度模式 |
| `MOTOR_MODE_POS_MULTI_T` | **多圈位置 + T 型轨迹（推荐往返定位）** |
| `MOTOR_MODE_POS_SINGLE_T` | 单圈位置 + T 型轨迹 |
| `MOTOR_MODE_POS_MULTI_DIR` | 多圈位置（直接，启停偏硬） |
| `MOTOR_MODE_POS_SINGLE_DIR` | 单圈位置（直接） |

演示默认使用 **多圈 + T 型轨迹**，启停更柔和。

---

## 四、硬件接线

| 功能 | STM32 | 电机驱动侧 |
|------|-------|------------|
| TX | **PA2**（USART2_TX） | RX |
| RX | **PA3**（USART2_RX） | TX |
| GND | GND | **必须共地** |

- 波特率：**115200，8N1**  
- 默认电机 ID：`Motor1.ID = 1`，`Motor2.ID = 2`（须与驱动器拨码/软件地址一致）  
- 逻辑电平以驱动器手册为准（常见 3.3V TTL；若为 5V 需注意电平兼容）  
- OLED：见 `OLED.c`（软件 I2C，常见 PB8/PB9，以工程配置为准）  

---

## 五、目录结构

```text
User/
  main.c                 # 双机平滑往返演示
  stm32f10x_it.c / .h
Hardware/
  bldc_motor.c / .h      # 串口协议驱动 + DMA/IDLE 接收
  OLED.* / Key / LED
System/ Library/ Start/  # 延时、启动文件、标准库
Project.uvprojx          # Keil 工程
Project.code-workspace   # EIDE / VS Code
```

---

## 六、驱动 API 一览

```c
void BLDC_Init(uint32_t baudrate);           // 初始化 USART2 + DMA 接收
void BLDC_Enable(uint8_t id);                // 使能
void BLDC_Set_Mode(uint8_t id, uint8_t mode);
void BLDC_Set_Speed(uint8_t id, int speed);
void BLDC_Set_Position(uint8_t id, int position);
void BLDC_Set_Acceleration(uint8_t id, uint16_t acc);
void BLDC_Req_Feedback(uint8_t id);          // 请求反馈；位置写入 Motorx.Current_Position
```

全局对象：`Motor1` / `Motor2`（见 `bldc_motor.c` 默认 ID 与模式）。

### 演示参数（`main.c`，可改）

| 参数 | 默认 | 说明 |
|------|------|------|
| 模式 | 多圈 + T 轨迹 | `0x0001` |
| 加速度 | 50 | 越大越“冲”，越小越软 |
| 规划速度 | 40 | 位置模式下的过程速度 |
| 目标 A/B | 0 / 18000 | **务必标定** |
| 换向周期 | `time_count >= 150` | 约数秒一级；加速软时应加大 |

---

## 七、快速上手

1. Keil 打开 `Project.uvprojx`（或 EIDE 打开 workspace）  
2. 确认电机 ID、TX/RX 交叉、共地  
3. 编译下载，上电观察：  
   - 电机在两端点间平滑往返  
   - OLED 上 M1/M2 位置数值变化  
4. 若行程不对：只改 `target_position` 的两个端点做标定  

### 常见问题

| 现象 | 排查 |
|------|------|
| 完全不动 | 是否 `BLDC_Enable`；ID 是否匹配；TX/RX 是否交叉；是否共地 |
| 偶发丢指令 | 上电后 `Delay` 是否足够；总线是否过载；驱动器是否忙 |
| 往返抢轨迹 | 换向周期太短 / 加速度太软还没到位又改目标 |
| 位置显示不更新 | DMA/IDLE 中断是否进；反馈帧格式是否与手册一致 |
| 冲击大 | 改用 T 型模式，减小速度与加速度 |
| `18000` 转太多/太少 | 位置单位与减速比相关，重新标定端点 |

---

## 八、实现要点（阅读代码时可抓）

1. **组帧发送**：`0x7A + ID + CMD + data + XOR + 0x7B`，见 `bldc_motor.c`  
2. **DMA 循环接收 + USART 空闲中断**：一帧结束再解析，避免主循环堵死等字节  
3. **双实例结构体**：`Target_*` 与 `Current_Position` 分离，便于闭环扩展  
4. **T 型轨迹交给驱动器**：MCU 只发位置/速度/加速度，平滑由模组完成  

> 注意：`USART2_IRQHandler` 在 `bldc_motor.c` 中实现，请勿在 `stm32f10x_it.c` 再定义同名函数。

---

## 九、相关仓库

| 仓库 | 说明 |
|------|------|
| [dual-stepper-trapezoid](https://github.com/kaka12331/dual-stepper-trapezoid) | 2023 E 题 · 双步进视觉云台（MCU 侧梯形加减速） |
| [2023-dian-sai-visual-gimbal](https://github.com/kaka12331/2023-dian-sai-visual-gimbal) | 2023 E 题 · 舵机视觉云台 |

---

## 十、开源说明

- 用途：学习、电赛备赛与二次开发  
- 协议细节、位置单位、模式编号 **以你手中的电机/驱动器手册为准**；本仓库按常见一体式串口无刷模组约定实现  
- 欢迎 Issue / PR 补充手册型号、实测标定参数与接线照片  

---

*STM32F103 + USART 协议型无刷 · 双机 T 型轨迹平滑往返*