#ifndef __OLED_H
#define __OLED_H

/**
  * @brief  OLED初始化（I2C接口，PB8=SCL，PB9=SDA）
  * @param  无
  * @retval 无
  */
void OLED_Init(void);

/** @brief  清空整屏显示 */
void OLED_Clear(void);

/**
  * @brief  在指定位置显示一个字符
  * @param  Line    行位置，范围：1~4
  * @param  Column  列位置，范围：1~16
  * @param  Char    要显示的字符
  */
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char);

/**
  * @brief  在指定位置显示字符串
  * @param  Line    起始行，范围：1~4
  * @param  Column  起始列，范围：1~16
  * @param  String  要显示的字符串
  */
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String);

/**
  * @brief  在指定位置显示无符号十进制数
  * @param  Line    起始行，范围：1~4
  * @param  Column  起始列，范围：1~16
  * @param  Number  要显示的数字，范围：0~4294967295
  * @param  Length  数字长度（显示位数）
  */
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);

/**
  * @brief  在指定位置显示有符号十进制数（自带正负号）
  * @param  Line    起始行，范围：1~4
  * @param  Column  起始列，范围：1~16
  * @param  Number  要显示的数字，范围：-2147483648~2147483647
  * @param  Length  数字长度（不含符号位的显示位数）
  */
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length);

/**
  * @brief  在指定位置显示十六进制数（大写，不带0x前缀）
  * @param  Line    起始行，范围：1~4
  * @param  Column  起始列，范围：1~16
  * @param  Number  要显示的数字，范围：0~0xFFFFFFFF
  * @param  Length  数字长度（显示位数）
  */
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);

/**
  * @brief  在指定位置显示二进制数
  * @param  Line    起始行，范围：1~4
  * @param  Column  起始列，范围：1~16
  * @param  Number  要显示的数字，范围：0~1111111111111111
  * @param  Length  数字长度（显示位数）
  */
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);

#endif
