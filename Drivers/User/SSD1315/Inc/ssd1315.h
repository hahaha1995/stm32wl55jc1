#ifndef SSD1315_H
#define SSD1315_H

#include <stdint.h>
#include <stdbool.h>


#define SSD1315_WIDTH 128
#define SSD1315_HEIGHT 64
#define SSD1315_PAGE_SIZE (SSD1315_HEIGHT / 8)
#define SSD1315_GDDRAM_SIZE (SSD1315_WIDTH * SSD1315_PAGE_SIZE)

// SSD1315 command definitions
#define SSD1315_CMD_DISPLAY_OFF 0xAE

typedef enum {
    SSD1315_OK       = 0x00,
    SSD1315_ERROR    = 0x01,
    SSD1315_BUSY     = 0x02,
    SSD1315_TIMEOUT  = 0x03
} ssd1315_status_t;

typedef struct {
    ssd1315_status_t (*write_cmd)(const uint8_t* data, uint16_t size);
    ssd1315_status_t (*read_status)(uint8_t* data, uint16_t size);
} ssd1315_dev_ops_t;

/// @brief  SSD1315 设备对象
typedef struct {
    ssd1315_dev_ops_t *ops;
    uint8_t gddram[SSD1315_GDDRAM_SIZE];
    ssd1315_status_t status;
    bool initialized;
    uint8_t dirty_pages;
} ssd1315_dev_obj_t;

#ifdef __cplusplus
extern "C" {
#endif

ssd1315_status_t ssd1315_Init(ssd1315_dev_obj_t *dev);
ssd1315_status_t ssd1315_Deinit(ssd1315_dev_obj_t *dev);
// void ssd1315_power_on(void);
// void ssd1315_power_off(void);
// ssd1315_status_t ssd1315_Clear(void);

#ifdef __cplusplus
}
#endif

#endif /* SSD1315_H */