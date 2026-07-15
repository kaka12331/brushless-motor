# 无刷电机双机平滑往返（串口协议型驱动）

基于 **STM32F103C8**，通过 **USART2** 控制两台「串口协议型」无刷电机（一体式舵机/无刷模组），使用 **多圈位置 + T 型轨迹** 在两目标点之间平滑往返，并在 OLED 上显示实时位置。

---

## 功能概述

1. 初始化 OLED、USART2（115200）+ DMA 接收  
2. 使能电机 → 设置 T 型多圈位置模式 → 设加速度 / 速度  
3. 主循环在 `0` 与 `18000`（驱动器位置单位）之间切换目标  
4. 请求位置反馈并刷新 OLED  

适合电赛备赛中需要 **柔和启停、双机同步** 的无刷定位场景。

---

## 通信协议

```text
帧头 0x7A | ID 1B | CMD 1B | 数据 N 字节 | BCC(XOR) | 帧尾 0x7B
```

BCC = 从帧头到数据区末字节的逐字节异或。

### 控制模式（驱动器约定）

| 宏 | 含义 |
|----|------|
| `MOTOR_MODE_SPEED` | 速度模式 |
| `MOTOR_MODE_POS_MULTI_T` | 多圈位置 + T 型轨迹（推荐） |
| `MOTOR_MODE_POS_SINGLE_T` | 单圈位置 + T 型轨迹 |
| `MOTOR_MODE_POS_MULTI_DIR` / `SINGLE_DIR` | 直接位置（无轨迹，响应快、启停较硬） |

---

## 硬件连接

| 信号 | STM32 | 电机驱动器 |
|------|-------|------------|
| TX | PA2 (USART2_TX) | RX |
| RX | PA3 (USART2_RX) | TX |
| GND | 共地 | 共地 |

波特率默认 **115200**。电机 ID 须与 `Motor1` / `Motor2` 结构体一致。

---

## 目录结构

```
├── User/main.c                 # 双机往返演示
├── Hardware/
│   ├── bldc_motor.c / .h       # 串口协议、模式、位置/速度指令
│   └── OLED / Key / LED
├── System/ Library/ Start/
└── Project.uvprojx
```

---

## 使用注意

- **位置单位 `18000` 不是角度**，需按减速比与编码器分辨率实机标定  
- 失能状态下设速度/位置可能无效，务必先 `BLDC_Enable`  
- 加速度偏小则更软更慢；偏大则更干脆，可能冲击变大  

---

## 相关仓库

- [dual-stepper-trapezoid](https://github.com/kaka12331/dual-stepper-trapezoid) — 步进电机梯形加减速  
- [2023-dian-sai-visual-gimbal](https://github.com/kaka12331/2023-dian-sai-visual-gimbal) — 电赛视觉云台  

用于学习与备赛交流。
