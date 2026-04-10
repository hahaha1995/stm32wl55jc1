#ifndef __OLED_H__
#define __OLED_H__

#ifdef __cplusplus
extern "C" {
#endif

#define OLED_SDA_PORT GPIOB
#define OLED_SDA_PIN GPIO_PIN_7
#define OLED_SCL_PORT GPIOB
#define OLED_SCL_PIN GPIO_PIN_8
// 评估板上3.3引脚给oled供电，软件不能控制电源，所以不定义OLED_RES_PIN

#ifdef __cplusplus
}
#endif

#endif /* __OLED_H__ */