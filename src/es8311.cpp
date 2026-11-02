#include "es8311.h"
#include <Wire.h>
#include "config.h"

// ES8311 寄存器地址
#define ES8311_RESET_REG00   0x00
#define ES8311_CLK_REG01     0x01
#define ES8311_CLK_REG02     0x02
#define ES8311_CLK_REG03     0x03
#define ES8311_CLK_REG04     0x04
#define ES8311_CLK_REG05     0x05
#define ES8311_CLK_REG06     0x06
#define ES8311_CLK_REG07     0x07
#define ES8311_CLK_REG08     0x08
#define ES8311_SDPIN_REG09   0x09
#define ES8311_SDPOUT_REG0A  0x0A
#define ES8311_SYSTEM_REG0D  0x0D
#define ES8311_SYSTEM_REG0E  0x0E
#define ES8311_SYSTEM_REG12  0x12
#define ES8311_SYSTEM_REG13  0x13
#define ES8311_ADC_REG1C     0x1C
#define ES8311_DAC_REG31     0x31
#define ES8311_DAC_REG32     0x32
#define ES8311_DAC_REG37     0x37

void Es8311::writeReg(uint8_t reg, uint8_t val)
{
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

uint8_t Es8311::readReg(uint8_t reg)
{
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return 0;
  Wire.requestFrom((int)ES8311_ADDR, 1);
  if (Wire.available()) return (uint8_t)Wire.read();
  return 0;
}

// MCLK=6.144MHz / 16kHz 对应的时钟分频系数（来自 ESP-BSP coeff_div 表）
//   pre_div=0x03 pre_multi=0x01 adc_div=0x01 dac_div=0x01 fs_mode=0
//   lrck_h=0x00 lrck_l=0xff bclk_div=0x04 adc_osr=0x10 dac_osr=0x10
void Es8311::sampleFreqConfig()
{
  uint8_t reg;

  reg = readReg(ES8311_CLK_REG02);
  reg &= 0x07;
  reg |= (0x03 - 1) << 5;   // pre_div
  reg |= 0x01 << 3;         // pre_multi
  writeReg(ES8311_CLK_REG02, reg);

  writeReg(ES8311_CLK_REG03, (0x00 << 6) | 0x10);  // fs_mode + adc_osr
  writeReg(ES8311_CLK_REG04, 0x10);                // dac_osr
  writeReg(ES8311_CLK_REG05, ((0x01 - 1) << 4) | (0x01 - 1));  // adc_div + dac_div

  reg = readReg(ES8311_CLK_REG06);
  reg &= 0xE0;
  reg |= (0x04 - 1) << 0;   // bclk_div
  writeReg(ES8311_CLK_REG06, reg);

  reg = readReg(ES8311_CLK_REG07);
  reg &= 0xC0;
  reg |= 0x00 << 0;         // lrck_h
  writeReg(ES8311_CLK_REG07, reg);

  writeReg(ES8311_CLK_REG08, 0xff);  // lrck_l
}

bool Es8311::init()
{
  // 复位
  writeReg(ES8311_RESET_REG00, 0x1F);
  delay(20);
  writeReg(ES8311_RESET_REG00, 0x00);
  writeReg(ES8311_RESET_REG00, 0x80);  // 上电

  // 时钟：使能所有时钟，MCLK 取自 MCLK 引脚，不反相
  writeReg(ES8311_CLK_REG01, 0x3F);
  sampleFreqConfig();

  // 格式：从机模式 + 16bit I2S
  uint8_t reg00 = readReg(ES8311_RESET_REG00);
  reg00 &= 0xBF;             // 从机串口
  writeReg(ES8311_RESET_REG00, reg00);
  writeReg(ES8311_SDPIN_REG09, 0x0C);   // 16bit in
  writeReg(ES8311_SDPOUT_REG0A, 0x0C);  // 16bit out

  // 上电模拟电路 + DAC + 耳机输出
  writeReg(ES8311_SYSTEM_REG0D, 0x01);
  writeReg(ES8311_SYSTEM_REG0E, 0x02);
  writeReg(ES8311_SYSTEM_REG12, 0x00);
  writeReg(ES8311_SYSTEM_REG13, 0x10);

  // 旁路 ADC/DAC 均衡器
  writeReg(ES8311_ADC_REG1C, 0x6A);
  writeReg(ES8311_DAC_REG37, 0x08);

  setVolume(AUDIO_VOLUME);
  return true;
}

void Es8311::setVolume(int volume)
{
  if (volume < 0) volume = 0;
  if (volume > 100) volume = 100;
  int reg32 = (volume == 0) ? 0 : ((volume * 256 / 100) - 1);
  writeReg(ES8311_DAC_REG32, (uint8_t)reg32);
}

void Es8311::mute(bool enable)
{
  uint8_t reg = readReg(ES8311_DAC_REG31);
  if (enable) reg |= 0x60;   // BIT(6) | BIT(5)
  else        reg &= ~0x60;
  writeReg(ES8311_DAC_REG31, reg);
}
