#include "qmi8658.h"
#include <Wire.h>
#include "config.h"

// QMI8658 寄存器地址（参照官方 esp32_s3_szp.h 枚举）
#define QMI8658_WHO_AM_I  0x00
#define QMI8658_CTRL1     0x02
#define QMI8658_CTRL2     0x03
#define QMI8658_CTRL3     0x04
#define QMI8658_CTRL7     0x08
#define QMI8658_STATUS0   0x2E
#define QMI8658_AX_L      0x35
#define QMI8658_RESET     0x60

void Qmi8658::writeReg(uint8_t reg, uint8_t val)
{
  Wire.beginTransmission(QMI8658_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

uint8_t Qmi8658::readReg(uint8_t reg)
{
  Wire.beginTransmission(QMI8658_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return 0;
  Wire.requestFrom((int)QMI8658_ADDR, 1);
  if (Wire.available()) return (uint8_t)Wire.read();
  return 0;
}

bool Qmi8658::init()
{
  // 检查 WHO_AM_I，最多尝试 3 次，避免芯片缺失时卡死
  uint8_t id = 0;
  for (int i = 0; i < 3; i++) {
    id = readReg(QMI8658_WHO_AM_I);
    if (id == 0x05) break;
    delay(100);
  }
  if (id != 0x05) return false;

  writeReg(QMI8658_RESET, 0xB0);  // 复位
  delay(10);
  writeReg(QMI8658_CTRL1, 0x40);  // 地址自动增加
  writeReg(QMI8658_CTRL7, 0x03);  // 使能加速度计和陀螺仪
  writeReg(QMI8658_CTRL2, 0x95);  // ACC 4g 250Hz
  writeReg(QMI8658_CTRL3, 0xD5);  // GYR 512dps 250Hz
  return true;
}

bool Qmi8658::fetchAngles(Qmi8658Data& d)
{
  uint8_t status = readReg(QMI8658_STATUS0);
  if (!(status & 0x03)) return false;  // 数据未就绪

  // 连续读 12 字节：AX AY AZ GX GY GZ（小端）
  Wire.beginTransmission(QMI8658_ADDR);
  Wire.write(QMI8658_AX_L);
  Wire.endTransmission(false);
  Wire.requestFrom((int)QMI8658_ADDR, 12);
  if (Wire.available() < 12) return false;

  uint8_t raw[12];
  for (int i = 0; i < 12; i++) raw[i] = (uint8_t)Wire.read();

  int16_t buf[6];
  for (int i = 0; i < 6; i++) {
    buf[i] = (int16_t)(raw[2 * i] | (raw[2 * i + 1] << 8));
  }

  float ax = buf[0], ay = buf[1], az = buf[2];
  float t = ax / sqrtf(ay * ay + az * az);
  d.angleX = atanf(t) * 57.29578f;
  t = ay / sqrtf(ax * ax + az * az);
  d.angleY = atanf(t) * 57.29578f;
  t = sqrtf(ax * ax + ay * ay) / az;
  d.angleZ = atanf(t) * 57.29578f;
  return true;
}
