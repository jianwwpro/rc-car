#pragma once

#include <Arduino.h>

// QMI8658 六轴姿态传感器驱动（基于加速度计计算倾角），Arduino Wire 实现。
struct Qmi8658Data
{
  float angleX;  // 俯仰倾角（度）
  float angleY;  // 横滚倾角（度）
  float angleZ;  // Z 轴相对竖直方向的倾斜角（度）
};

class Qmi8658
{
public:
  static bool init();                 // 返回 false 表示芯片未找到
  static bool fetchAngles(Qmi8658Data& d);

private:
  static void writeReg(uint8_t reg, uint8_t val);
  static uint8_t readReg(uint8_t reg);
};
