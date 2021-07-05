#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/binary_info.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include <stdio.h>
#include <string.h>
#define BUTTON_PIN 16
#define POMODORO_LENGTH (25 * 60)

const int LCD_CLEARDISPLAY = 0x01;
const int LCD_RETURNHOME = 0x02;
const int LCD_ENTRYMODESET = 0x04;
const int LCD_DISPLAYCONTROL = 0x08;
const int LCD_CURSORSHIFT = 0x10;
const int LCD_FUNCTIONSET = 0x20;
const int LCD_SETCGRAMADDR = 0x40;
const int LCD_SETDDRAMADDR = 0x80;

// flags for display entry mode
const int LCD_ENTRYSHIFTINCREMENT = 0x01;
const int LCD_ENTRYLEFT = 0x02;

// flags for display and cursor control
const int LCD_BLINKON = 0x01;
const int LCD_CURSORON = 0x02;
const int LCD_DISPLAYON = 0x04;

// flags for display and cursor shift
const int LCD_MOVERIGHT = 0x04;
const int LCD_DISPLAYMOVE = 0x08;

// flags for function set
const int LCD_5x10DOTS = 0x04;
const int LCD_2LINE = 0x08;
const int LCD_8BITMODE = 0x10;

// flag for backlight control
const int LCD_BACKLIGHT = 0x08;

const int LCD_ENABLE_BIT = 0x04;

// By default these LCD display drivers are on bus address 0x27
static int addr = 0x27;

absolute_time_t starttime = 0;
int request_clear = 0;
int run = 1;

// Modes for lcd_send_byte
#define LCD_CHARACTER 1
#define LCD_COMMAND 0

#define MAX_LINES 2
#define MAX_CHARS 16

/* Quick helper function for single byte transfers */
void i2c_write_byte(uint8_t val) {
#ifdef i2c_default
  i2c_write_blocking(i2c_default, addr, &val, 1, false);
#endif
}

void lcd_toggle_enable(uint8_t val) {
  // Toggle enable pin on LCD display
  // We cannot do this too quickly or things don't work
#define DELAY_US 600
  sleep_us(DELAY_US);
  i2c_write_byte(val | LCD_ENABLE_BIT);
  sleep_us(DELAY_US);
  i2c_write_byte(val & ~LCD_ENABLE_BIT);
  sleep_us(DELAY_US);
}

// The display is sent a byte as two separate nibble transfers
void lcd_send_byte(uint8_t val, int mode) {
  uint8_t high = mode | (val & 0xF0) | LCD_BACKLIGHT;
  uint8_t low = mode | ((val << 4) & 0xF0) | LCD_BACKLIGHT;

  i2c_write_byte(high);
  lcd_toggle_enable(high);
  i2c_write_byte(low);
  lcd_toggle_enable(low);
}

void lcd_clear(void) { lcd_send_byte(LCD_CLEARDISPLAY, LCD_COMMAND); }

// go to location on LCD
void lcd_set_cursor(int line, int position) {
  int val = (line == 0) ? 0x80 + position : 0xC0 + position;
  lcd_send_byte(val, LCD_COMMAND);
}

static void inline lcd_char(char val) { lcd_send_byte(val, LCD_CHARACTER); }

void lcd_string(const char *s) {
  while (*s) {
    lcd_char(*s++);
  }
}

void lcd_init() {
  lcd_send_byte(0x03, LCD_COMMAND);
  lcd_send_byte(0x03, LCD_COMMAND);
  lcd_send_byte(0x03, LCD_COMMAND);
  lcd_send_byte(0x02, LCD_COMMAND);

  lcd_send_byte(LCD_ENTRYMODESET | LCD_ENTRYLEFT, LCD_COMMAND);
  lcd_send_byte(LCD_FUNCTIONSET | LCD_2LINE, LCD_COMMAND);
  lcd_send_byte(LCD_DISPLAYCONTROL | LCD_DISPLAYON, LCD_COMMAND);
  lcd_clear();
}

void gpio_callback(uint gpio, uint32_t events) {
  starttime = get_absolute_time();
  request_clear = 1;
  run = 1;
}

int main() {
#if !defined(i2c_default) || !defined(PICO_DEFAULT_I2C_SDA_PIN) ||             \
    !defined(PICO_DEFAULT_I2C_SCL_PIN)
#warning i2c/lcd_1602_i2c example requires a board with I2C pins
#else
  // This example will use I2C0 on the default SDA and SCL pins (4, 5 on a Pico)
  absolute_time_t start_time = get_absolute_time();
  i2c_init(i2c_default, 100 * 1000);
  gpio_set_function(PICO_DEFAULT_I2C_SDA_PIN, GPIO_FUNC_I2C);
  gpio_set_function(PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C);
  gpio_pull_up(PICO_DEFAULT_I2C_SDA_PIN);
  gpio_pull_up(PICO_DEFAULT_I2C_SCL_PIN);
  gpio_init(BUTTON_PIN);
  gpio_set_dir(BUTTON_PIN, GPIO_IN);
  gpio_pull_up(BUTTON_PIN);
  gpio_set_irq_enabled_with_callback(BUTTON_PIN, 0x04, true, &gpio_callback);

  lcd_init();
  char lastTimeString[16];
  char lastPomodoroString[16];
  char curTimeString[16] = "";
  char pomodoroString[16] = "";
  int pomodoros = 0;

  while (1) {
    int curTimeSecs;
    absolute_time_t breakTime;
    absolute_time_t curTime = get_absolute_time();

    if (run) {
      if ((curTime - starttime) / 1000000.0 > POMODORO_LENGTH) {
        breakTime = curTime;
        pomodoros++;
        run = 0;
      }
    }
    if (request_clear) {
      lcd_clear();
      request_clear = 0;
      for (int i = 0; i < 16; i++) {
        lastTimeString[i] = '\0';
        lastPomodoroString[i] = '\0';
      }
    }
    if (run) {
      curTimeSecs = (curTime - starttime) / 1000000;

    } else {
      curTimeSecs = (curTime - breakTime) / 1000000;
    }
    sprintf(curTimeString, "%02d:%02d %s", curTimeSecs / 60, curTimeSecs % 60,
            run ? "Work" : "Break");
    for (int i = 0; i < pomodoros; i++) {
      pomodoroString[i] = 0xA5;
    }
    for (int i = 0; curTimeString[i] != '\0'; i++) {
      lcd_set_cursor(0, i);
      if (curTimeString[i] != lastTimeString[i]) {
        lcd_string(&curTimeString[i]);
      }
    }
    for (int i = 0; pomodoroString[i] != '\0'; i++) {
      lcd_set_cursor(1, i);
      if (pomodoroString[i] != lastPomodoroString[i]) {
        lcd_string(pomodoroString + i);
      }
    }
    strncpy(lastTimeString, curTimeString, 16);
    strncpy(lastPomodoroString, pomodoroString, 16);
    sleep_ms(200);
  }

  return 0;
#endif
}
