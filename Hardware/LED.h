#ifndef __LED_H
#define __LED_H

/**
  * @brief  LED初始化
  * @param  无
  * @retval 无
  * @note   引脚：PA1=LED1，PA2=LED2，均配置为推挽输出，初始为高电平（灯灭）
  */
void LED_Init(void);

/** @brief  LED1开启（PA1置低电平） */
void LED1_ON(void);
/** @brief  LED1关闭（PA1置高电平） */
void LED1_OFF(void);
/** @brief  LED1状态翻转 */
void LED1_Turn(void);
/** @brief  LED2开启（PA2置低电平） */
void LED2_ON(void);
/** @brief  LED2关闭（PA2置高电平） */
void LED2_OFF(void);
/** @brief  LED2状态翻转 */
void LED2_Turn(void);

#endif
