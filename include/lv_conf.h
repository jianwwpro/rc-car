/**
 * LVGL 8.3 精简配置（针对本项目 ST7789 RGB565 屏）
 * 未在此定义的宏由 lv_conf_internal.h 自动填充默认值。
 */

#ifndef LV_CONF_H
#define LV_CONF_H

/* 颜色深度：RGB565（官方例程 CONFIG_LV_COLOR_16_SWAP=y，需字节交换匹配 ST7789） */
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 1

/* 内存：使用 LVGL 内置分配器，内部 RAM */
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (48 * 1024U)

/* Tick 由 loop 里的 lv_tick_inc 提供，不使用自定义 tick */
#define LV_TICK_CUSTOM 0

/* 关闭日志/调试，减小体积 */
#define LV_USE_LOG 0
#define LV_USE_DEBUG 0
#define LV_USE_ASSERT_NULL 0
#define LV_USE_ASSERT_MALLOC 0
#define LV_USE_PERF_MONITOR 0

/* 需要用到的基础组件（默认已启用，这里显式声明） */
#define LV_USE_LABEL 1
#define LV_USE_CANVAS 1

#endif
