#include <stdint.h>
#include <stdlib.h>
#include "ssd1315.h"

/**
 * @brief  ssd1315控制器初始化
 * @note   上电关显示 → 设置扫描参数（硬件相关）
                    → 设置电气参数（电压/电流/时序）
                    → 启动电荷泵升压
                    → 开显示
           这个顺序不能乱，尤其电荷泵要在开显示之前使能，否则 OLED 没有足够驱动电压，轻则不亮，重则损坏 panel。
 * @param  *dev: 
 * @retval 
 */
ssd1315_status_t ssd1315_Init(ssd1315_dev_obj_t *dev)
{
    if (dev == NULL || dev->ops == NULL || dev->ops->write_cmd == NULL || dev->ops->read_status == NULL) {
        return SSD1315_ERROR;
    }

    dev->status = SSD1315_OK;
    dev->initialized = true;

    // Send initialization commands to the SSD1315
    // 上电时控制器内部寄存器全是随机值，先关显示防止乱码/闪烁，配置完再开
    dev->ops->write_cmd((const uint8_t[]){SSD1315_CMD_DISPLAY_OFF}, 1);
    // 光标归零
    // SSD1315 用页寻址模式，每页8行，共8页。
    // 需要指定写入起始位置。
    dev->ops->write_cmd((const uint8_t[]){0x00}, 1); // 设置列地址低4位
    dev->ops->write_cmd((const uint8_t[]){0x10}, 1); // 设置列地址高4位 -> 列地址=0
    dev->ops->write_cmd((const uint8_t[]){0x40}, 1); // RAM第0行映射到屏幕第0行
    dev->ops->write_cmd((const uint8_t[]){0xB0}, 1); // 设置页地址=0 -> 从Page 0开始
    // 对比度配置
    // 控制流过 OLED 有机材料的电流大小，电流越大越亮，但寿命越短。
    // 0xFF 是最亮。
    dev->ops->write_cmd((const uint8_t[]){0x81}, 1); // 设置对比度命令
    dev->ops->write_cmd((const uint8_t[]){0xFF}, 1); // 对比度值=最大
    // 段映射+扫描方向配置
    dev->ops->write_cmd((const uint8_t[]){0xA1}, 1); // 段映射配置命令
    dev->ops->write_cmd((const uint8_t[]){0xA6}, 1); // 正常显示（0xA7=反转显示）
    dev->ops->write_cmd((const uint8_t[]){0xC8}, 1); // 扫描方向：COM从2^(8-1)扫到0（垂直镜像）
    // 多路复用比配置
    // OLED 不能同时点亮所有行，控制器采用逐行扫描，每次激活一行 COM，循环速度足够快人眼看不出闪烁。
    // 0x3F 告诉控制器屏高是 64 行
    dev->ops->write_cmd((const uint8_t[]){0xA8}, 1); // 多路复用比配置命令
    dev->ops->write_cmd((const uint8_t[]){0x3F}, 1); // 多路复用比=64（0x3F+1）
    // 显示偏移配置
    // COM 输出起始行偏移 = 0，不做垂直位移。
    // 某些屏需要偏移来对齐硬件接线。
    dev->ops->write_cmd((const uint8_t[]){0xD3}, 1); // 显示偏移配置命令
    dev->ops->write_cmd((const uint8_t[]){0x00}, 1); // 显示偏移=0
    // 时钟分频配置
    // 控制器内置 RC 振荡器驱动扫描时序，频率影响帧率和功耗。
    dev->ops->write_cmd((const uint8_t[]){0xD5}, 1); // 时钟分频配置命令
    dev->ops->write_cmd((const uint8_t[]){0x80}, 1); // 高4位 = 0x8 → 振荡器频率, 低4位 = 0x0 → 分频比 = 1
    // 预充电周期配置
    // OLED 像素是容性负载，驱动前需要预充电。
    // 时间太短驱动不足（暗），太长浪费功耗。
    dev->ops->write_cmd((const uint8_t[]){0xD9}, 1); // 预充电周期配置命令
    dev->ops->write_cmd((const uint8_t[]){0xF1}, 1); // 高4位 = 0xF → Phase 2 (预充) = 15 个时钟周期, 低4位 = 0x1 → Phase 1 (放电) = 1 个时钟周期
    // COM 引脚配置
    // 这是硬件相关配置，告诉控制器 OLED panel 的 COM 引脚是顺序排列还是交替排列（不同厂商 panel 不同），接错会出现显示错位。
    dev->ops->write_cmd((const uint8_t[]){0xDA}, 1); // COM 引脚配置命令
    dev->ops->write_cmd((const uint8_t[]){0x12}, 1); // 0x12: 顺序排列, 0x02: 交替排列
    // VCOMH 电压配置
    // VCOMH 是 COM 线的截止电压，影响对比度和显示质量。
    dev->ops->write_cmd((const uint8_t[]){0xDB}, 1); // VCOMH 电压配置命令
    dev->ops->write_cmd((const uint8_t[]){0x30}, 1); // VCOMH 电压 = 0.83*Vcc
    // 电荷泵配置
    // 这是 OLED 能否点亮的关键。
    // OLED 需要 ~7-12V 驱动电压，但 MCU 只提供 3.3V/5V。
    // 电荷泵是内置的 DC-DC 升压电路，把低压升到 OLED 所需的驱动电压。
    // 没有这条命令，屏幕一定不亮。
    dev->ops->write_cmd((const uint8_t[]){0x8D}, 1); // 电荷泵配置命令
    dev->ops->write_cmd((const uint8_t[]){0x14}, 1); // 0x14: 启用电荷泵, 0x10: 禁用电荷泵
    // 打开显示
    dev->ops->write_cmd((const uint8_t[]){0xAF}, 1); // 打开显示命令
    
    return SSD1315_OK;
}

/**
 * @brief  ssd1315控制器反初始化
 * @note   关显示 → 禁用电荷泵
 * @param  *dev: 
 * @retval 
 */
ssd1315_status_t ssd1315_Deinit(ssd1315_dev_obj_t *dev)
{
    if (dev == NULL || dev->ops == NULL || dev->ops->write_cmd == NULL) {
        return SSD1315_ERROR;
    }

    // 关显示
    dev->ops->write_cmd((const uint8_t[]){SSD1315_CMD_DISPLAY_OFF}, 1);
    // 禁用电荷泵
    dev->ops->write_cmd((const uint8_t[]){0x8D}, 1); // 电荷泵配置命令
    dev->ops->write_cmd((const uint8_t[]){0x10}, 1); // 禁用电荷泵
    
    dev->status = SSD1315_OK;
    dev->initialized = false;
    return SSD1315_OK;
}