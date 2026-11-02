#pragma once

// ---------------------------------------------------------------------------
// 立创·实战派 ESP32-S3 (LCKFB-SZPI-ESP32-S3-VA) 硬件配置
// ---------------------------------------------------------------------------

// --- LCD (ST7789, 4 线 SPI) ---
#define LCD_MOSI  40      // SPI MOSI (数据)
#define LCD_SCLK  41      // SPI SCLK (时钟)
#define LCD_DC    39      // 数据/命令选择 (DC)
#define LCD_CS    -1      // CS 由 PCA9557 IO0 控制，软件里单独拉低
#define LCD_RST   -1      // 无硬件复位引脚，使用软件复位 (SWRESET)
#define LCD_BL    42      // 背光（板载 P-MOS 反相，低电平点亮）
#define LCD_BL_ON_LEVEL LOW

// swap_xy(true) 后的逻辑分辨率（横屏）
#define LCD_H_RES  320
#define LCD_V_RES  240

// --- I2C (PCA9557 / QMI8658 / ES8311 共用) ---
#define I2C_SDA        1
#define I2C_SCL        2

// --- PCA9557 GPIO 扩展器 (0x19) ---
//   IO0 = LCD_CS（低电平选中）
//   IO1 = PA_EN（音频功放使能）
//   IO2 = DVP_PWDN（摄像头电源）
#define PCA9557_ADDR     0x19
#define PCA9557_OUT      0x01
#define PCA9557_CFG      0x03
#define LCD_CS_BIT       0x01
#define PA_EN_BIT        0x02
#define DVP_PWDN_BIT     0x04

// --- QMI8658 姿态传感器 (0x6A) ---
#define QMI8658_ADDR     0x6A

// --- ES8311 音频编解码 (0x18) ---
#define ES8311_ADDR      0x18

// --- I2S 音频输出 (ES8311 DAC) ---
#define I2S_MCK_IO       38
#define I2S_BCK_IO       14
#define I2S_WS_IO        13
#define I2S_DO_IO        45
#define AUDIO_SAMPLE_RATE 16000
#define AUDIO_MCLK_MULT   384          // MCLK = 16000 * 384 = 6.144 MHz
#define AUDIO_VOLUME      70           // 0~100

// --- 接收机 PWM 输入通道（GH1.25 扩展接口 GPIO13/14/23/24，共 4 通道）---
#define THROTTLE_PIN     13           // 油门通道（比例，前推引擎/拉回倒车）
#define POLICE_PIN       14           // 警笛声开关
#define AMBULANCE_PIN    23           // 救护车声开关
#define FIRE_PIN         24           // 消防车声开关

// PWM 微秒阈值
#define PWM_NEUTRAL      1500
#define PWM_DEADBAND     40
#define PWM_MIN          1000
#define PWM_MAX          2000

// 开关通道的判 ON 阈值（大于此值视为打开）
#define SW_ON_US         1500
