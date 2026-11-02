#include <Arduino.h>
#include <Wire.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <driver/i2s_std.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_st7789.h>
#include <esp_heap_caps.h>

#include <lvgl.h>

#include "config.h"
#include "es8311.h"
#include "qmi8658.h"

// ---------------------------------------------------------------------------
// LCD
// ---------------------------------------------------------------------------
static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t lcd_io_handle = NULL;

static uint8_t pca9557Write(uint8_t reg, uint8_t val)
{
  Wire.beginTransmission(PCA9557_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission();
}

static uint8_t pca9557Read(uint8_t reg)
{
  Wire.beginTransmission(PCA9557_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return 0xFF;
  Wire.requestFrom((int)PCA9557_ADDR, 1);
  if (Wire.available()) return (uint8_t)Wire.read();
  return 0xFF;
}

static void pca9557SetBit(uint8_t bit, uint8_t level)
{
  uint8_t out = pca9557Read(PCA9557_OUT);
  if (out == 0xFF) out = 0x00;
  if (level) out |= bit;
  else       out &= ~bit;
  pca9557Write(PCA9557_OUT, out);
}

static void lcd_cs(uint8_t level) { pca9557SetBit(LCD_CS_BIT, level); }
static void pa_en(uint8_t level)  { pca9557SetBit(PA_EN_BIT, level); }

static void pca9557Init()
{
  // 默认值：LCD_CS=1(未选中) PA_EN=0(关功放) DVP_PWDN=1(摄像头掉电)
  pca9557Write(PCA9557_OUT, 0x05);
  // IO0~IO2 设为输出，其余输入
  pca9557Write(PCA9557_CFG, 0xF8);
}

static void backlightOn()
{
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, LCD_BL_ON_LEVEL);
}

static void bspLcdInit()
{
  esp_err_t ret;

  spi_bus_config_t buscfg = {};
  buscfg.sclk_io_num = LCD_SCLK;
  buscfg.mosi_io_num = LCD_MOSI;
  buscfg.miso_io_num = -1;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = LCD_H_RES * LCD_V_RES * 2;
  ret = spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);
  Serial.printf("[LCD] spi_bus ret=0x%X\n", (int)ret);

  esp_lcd_panel_io_spi_config_t io_config = {};
  io_config.dc_gpio_num = LCD_DC;
  io_config.cs_gpio_num = -1;            // CS 由 PCA9557 控制
  io_config.pclk_hz = 80 * 1000 * 1000;
  io_config.lcd_cmd_bits = 8;
  io_config.lcd_param_bits = 8;
  io_config.spi_mode = 2;
  io_config.trans_queue_depth = 10;
  ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &io_config, &lcd_io_handle);
  Serial.printf("[LCD] panel_io ret=0x%X\n", (int)ret);

  esp_lcd_panel_dev_config_t panel_config = {};
  panel_config.reset_gpio_num = -1;
  panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panel_config.bits_per_pixel = 16;
  ret = esp_lcd_new_panel_st7789(lcd_io_handle, &panel_config, &panel_handle);
  Serial.printf("[LCD] st7789 ret=0x%X\n", (int)ret);

  esp_lcd_panel_reset(panel_handle);
  lcd_cs(0);                             // 拉低 LCD_CS
  esp_lcd_panel_init(panel_handle);

  esp_lcd_panel_invert_color(panel_handle, true);
  esp_lcd_panel_swap_xy(panel_handle, true);
  esp_lcd_panel_mirror(panel_handle, true, false);
  esp_lcd_panel_disp_on_off(panel_handle, true);
  Serial.println("[LCD] init done");
}

// ---------------------------------------------------------------------------
// LVGL
// ---------------------------------------------------------------------------
static lv_disp_draw_buf_t draw_buf;
static lv_color_t disp_buf[LCD_H_RES * 20];
static lv_disp_drv_t disp_drv;   // 必须静态/全局，lv_disp_drv_register 仅保存指针

// DMA 完成回调：SPI 传输结束后才通知 LVGL，避免 LVGL 复用缓冲区时与 DMA 竞争导致撕裂/条纹
static bool lcdTransDone(esp_lcd_panel_io_handle_t panel_io,
                         esp_lcd_panel_io_event_data_t* edata, void* user_ctx)
{
  lv_disp_drv_t* drv = (lv_disp_drv_t*)user_ctx;
  lv_disp_flush_ready(drv);
  return false;
}

static void lvglFlush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p)
{
  (void)drv;
  esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1,
                            area->x2 + 1, area->y2 + 1, color_p);
  // 注意：不要在这里调用 lv_disp_flush_ready，等 lcdTransDone 回调
}

static void lvglInit()
{
  lv_init();

  lv_disp_draw_buf_init(&draw_buf, disp_buf, NULL, LCD_H_RES * 20);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = LCD_H_RES;
  disp_drv.ver_res = LCD_V_RES;
  disp_drv.flush_cb = lvglFlush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  // 注册 SPI 传输完成回调，异步通知 LVGL 刷新完成
  esp_lcd_panel_io_callbacks_t cbs = {};
  cbs.on_color_trans_done = lcdTransDone;
  esp_lcd_panel_io_register_event_callbacks(lcd_io_handle, &cbs, &disp_drv);

  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, LV_PART_MAIN);
}

// ---------------------------------------------------------------------------
// 地平仪（类似飞机姿态显示）
// ---------------------------------------------------------------------------
#define HORIZON_W 320
#define HORIZON_H 216   // 底部留 24px 给文字行

static lv_color_t* canvasBuf = NULL;
static lv_obj_t*  horizonCanvas = NULL;

// 依据俯仰角/横滚角把地平线画到画布上
static void renderHorizon(float pitchDeg, float rollDeg)
{
  if (!canvasBuf || !horizonCanvas) return;

  const int W = HORIZON_W;
  const int H = HORIZON_H;
  const float cx = W * 0.5f;
  const float cy = H * 0.5f;
  const float scale = 2.2f;                        // 每度俯仰对应的像素
  const float rad = -rollDeg * 0.0174532925f;
  const float cosr = cosf(rad);
  const float sinr = sinf(rad);
  const float off = pitchDeg * scale;              // 地平线在旋转坐标系中的偏移

  lv_color_t sky    = lv_color_make(0x1E, 0x90, 0xFF);
  lv_color_t ground = lv_color_make(0x8B, 0x5A, 0x2B);
  lv_color_t white  = lv_color_make(0xFF, 0xFF, 0xFF);
  lv_color_t yellow = lv_color_make(0xFF, 0xE0, 0x00);

  const float ladderSpacing = 15.0f * scale;

  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      float dx = x - cx;
      float dy = y - cy;
      float rx = dx * cosr - dy * sinr;
      float ry = dx * sinr + dy * cosr;

      lv_color_t c = (ry > off) ? ground : sky;

      // 地平线（粗白线）
      if (fabsf(ry - off) < 2.0f) c = white;

      // 俯仰刻度（每 15°，中间区域）
      float rel = ry - off;
      float m = fmodf(rel, ladderSpacing);
      if (m < 0.0f) m += ladderSpacing;
      if (m < 2.0f && fabsf(rx) < 40.0f) c = white;

      // 固定居中的飞机符号（黄色）
      if (fabsf(dx) <= 1.0f && dy >= -18.0f && dy <= 12.0f) c = yellow;   // 机身
      if (fabsf(dy) <= 1.0f && dx >= -26.0f && dx <= 26.0f) c = yellow;   // 机翼

      canvasBuf[y * W + x] = c;
    }
  }

  lv_obj_invalidate(horizonCanvas);
}

// ---------------------------------------------------------------------------
// UI 对象
// ---------------------------------------------------------------------------
static lv_obj_t* slopeVal = NULL;
static lv_obj_t* statusLabels[4] = {NULL, NULL, NULL, NULL};

static const char* SOUND_NAMES[4] = {"ENG", "POL", "AMB", "FIR"};

static void setupUi()
{
  // 地平仪画布：铺满屏幕上方，底部留一行文字
  if (canvasBuf) {
    horizonCanvas = lv_canvas_create(lv_scr_act());
    lv_canvas_set_buffer(horizonCanvas, canvasBuf, HORIZON_W, HORIZON_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(horizonCanvas, LV_ALIGN_TOP_MID, 0, 0);
  }

  // 底部一行文字（缩略），使用 flex 横向排列
  lv_obj_t* row = lv_obj_create(lv_scr_act());
  lv_obj_set_size(row, LCD_H_RES, 22);
  lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_row(row, 0, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // 坡度
  slopeVal = lv_label_create(row);
  lv_label_set_text(slopeVal, "SLP 0.0%");
  lv_obj_set_style_text_color(slopeVal, lv_color_make(0xFF, 0xC0, 0x00), LV_PART_MAIN);

  // 声音状态
  for (int i = 0; i < 4; i++) {
    statusLabels[i] = lv_label_create(row);
    lv_label_set_text(statusLabels[i], SOUND_NAMES[i]);
    lv_obj_set_style_text_color(statusLabels[i], lv_color_make(0x60, 0x60, 0x60), LV_PART_MAIN);
  }
}

// ---------------------------------------------------------------------------
// I2S 音频输出 + 合成
// ---------------------------------------------------------------------------
static i2s_chan_handle_t tx_handle = NULL;

static void i2sInit()
{
  esp_err_t ret;

  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.auto_clear = true;
  ret = i2s_new_channel(&chan_cfg, &tx_handle, NULL);
  Serial.printf("[I2S] new_channel ret=0x%X\n", (int)ret);

  i2s_std_config_t std_cfg = {};
  std_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE);
  std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  std_cfg.gpio_cfg.mclk = (gpio_num_t)I2S_MCK_IO;
  std_cfg.gpio_cfg.bclk = (gpio_num_t)I2S_BCK_IO;
  std_cfg.gpio_cfg.ws = (gpio_num_t)I2S_WS_IO;
  std_cfg.gpio_cfg.dout = (gpio_num_t)I2S_DO_IO;
  std_cfg.gpio_cfg.din = GPIO_NUM_NC;
  std_cfg.gpio_cfg.invert_flags.mclk_inv = false;
  std_cfg.gpio_cfg.invert_flags.bclk_inv = false;
  std_cfg.gpio_cfg.invert_flags.ws_inv = false;
  std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;
  ret = i2s_channel_init_std_mode(tx_handle, &std_cfg);
  Serial.printf("[I2S] init_std ret=0x%X\n", (int)ret);

  ret = i2s_channel_enable(tx_handle);
  Serial.printf("[I2S] enable ret=0x%X\n", (int)ret);
}

// 声音状态（loop 写入，音频任务读取）
volatile int   g_policeOn      = 0;   // 警笛
volatile int   g_ambulanceOn   = 0;   // 救护
volatile int   g_fireOn        = 0;   // 消防
volatile int   g_driveState    = 0;   // 0=怠速 1=前进 2=倒车
volatile float g_throttle01    = 0.0f;

static const float TWOPI = 6.28318530718f;
static float engPhase = 0.0f;
static float sirenPhase1 = 0.0f, sirenPhase2 = 0.0f, sirenPhase3 = 0.0f;
static float sirenTime1 = 0.0f, sirenTime2 = 0.0f, sirenTime3 = 0.0f;
static float beepPhase = 0.0f, beepTimer = 0.0f;

static inline float wrap01(float v)
{
  while (v >= 1.0f) v -= 1.0f;
  while (v < 0.0f)  v += 1.0f;
  return v;
}

static float sirenSample(int type, float& phase, float& time)
{
  time += 1.0f / AUDIO_SAMPLE_RATE;
  if (time > 20.0f) time -= 20.0f;

  float fs = 600.0f;
  float amp = 0.35f;

  if (type == 1) {                             // 警笛：600~1200Hz 扫描
    float sweep = 0.5f + 0.5f * sinf(TWOPI * time / 1.6f);
    fs = 600.0f + sweep * 600.0f;
  } else if (type == 2) {                      // 救护车：700/950Hz 交替
    float t = fmodf(time, 0.9f);
    fs = (t < 0.45f) ? 700.0f : 950.0f;
  } else {                                     // 消防车：400/500Hz 低频交替
    float t = fmodf(time, 1.0f);
    fs = (t < 0.5f) ? 400.0f : 500.0f;
    amp = 0.45f;
  }

  phase += fs / AUDIO_SAMPLE_RATE;
  phase = wrap01(phase);
  return sinf(TWOPI * phase) * amp;
}

// 生成 n 个立体声采样（每个采样 = L + R，共 2n 个 int16）
static void synthFill(int16_t* buf, int n)
{
  int drive = g_driveState;
  float thr = g_throttle01;
  int pol = g_policeOn, amb = g_ambulanceOn, fire = g_fireOn;

  for (int i = 0; i < n; i++) {
    float s = 0.0f;

    // 引擎声跟随油门：前进引擎声、倒车滴滴声、怠速静音（上电默认静音）
    if (drive == 1) {                          // 前进：发动机（频率随油门升高）
      float f = 55.0f + thr * 145.0f;
      engPhase += f / AUDIO_SAMPLE_RATE;
      engPhase = wrap01(engPhase);
      s += (engPhase * 2.0f - 1.0f) * (0.30f + thr * 0.50f);
    } else if (drive == 2) {                   // 倒车：滴滴声
      beepTimer += 1.0f / AUDIO_SAMPLE_RATE;
      if (beepTimer >= 0.34f) beepTimer -= 0.34f;
      if (beepTimer < 0.17f) {
        beepPhase += 1000.0f / AUDIO_SAMPLE_RATE;
        beepPhase = wrap01(beepPhase);
        s += (beepPhase < 0.5f) ? 0.50f : -0.50f;
      }
    }

    // 警报声（各通道独立）
    if (pol)  s += sirenSample(1, sirenPhase1, sirenTime1);
    if (amb)  s += sirenSample(2, sirenPhase2, sirenTime2);
    if (fire) s += sirenSample(3, sirenPhase3, sirenTime3);

    if (s > 1.0f) s = 1.0f;
    if (s < -1.0f) s = -1.0f;
    int16_t v = (int16_t)(s * 32000.0f);
    buf[i * 2] = v;      // L
    buf[i * 2 + 1] = v;  // R
  }
}

static void audioTask(void* arg)
{
  const int chunk = 320;  // 20ms
  static int16_t buf[chunk * 2];
  while (1) {
    synthFill(buf, chunk);
    size_t written = 0;
    esp_err_t ret = i2s_channel_write(tx_handle, buf, sizeof(buf), &written, portMAX_DELAY);
    if (ret != ESP_OK) {
      vTaskDelay(1);
    }
  }
}

// ---------------------------------------------------------------------------
// 接收机 PWM 读取（每个声音一个通道）
// ---------------------------------------------------------------------------
#define NUM_RX_CH 4

static const uint8_t RX_PIN[NUM_RX_CH] = {
  THROTTLE_PIN, POLICE_PIN, AMBULANCE_PIN, FIRE_PIN
};

volatile uint32_t rxPulseUs[NUM_RX_CH] = {PWM_NEUTRAL, PWM_NEUTRAL, PWM_NEUTRAL, PWM_NEUTRAL};
static volatile uint32_t rxRise[NUM_RX_CH] = {0, 0, 0, 0};

static void IRAM_ATTR rxEdge(uint8_t idx)
{
  uint32_t now = micros();
  if (gpio_get_level((gpio_num_t)RX_PIN[idx])) {
    rxRise[idx] = now;
  } else {
    uint32_t w = now - rxRise[idx];
    if (w > 500 && w < 5000) rxPulseUs[idx] = w;
  }
}

static void IRAM_ATTR isr0() { rxEdge(0); }
static void IRAM_ATTR isr1() { rxEdge(1); }
static void IRAM_ATTR isr2() { rxEdge(2); }
static void IRAM_ATTR isr3() { rxEdge(3); }

// ---------------------------------------------------------------------------
// 主逻辑
// ---------------------------------------------------------------------------
static Qmi8658Data attitude = {0, 0, 0};
static bool imuOk = false;

static void readReceiver()
{
  uint32_t w = rxPulseUs[0];                       // 油门
  if (w > PWM_NEUTRAL + PWM_DEADBAND) {
    g_driveState = 1;
    float t = (float)(w - PWM_NEUTRAL) / (float)(PWM_MAX - PWM_NEUTRAL);
    g_throttle01 = (t > 1.0f) ? 1.0f : t;
  } else if (w < PWM_NEUTRAL - PWM_DEADBAND) {
    g_driveState = 2;
    float t = (float)(PWM_NEUTRAL - w) / (float)(PWM_NEUTRAL - PWM_MIN);
    g_throttle01 = (t > 1.0f) ? 1.0f : t;
  } else {
    g_driveState = 0;
    g_throttle01 = 0.0f;
  }

  g_policeOn      = rxPulseUs[1] > SW_ON_US;
  g_ambulanceOn   = rxPulseUs[2] > SW_ON_US;
  g_fireOn        = rxPulseUs[3] > SW_ON_US;
}

static void updateUi()
{
  char buf[32];

  if (imuOk) {
    renderHorizon(attitude.angleX, attitude.angleY);

    float slopePct = tanf(attitude.angleZ * 0.0174532925f) * 100.0f;
    if (slopePct < 0.0f) slopePct = 0.0f;
    snprintf(buf, sizeof(buf), "SLP %.1f%%", slopePct);
    lv_label_set_text(slopeVal, buf);
  } else {
    lv_label_set_text(slopeVal, "SLP N/A");
  }

  int states[4] = {(g_driveState != 0), g_policeOn, g_ambulanceOn, g_fireOn};
  for (int i = 0; i < 4; i++) {
    snprintf(buf, sizeof(buf), "%s:%s", SOUND_NAMES[i], states[i] ? "ON" : "OFF");
    lv_label_set_text(statusLabels[i], buf);
    lv_color_t c = states[i] ? lv_color_make(0x00, 0xFF, 0x00)
                             : lv_color_make(0x60, 0x60, 0x60);
    lv_obj_set_style_text_color(statusLabels[i], c, LV_PART_MAIN);
  }
}

static void lvglTask(void* arg);

void setup()
{
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("[RC Audio] 实战派 ESP32-S3");

  // I2C 总线
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  gpio_set_pull_mode((gpio_num_t)I2C_SDA, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode((gpio_num_t)I2C_SCL, GPIO_PULLUP_ONLY);
  delay(10);

  // LCD
  pca9557Init();
  backlightOn();
  bspLcdInit();

  // 姿态传感器
  imuOk = Qmi8658::init();
  Serial.printf("[IMU] QMI8658 %s\n", imuOk ? "OK" : "NOT FOUND");

  // 音频
  i2sInit();
  if (Es8311::init()) {
    Serial.println("[AUDIO] ES8311 OK");
  } else {
    Serial.println("[AUDIO] ES8311 init failed");
  }
  pa_en(1);  // 打开功放（默认输出静音，由各通道控制发声）

  // LVGL UI
  lvglInit();
  // 地平仪画布缓冲：铺满屏（320x216）。用内部 RAM 更可靠（避免 PSRAM 读取问题）
  canvasBuf = (lv_color_t*)heap_caps_malloc(HORIZON_W * HORIZON_H * sizeof(lv_color_t),
                                            MALLOC_CAP_INTERNAL);
  Serial.printf("[UI] canvas buf %s (%d bytes) in_psram=%d\n",
                canvasBuf ? "OK" : "ALLOC FAIL",
                (int)(HORIZON_W * HORIZON_H * sizeof(lv_color_t)),
                canvasBuf ? (int)esp_ptr_external_ram(canvasBuf) : -1);
  setupUi();
  renderHorizon(0.0f, 0.0f);
  if (canvasBuf) {
    Serial.printf("[UI] px top=%04X mid=%04X bot=%04X\n",
                  canvasBuf[0].full,
                  canvasBuf[108 * HORIZON_W + 160].full,
                  canvasBuf[215 * HORIZON_W + 160].full);
  }

  // 接收机
  for (int i = 0; i < NUM_RX_CH; i++) {
    pinMode(RX_PIN[i], INPUT_PULLDOWN);
  }
  attachInterrupt(RX_PIN[0], isr0, CHANGE);
  attachInterrupt(RX_PIN[1], isr1, CHANGE);
  attachInterrupt(RX_PIN[2], isr2, CHANGE);
  attachInterrupt(RX_PIN[3], isr3, CHANGE);

  // 音频任务（核心 0）
  xTaskCreatePinnedToCore(audioTask, "audio", 8192, NULL, 5, NULL, 0);

  // LVGL 任务（核心 1，较大栈）
  xTaskCreatePinnedToCore(lvglTask, "lvgl", 32768, NULL, 4, NULL, 1);

  Serial.println("[RC Audio] ready");
}

// LVGL 专用任务：所有 LVGL 调用（tick/handler/对象更新）都在此任务内，
// 使用较大栈，避免 Arduino loop 任务 8KB 栈溢出导致崩溃。
static void lvglTask(void* arg)
{
  uint32_t lastTick = millis();
  uint32_t lastRead = millis();
  uint32_t lastUi = millis();

  while (1) {
    uint32_t now = millis();

    lv_tick_inc(now - lastTick);
    lastTick = now;
    lv_timer_handler();

    if (now - lastRead >= 20) {   // 50Hz 读取接收机
      lastRead = now;
      readReceiver();
    }

    if (now - lastUi >= 100) {    // 10Hz 刷新姿态/界面
      lastUi = now;
      if (imuOk) {
        Qmi8658::fetchAngles(attitude);
      }
      updateUi();
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void loop()
{
  vTaskDelay(pdMS_TO_TICKS(20));
}
