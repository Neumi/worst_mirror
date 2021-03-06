#include "SPI.h"

#include <FastLED.h>

#include <LEDMatrix.h>

#define LED_PIN        15
#define COLOR_ORDER    GRB
#define CHIPSET        WS2812B

#define MATRIX_WIDTH   16  // Set this negative if physical led 0 is opposite to where you want logical 0
#define MATRIX_HEIGHT  64  // Set this negative if physical led 0 is opposite to where you want logical 0
#define MATRIX_TYPE    HORIZONTAL_ZIGZAG_MATRIX  // See top of LEDMatrix.h for matrix wiring types

cLEDMatrix<MATRIX_WIDTH, MATRIX_HEIGHT, MATRIX_TYPE> leds;


uint8_t angle = 15;


#define LED_COUNT 1024
#define SUB_SEGMENT_X 16
#define SUB_SEGMENT_Y 16
#define SUB_SEGMENTS 4

#define PIN_SS        15
#define PIN_MISO      12
#define PIN_MOSI      13
#define PIN_SCK       14

#define PIN_MOUSECAM_RESET     5
#define PIN_MOUSECAM_CS        4
#define LED_PIN    15

#define ADNS3080_PIXELS_X                 30
#define ADNS3080_PIXELS_Y                 30
#define ADNS3080_PIXELS 900

#define ADNS3080_PRODUCT_ID            0x00
#define ADNS3080_REVISION_ID           0x01
#define ADNS3080_MOTION                0x02
#define ADNS3080_DELTA_X               0x03
#define ADNS3080_DELTA_Y               0x04
#define ADNS3080_SQUAL                 0x05
#define ADNS3080_PIXEL_SUM             0x06
#define ADNS3080_MAXIMUM_PIXEL         0x07
#define ADNS3080_CONFIGURATION_BITS    0x0a
#define ADNS3080_EXTENDED_CONFIG       0x0b
#define ADNS3080_DATA_OUT_LOWER        0x0c
#define ADNS3080_DATA_OUT_UPPER        0x0d
#define ADNS3080_SHUTTER_LOWER         0x0e
#define ADNS3080_SHUTTER_UPPER         0x0f
#define ADNS3080_FRAME_PERIOD_LOWER    0x10
#define ADNS3080_FRAME_PERIOD_UPPER    0x11
#define ADNS3080_MOTION_CLEAR          0x12
#define ADNS3080_FRAME_CAPTURE         0x13
#define ADNS3080_SROM_ENABLE           0x14
#define ADNS3080_FRAME_PERIOD_MAX_BOUND_LOWER      0x19
#define ADNS3080_FRAME_PERIOD_MAX_BOUND_UPPER      0x1a
#define ADNS3080_FRAME_PERIOD_MIN_BOUND_LOWER      0x1b
#define ADNS3080_FRAME_PERIOD_MIN_BOUND_UPPER      0x1c
#define ADNS3080_SHUTTER_MAX_BOUND_LOWER           0x1e
#define ADNS3080_SHUTTER_MAX_BOUND_UPPER           0x1e
#define ADNS3080_SROM_ID               0x1f
#define ADNS3080_OBSERVATION           0x3d
#define ADNS3080_INVERSE_PRODUCT_ID    0x3f
#define ADNS3080_PIXEL_BURST           0x40
#define ADNS3080_MOTION_BURST          0x50
#define ADNS3080_SROM_LOAD             0x60
#define ADNS3080_PRODUCT_ID_VAL        0x17



byte frame[ADNS3080_PIXELS_X * ADNS3080_PIXELS_Y];

void mousecam_reset()
{
  digitalWrite(PIN_MOUSECAM_RESET, HIGH);
  delay(1); // reset pulse >10us
  digitalWrite(PIN_MOUSECAM_RESET, LOW);
  delay(35); // 35ms from reset to functional
}

int mousecam_init()
{
  pinMode(PIN_MOUSECAM_RESET, OUTPUT);
  pinMode(PIN_MOUSECAM_CS, OUTPUT);

  digitalWrite(PIN_MOUSECAM_CS, HIGH);

  mousecam_reset();

  int pid = mousecam_read_reg(ADNS3080_PRODUCT_ID);
  if (pid != ADNS3080_PRODUCT_ID_VAL)
    return -1;

  // turn on sensitive mode
  mousecam_write_reg(ADNS3080_CONFIGURATION_BITS, 0x19);

  return 0;
}

void mousecam_write_reg(int reg, int val)
{
  digitalWrite(PIN_MOUSECAM_CS, LOW);
  SPI.transfer(reg | 0x80);
  SPI.transfer(val);
  digitalWrite(PIN_MOUSECAM_CS, HIGH);
  delayMicroseconds(50);
}

int mousecam_read_reg(int reg)
{
  digitalWrite(PIN_MOUSECAM_CS, LOW);
  SPI.transfer(reg);
  delayMicroseconds(75);
  int ret = SPI.transfer(0xff);
  digitalWrite(PIN_MOUSECAM_CS, HIGH);
  delayMicroseconds(1);
  return ret;
}

struct MD
{
  byte motion;
  char dx, dy;
  byte squal;
  word shutter;
  byte max_pix;
};

void mousecam_read_motion(struct MD *p)
{
  digitalWrite(PIN_MOUSECAM_CS, LOW);
  SPI.transfer(ADNS3080_MOTION_BURST);
  delayMicroseconds(75);
  p->motion =  SPI.transfer(0xff);
  p->dx =  SPI.transfer(0xff);
  p->dy =  SPI.transfer(0xff);
  p->squal =  SPI.transfer(0xff);
  p->shutter =  SPI.transfer(0xff) << 8;
  p->shutter |=  SPI.transfer(0xff);
  p->max_pix =  SPI.transfer(0xff);
  digitalWrite(PIN_MOUSECAM_CS, HIGH);
  delayMicroseconds(5);
}

// pdata must point to an array of size ADNS3080_PIXELS_X x ADNS3080_PIXELS_Y
// you must call mousecam_reset() after this if you want to go back to normal operation
int mousecam_frame_capture(byte *pdata)
{
  mousecam_write_reg(ADNS3080_FRAME_CAPTURE, 0x83);

  digitalWrite(PIN_MOUSECAM_CS, LOW);

  SPI.transfer(ADNS3080_PIXEL_BURST);
  delayMicroseconds(50);

  int pix;
  byte started = 0;
  int count;
  int timeout = 0;
  int ret = 0;
  for (count = 0; count < ADNS3080_PIXELS_X * ADNS3080_PIXELS_Y; )
  {
    pix = SPI.transfer(0xff);
    delayMicroseconds(10);
    if (started == 0)
    {
      if (pix & 0x40)
        started = 1;
      else
      {
        timeout++;
        if (timeout == 100)
        {
          ret = -1;
          break;
        }
      }
    }
    if (started == 1)
    {
      pdata[count++] = (pix & 0x3f) << 2; // scale to normal grayscale byte range
    }
  }

  digitalWrite(PIN_MOUSECAM_CS, HIGH);
  delayMicroseconds(14);

  return ret;
}

void setup()
{
  pinMode(PIN_SS, OUTPUT);
  pinMode(PIN_MISO, INPUT);
  pinMode(PIN_MOSI, OUTPUT);
  pinMode(PIN_SCK, OUTPUT);

  FastLED.addLeds<CHIPSET, LED_PIN, COLOR_ORDER>(leds[0], leds.Size());
  FastLED.setBrightness(32);
  FastLED.clear(true);
  FastLED.show();


  SPI.begin();
  SPI.setClockDivider(SPI_CLOCK_DIV32);
  SPI.setDataMode(SPI_MODE3);
  SPI.setBitOrder(MSBFIRST);

  Serial.begin(250000);

  if (mousecam_init() == -1)
  {
    Serial.println("Mouse cam failed to init");
    while (1);
  }
}


void loop()
{
  if (mousecam_frame_capture(frame) == 0)
  {
    for (int i = 0, j = ADNS3080_PIXELS_Y * ADNS3080_PIXELS_X - 1; i < ADNS3080_PIXELS_Y * ADNS3080_PIXELS_X / 2; i++, j--)
    {
      int temp = frame[i];
      frame[i] = frame[j];
      frame[j] = temp;
    }
    driveDisplay(frame);
  }
}


void driveDisplay(byte frame[]) {
  int i, j, k;
  for (i = 0, k = 0; i < ADNS3080_PIXELS_Y; i++)
  {
    for (j = 0; j < ADNS3080_PIXELS_X; j++, k++)
    {
      if (i >= 15) {
        leds(i - 15, j + 1) = CRGB(frame[k], 0, 0); // left matrix part
      }
      else {
        leds(i + 1 , j + 33) = CRGB(frame[k], 0, 0); // right matrix part
      }


      /*
         working. dont touch!
        if (i >= 15) {
        leds(i - 15, j + 1) = CRGB(frame[k], 0, 0); // left matrix part
        }
        else {
        leds(i + 1 , j + 33) = CRGB(frame[k], 0, 0); // right matrix part
        }
      */

    }
  }

  FastLED.show();

}
