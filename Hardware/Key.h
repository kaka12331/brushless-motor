#ifndef __KEY_H
#define __KEY_H

/**
  * @brief  按键初始化
  * @param  无
  * @retval 无
  * @note   引脚：PB1=按键1，PB11=按键2（硬件预留，当前逻辑未启用），均配置为上拉输入
  */
void Key_Init(void);

/**
  * @brief  获取按键键码（阻塞式，按住不放会卡住直到松手）
  * @param  无
  * @retval 键码：0=无按键按下，1=按键1按下
  */
uint8_t Key_GetNum(void);

#endif
