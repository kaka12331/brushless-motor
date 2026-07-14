/**
  ******************************************************************************
  * @file    bldc_motor.c
  * @brief   无刷电机串口通信驱动实现
  * @details
  *   1. 组帧发送：帧头(0x7A) + ID + CMD + 数据 + BCC(异或) + 帧尾(0x7B)
  *   2. 接收：DMA1_Channel6 循环搬运 USART2 数据到缓冲区，
  *            用 USART 空闲中断（IDLE）判断“一帧收完”，再解析位置。
  *   3. 本文件内实现了 USART2_IRQHandler，请勿在 stm32f10x_it.c 中再定义同名函数。
  *
  * 指令码速查（以本工程实际使用为准，详细以电机手册为准）：
  *   0x00  设置模式
  *   0x01  设置速度
  *   0x02  设置位置
  *   0x06  电机使能
  *   0x07  设置加速度
  *   0x0E  请求反馈（位置等）
  ******************************************************************************
  */

#include "bldc_motor.h"

/* ========================= 电机对象默认参数 =========================
 * ID：须与驱动器拨码/软件地址一致
 * Mode：默认多圈位置 + T 型轨迹（平滑往返）
 * Target_Speed：默认规划速度 20（单位以驱动器为准）
 * Current_Position：上电初值为 0，收到反馈后更新
 * =================================================================== */
BLDC_Motor_TypeDef Motor1 = {
    .ID = 1,
    .Mode = MOTOR_MODE_POS_MULTI_T,
    .Target_Speed = 20,
    .Target_Position = 0,
    .Current_Position = 0
};

BLDC_Motor_TypeDef Motor2 = {
    .ID = 2,
    .Mode = MOTOR_MODE_POS_MULTI_T,
    .Target_Speed = 20,
    .Target_Position = 0,
    .Current_Position = 0
};

/**
  * DMA 接收缓冲区
  * - 下标 0：帧头
  * - 下标 1：电机 ID
  * - 下标 2：指令/反馈类型
  * - 下标 3~6：位置数据（大端 32 位，反馈帧时）
  * - 下标 7：BCC
  * - 其余：预留/帧尾等（按实际帧长使用）
  * 长度取 12 字节，大于当前解析用到的 8 字节，留余量防止溢出踩踏。
  */
uint8_t usart2_receive_data[12];

/* =====================================================================
 *                         内部工具函数（static）
 * ===================================================================== */

/**
  * @brief  USART2 阻塞发送 1 字节
  * @param  data  待发送字节
  * @note
  *   写 DR 后轮询 SR 的 TC（发送完成，bit6=0x40）标志。
  *   阻塞发送实现简单可靠；若后续要提高总线占用效率，可改 DMA 发送。
  */
static void usart2_send(uint8_t data)
{
    USART2->DR = data;
    /* 等待发送完成：SR.TC == 1 时 (SR & 0x40) 非 0 */
    while ((USART2->SR & 0x40) == 0);
}

/**
  * @brief  BCC 校验：对 data[0] ~ data[len-1] 做异或
  * @param  data  数据指针
  * @param  len   参与校验的字节数
  * @retval 异或结果（1 字节）
  * @note   发送时：BCC = 帧头+ID+CMD+数据 的 XOR
  *         接收时：用同样方式算一遍，与帧内 BCC 比较
  */
static uint8_t BCC_Sum(uint8_t *data, uint8_t len)
{
    uint8_t crc_sum = 0;
    uint8_t i;

    for (i = 0; i < len; i++) {
        crc_sum ^= data[i];
    }
    return crc_sum;
}

/**
  * @brief  统一组帧并发送一条控制指令
  * @param  id        目标电机 ID
  * @param  cmd       指令码（见文件头注释）
  * @param  data      数据区指针；无数据时可传 NULL/0，且 data_len=0
  * @param  data_len  数据区长度（字节）
  * @note
  *   帧结构：
  *     [0]     FRAME_HEADER (0x7A)
  *     [1]     id
  *     [2]     cmd
  *     [3..]   data[0..data_len-1]
  *     [..]    BCC = XOR(从帧头到数据末)
  *     [..]    FRAME_FOOTER (0x7B)
  *   tx_buf 最大 12 字节，调用时请保证 data_len 不要过大（本协议常用 0~4）。
  */
static void BLDC_Send_Cmd(uint8_t id, uint8_t cmd, uint8_t *data, uint8_t data_len)
{
    uint8_t tx_buf[12];
    uint8_t index = 0;
    uint8_t i;

    /* ---- 帧头 + 地址 + 指令 ---- */
    tx_buf[index++] = FRAME_HEADER;
    tx_buf[index++] = id;
    tx_buf[index++] = cmd;

    /* ---- 数据区（大端数据由调用方事先排好） ---- */
    for (i = 0; i < data_len; i++) {
        tx_buf[index++] = data[i];
    }

    /* ---- BCC：对当前已填入内容做异或 ---- */
    tx_buf[index] = BCC_Sum(tx_buf, index);
    index++;

    /* ---- 帧尾 ---- */
    tx_buf[index++] = FRAME_FOOTER;

    /* ---- 逐字节发出 ---- */
    for (i = 0; i < index; i++) {
        usart2_send(tx_buf[i]);
    }
}

/* =====================================================================
 *                         对外控制 API
 * ===================================================================== */

/**
  * @brief  使能电机
  * @param  id  电机 ID
  * @note   指令码 0x06，无数据区。上电初始化流程中应尽早调用。
  */
void BLDC_Enable(uint8_t id)
{
    BLDC_Send_Cmd(id, 0x06, 0, 0);
}

/**
  * @brief  设置控制模式
  * @param  id    电机 ID
  * @param  mode  模式（见 bldc_motor.h 中 MOTOR_MODE_xxx）
  * @note
  *   指令码 0x00。
  *   数据 2 字节，大端：高字节在前。
  *   例：mode=1 -> data = {0x00, 0x01}
  *   若将来模式字超过 8 位，高字节才有意义；当前宏定义多为 0~4。
  */
void BLDC_Set_Mode(uint8_t id, uint8_t mode)
{
    uint8_t data[2] = {
        (uint8_t)(mode >> 8),   /* 高 8 位 */
        (uint8_t)(mode & 0xFF)  /* 低 8 位 */
    };
    BLDC_Send_Cmd(id, 0x00, data, 2);
}

/**
  * @brief  设置目标速度
  * @param  id     电机 ID
  * @param  speed  有符号速度；按 16 位大端拆分发送
  * @note
  *   指令码 0x01。
  *   注意：int 在 Cortex-M3 上一般为 32 位，这里只取低 16 位有效（与常见协议一致）。
  *   若需要更大速度范围，请对照手册确认数据长度是否应为 32 位。
  */
void BLDC_Set_Speed(uint8_t id, int speed)
{
    uint8_t data[2] = {
        (uint8_t)(speed >> 8),
        (uint8_t)(speed & 0xFF)
    };
    BLDC_Send_Cmd(id, 0x01, data, 2);
}

/**
  * @brief  设置目标位置
  * @param  id        电机 ID
  * @param  position  32 位有符号位置，大端 4 字节发送
  * @note
  *   指令码 0x02。
  *   例：position = 18000
  *     data = {0x00, 0x00, 0x46, 0x50}  （18000 = 0x00004650）
  *   单位由驱动器编码器分辨率决定，不是“度”的直接值，需标定。
  */
void BLDC_Set_Position(uint8_t id, int position)
{
    uint8_t data[4] = {
        (uint8_t)(position >> 24),
        (uint8_t)(position >> 16),
        (uint8_t)(position >> 8),
        (uint8_t)(position & 0xFF)
    };
    BLDC_Send_Cmd(id, 0x02, data, 4);
}

/**
  * @brief  请求位置反馈
  * @param  id  电机 ID
  * @note
  *   指令码 0x0E，数据 1 字节 0x01 表示“请求位置类反馈”（以手册为准）。
  *   发送后不必轮询等待；驱动器回传后由 USART2_IRQHandler 更新
  *   Motorx.Current_Position，主循环读结构体即可。
  */
void BLDC_Req_Feedback(uint8_t id)
{
    uint8_t data[1] = {0x01};
    BLDC_Send_Cmd(id, 0x0E, data, 1);
}

/**
  * @brief  设置加速度
  * @param  id   电机 ID
  * @param  acc  加速度（16 位，大端）
  * @note
  *   指令码 0x07。
  *   配合 T 型轨迹位置模式使用时，可显著减小启动冲击。
  *   值越小越柔和，但加减速耗时更长，往返周期要相应留足时间。
  */
void BLDC_Set_Acceleration(uint8_t id, uint16_t acc)
{
    uint8_t data[2] = {
        (uint8_t)(acc >> 8),
        (uint8_t)(acc & 0xFF)
    };
    BLDC_Send_Cmd(id, 0x07, data, 2);
}

/* =====================================================================
 *                    硬件初始化：USART2 + DMA + NVIC
 * ===================================================================== */

/**
  * @brief  初始化电机通信外设
  * @param  baudrate  波特率，工程中常用 115200
  * @note
  *   时钟：
  *     - GPIOA : APB2
  *     - USART2: APB1
  *     - DMA1  : AHB
  *   引脚：
  *     - PA2 : USART2_TX  复用推挽
  *     - PA3 : USART2_RX  浮空输入
  *   接收路径：
  *     USART2_RX -> DMA1_Channel6 -> usart2_receive_data[]
  *     一帧结束靠 IDLE 中断识别（总线上静默一段时间触发）
  */
void BLDC_Init(uint32_t baudrate)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;
    DMA_InitTypeDef   DMA_InitStructure;

    /* ---------- 1. 打开外设时钟 ---------- */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    /* ---------- 2. GPIO：PA2=TX, PA3=RX ---------- */
    /* TX：复用推挽输出 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* RX：浮空输入（也可改为上拉输入，视硬件而定） */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* ---------- 3. USART2 参数 ---------- */
    USART_InitStructure.USART_BaudRate            = baudrate;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;   /* 8 位数据 */
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;      /* 1 停止位 */
    USART_InitStructure.USART_Parity              = USART_Parity_No;       /* 无校验 */
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART2, &USART_InitStructure);

    /* ---------- 4. DMA1_Channel6：USART2_RX ----------
     * 方向：外设 -> 内存
     * 缓冲区长度：10 字节（与当前反馈帧长度匹配；若手册帧更长需同步加大）
     * 模式：Normal（每帧收完后在中断里重新开启并重置计数）
     * ------------------------------------------------- */
    DMA_DeInit(DMA1_Channel6);
    DMA_InitStructure.DMA_BufferSize         = 10;
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_M2M                = DMA_M2M_Disable;
    DMA_InitStructure.DMA_MemoryBaseAddr     = (uint32_t)usart2_receive_data;
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Enable;      /* 内存地址递增 */
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)(&USART2->DR);
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable; /* 外设地址固定 DR */
    DMA_InitStructure.DMA_Priority           = DMA_Priority_High;
    DMA_Init(DMA1_Channel6, &DMA_InitStructure);
    DMA_Cmd(DMA1_Channel6, ENABLE);

    /* ---------- 5. NVIC：USART2 中断优先级 ---------- */
    NVIC_InitStructure.NVIC_IRQChannel                   = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* ---------- 6. 使能 IDLE 中断 + DMA 接收请求 + USART ---------- */
    USART_ITConfig(USART2, USART_IT_IDLE, ENABLE);   /* 总线空闲 -> 一帧可能结束 */
    USART_DMACmd(USART2, USART_DMAReq_Rx, ENABLE);   /* 允许 RX 触发 DMA */
    USART_Cmd(USART2, ENABLE);

    /* 发一个 0x00 激活线路（部分驱动器/电平转换需要“踢一脚”） */
    usart2_send(0x00);
}

/* =====================================================================
 *                 USART2 空闲中断：解析电机位置反馈
 * ===================================================================== */

/**
  * @brief  USART2 中断服务函数（本工程放在驱动 .c 中实现）
  * @note
  *   触发条件：配置了 USART_IT_IDLE。
  *   当 RX 线上“先有数据、再出现空闲”时进入本中断，视为一帧收完。
  *
  *   处理流程：
  *     1) 读 DR 清除 IDLE 标志（读 SR 再读 DR 是常见清 IDLE 写法；
  *        此处用库函数读 DR，配合前面已进入 IDLE 分支）
  *     2) 暂时关闭 DMA，避免解析时缓冲被新数据覆盖
  *     3) 校验帧头 + BCC
  *     4) 若为位置反馈（CMD=0x01），拼 32 位位置并写入对应电机
  *     5) 重置 DMA 计数并重新使能，准备收下一帧
  *
  *   当前解析约定的反馈帧（请与手册核对）：
  *     [0] 0x7A 帧头
  *     [1] 电机 ID
  *     [2] 0x01 表示位置类反馈
  *     [3..6] 当前位置（大端 int32）
  *     [7] BCC = XOR([0]..[6])
  */
void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_IDLE) != RESET) {
        /* 读 DR 有助于清除 IDLE 相关挂起状态 */
        USART_ReceiveData(USART2);

        /* 暂停 DMA，锁定当前缓冲区内容 */
        DMA_Cmd(DMA1_Channel6, DISABLE);

        /* 校验：帧头正确，且 BCC 与前 7 字节异或结果一致 */
        if (usart2_receive_data[0] == FRAME_HEADER &&
            usart2_receive_data[7] == BCC_Sum(usart2_receive_data, 7)) {

            uint8_t recv_id = usart2_receive_data[1]; /* 哪台电机回的数据 */

            /* 仅处理位置反馈指令类型 0x01 */
            if (usart2_receive_data[2] == 0x01) {
                /* 4 字节大端拼成 int32 当前位置 */
                int32_t current_pos =
                    ((int32_t)usart2_receive_data[3] << 24) |
                    ((int32_t)usart2_receive_data[4] << 16) |
                    ((int32_t)usart2_receive_data[5] << 8)  |
                    ((int32_t)usart2_receive_data[6]);

                /* 按 ID 分发到对应电机对象 */
                if (recv_id == Motor1.ID) {
                    Motor1.Current_Position = current_pos;
                } else if (recv_id == Motor2.ID) {
                    Motor2.Current_Position = current_pos;
                }
            }
        }

        /* 为下一帧重置 DMA 传输计数（必须先 Disable 再改计数再 Enable） */
        DMA_SetCurrDataCounter(DMA1_Channel6, 10);
        DMA_Cmd(DMA1_Channel6, ENABLE);
    }
}
