#pragma once

#include <Arduino.h>

// ES8311 音频编解码芯片（DAC 播放）驱动，基于 Arduino Wire 实现。
// 初始化序列复刻自 ESP-BSP es8311 组件（esp-bsp/components/es8311）。
class Es8311
{
public:
  // 固定 16kHz / MCLK 6.144MHz / 16-bit 输出
  static bool init();
  static void setVolume(int volume);  // 0~100
  static void mute(bool enable);

private:
  static void writeReg(uint8_t reg, uint8_t val);
  static uint8_t readReg(uint8_t reg);
  static void sampleFreqConfig();     // 16kHz 时钟分频系数
};
