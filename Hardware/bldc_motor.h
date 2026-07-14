/**
  ******************************************************************************
  * @file    bldc_motor.h
  * @brief   无刷电机（BLDC）串口通信驱动 —— 头文件
  * @note
  *   本驱动面向「串口协议型」无刷电机驱动器（如常见的一体式舵机/无刷模组）。
  *   MCU 通过 USART2（PA2=TX, PA3=RX）按固定帧格式下发指令，
  *   并用 DMA + 空闲中断接收电机回传的位置反馈。
  *
  *   通信帧格式（发送/接收思路一致）：
  *   ┌────────┬────┬─────┬──────────┬─────┬────────┐
  *   │ 帧头   │ ID │ CMD │  数据区  │ BCC │  帧尾  │
  *   │ 0x7A   │1B  │ 1B  │ N 字节   │ XOR │  0x7B  │
  *   └────────┴────┴─────┴──────────┴─────┴────────┘
  *   BCC = 帧头到数据区末字节的逐字节异或（XOR）
  *
  *   硬件连接建议：
  *   - USART2_TX -> 电机驱动器 RX
  *   - USART2_RX -> 电机驱动器 TX
  *   - 共地（GND 必须连接）
  *   - 波特率默认 115200（与 BLDC_Init 传入参数一致）
  ******************************************************************************
  */

#ifndef __BLDC_MOTOR_H
#define __BLDC_MOTOR_H

#include "stm32f10x.h"

/* ========================= 协议帧定界符 ========================= */
/** 帧头：每条指令/反馈的起始字节 */
#define FRAME_HEADER  0x7A
/** 帧尾：每条指令/反馈的结束字节 */
#define FRAME_FOOTER  0x7B

/* ========================= 电机控制模式宏定义 =========================
 * 说明：不同模式决定「速度指令」与「位置指令」如何配合工作。
 * 实际写入驱动器时，通过 BLDC_Set_Mode(id, mode) 下发。
 * 模式编号以驱动器手册为准，下列为常用约定。
 * ===================================================================== */
/** 速度模式：只按目标转速旋转，不跟踪绝对位置 */
#define MOTOR_MODE_SPEED            0
/** 多圈位置模式 + T 型轨迹规划：可跨多圈，加减速更平滑（推荐用于往返定位） */
#define MOTOR_MODE_POS_MULTI_T      1
/** 单圈位置模式 + T 型轨迹规划：限制在 0~1 圈内定位，带平滑加减速 */
#define MOTOR_MODE_POS_SINGLE_T     2
/** 多圈位置模式（直接/无轨迹）：响应更快，但启停可能较硬 */
#define MOTOR_MODE_POS_MULTI_DIR    3
/** 单圈位置模式（直接/无轨迹） */
#define MOTOR_MODE_POS_SINGLE_DIR   4

/* ========================= 电机状态结构体 =========================
 * 每个物理电机对应一个结构体实例（本工程：Motor1 / Motor2）。
 * Target_* 为“我们希望它去的地方”，Current_Position 为“驱动器回传的实际位置”。
 * ================================================================= */
typedef struct {
    uint8_t ID;                 /**< 电机通信 ID（驱动器拨码/软件设定，须与硬件一致） */
    uint8_t Mode;               /**< 当前期望控制模式（见上方 MOTOR_MODE_xxx） */
    int     Target_Speed;       /**< 目标速度（单位以驱动器为准，常见为 RPM 或内部转速单位） */
    int     Target_Position;    /**< 目标位置（编码器计数/驱动器内部位置单位） */
    int     Current_Position;   /**< 实时位置：由 USART2 中断解析反馈帧后更新 */
} BLDC_Motor_TypeDef;

/* ========================= 全局电机对象 =========================
 * 在 bldc_motor.c 中定义并初始化默认参数。
 * 使用前请确认 ID 与驱动器实际地址一致。
 * =============================================================== */
extern BLDC_Motor_TypeDef Motor1;   /**< 1 号电机（默认 ID = 1） */
extern BLDC_Motor_TypeDef Motor2;   /**< 2 号电机（默认 ID = 2） */

/* ========================= 对外 API ========================= */

/**
  * @brief  初始化 USART2 + DMA 接收 + 空闲中断
  * @param  baudrate  波特率，常用 115200
  * @note   引脚：PA2=TX，PA3=RX；DMA1_Channel6 用于 USART2_RX
  */
void BLDC_Init(uint32_t baudrate);

/**
  * @brief  使能指定 ID 的电机（退出失能/刹车态，允许运行）
  * @param  id  电机 ID
  * @note   上电后一般需先 Enable，再设模式/速度/位置
  */
void BLDC_Enable(uint8_t id);

/**
  * @brief  设置电机控制模式
  * @param  id    电机 ID
  * @param  mode  模式值，可用 MOTOR_MODE_xxx，也可直接传手册中的 16 位模式字
  */
void BLDC_Set_Mode(uint8_t id, uint8_t mode);

/**
  * @brief  设置电机目标速度
  * @param  id     电机 ID
  * @param  speed  目标速度（有符号；正反转由符号表示，具体看驱动器协议）
  * @note   在位置模式下，该速度常作为“运动过程中的最大速度/规划速度”
  */
void BLDC_Set_Speed(uint8_t id, int speed);

/**
  * @brief  设置电机目标位置
  * @param  id        电机 ID
  * @param  position  目标位置（32 位有符号，大端发送）
  */
void BLDC_Set_Position(uint8_t id, int position);

/**
  * @brief  请求电机反馈当前位置等信息
  * @param  id  电机 ID
  * @note   发送后，驱动器回传帧在 USART2_IRQHandler 中解析，
  *         结果写入对应电机的 Current_Position
  */
void BLDC_Req_Feedback(uint8_t id);

/**
  * @brief  设置加速度（用于 T 型轨迹等平滑启停）
  * @param  id   电机 ID
  * @param  acc  加速度，单位以驱动器为准（常见：转/s² 或内部加速度单位）
  * @note   加速度越小，启动/停车越柔和，到位时间越长
  */
void BLDC_Set_Acceleration(uint8_t id, uint16_t acc);

#endif /* __BLDC_MOTOR_H */
