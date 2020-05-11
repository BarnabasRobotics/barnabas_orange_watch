// this sketch makes a pretty digital watch with an M5Stack M5StickC ESP32 module
// I started with the example code
// https://github.com/m5stack/M5StickC/blob/master/examples/Basics/RTC/RTC.ino
// but prettied up the display, made it go to sleep so you can get more than the
// 40 or so minutes it can run otherwise, and implemented features such as an accurate
// battery state of charge.
//
// use with the $15.60 watch kit from
// https://m5stack.com/collections/m5-core/products/m5stickc-development-kit-with-hat
//

//
// features:
//
// * code to set the Real Time Clock (RTC) chip once
//  * multiple screens of data, use the big M5 button to cycle between them, the watch
//    saves your preference for the next time it wakes up
// * digital display with colorful 7-segment hours and minutes, and somewhat animated
//  seconds display (text 0-59 moves across the bottom of the display.)
// * analog and display with 80 pixel diameter analog clock, plus battery meter, plus
//    digital clock.


//
// potential future improvements:
//
// * add battery level icon to digital screen
// * would lowering the CPU clock save much power?
//  see https://github.com/tracestick/firmware_alpha/blob/master/mods/boards.diff.txt
// * beep on hour?
// * speak time? (these two would need the Speaker module plugged in)
// * magnometer in ENV sensor -> analog/digital compass
// * use RTC to sleep/wake CPU while OLED remains active?
// * stay awake when plugged into USB or power
// * fade out display at power off
// * set time via NTP over WiFi?
// * temperature gauge (will be hot when charging!
// * timer
// * stopwatch
// * use gyroscope/accelerometer as input
// * wake up from tap like fitbit?
// * set time from device's UI
// * maybe thicker hands by painting them again with X and Y + 1
// * maybe prettier hands by drawing triangles
// * would using tftSprite like Basics/AXP192/AXP192.ino make the display better?
// * WiFi + MQTT
// * make 12/24 hour user configurable, and verify that 24h works correctly


// questions:
//
// * in 24h mode, should analog hour hand go around once per 12h or 24h?
//

// references:
//
// https://en.wikipedia.org/wiki/Polar_coordinate_system
// https://setosa.io/ev/sine-and-cosine/
// https://github.com/m5stack/M5StickC/blob/master/examples/Advanced/AXP192/sleep/sleep.ino
// https://github.com/m5stack/M5StickC/blob/master/examples/Basics/RTC/RTC.ino
// https://www.instructables.com/id/M5-Smart-Watch-Using-DEEP-SLEEP-Function-to-Increa/
//
// The M5Stack M5StickC device uses a Belling Shanghai BM8563 RTC
// https://www.belling.com.cn/media/file_object/bel_product/BM8563/datasheet/BM8563_V1.1_cn.pdf
// which seems to be a clone of the NXP PCF8563 RTC
// https://www.nxp.com/docs/en/data-sheet/PCF8563.pdf
//
// https://docs.m5stack.com/#/en/core/m5stickc
//
// an English language overview of the AXP192 Power Management IC
// https://github.com/ContextQuickie/TTGO-T-Beam/wiki/AXP192-Information
// with gems such as:
//  "The AXP192 contains an internal timer. The timer value can be changed
// by setting the register REG8AH Bits 6-0. Its minimum resolution is minutes.
// REG8AH Bit 7 is set when the timer expires."
// interesting.  so for sleep/wake, we can look at both the RTC and the PMIC.
//
// https://github.com/m5stack/m5-docs/blob/master/docs/en/api/axp192_m5stickc.md

// Notes
// the AXP192 library says there are six bytes of "Data Buffer", 0x06-0x0B
// but the data sheet in section 9.11.8 says four registers: 0x060x09
// I am using five of the bytes.

// http://www.x-powers.com/en.php/Info/product_detail/article_id/29
// https://github.com/m5stack/M5-Schematic/raw/master/Core/AXP192%20Datasheet_v1.1_en_draft_2211.pdf

// Hardware:
//
// ESP32-PICO ARM MCU with Bluetooth and WiFi
// ST7735S
// BM8563   Real Time Clock (RTC)
// MPU6886   Motion Sensor (IMU/accellerometer)
// SH200Q
// AXP192   Power Management IC (PMIC)
// SPM1423
// 80 or 95 mAh LiIon cell "battery"
// USB C with programming/debugging serial port

const int SLEEP_TIME = 15; // watch goes to sleep after this many seconds
                          // or zero to stay awake

const bool twelve_hour_time = 1; // set to 1 for "normal" 1-12 hours time
                // set to 0 for 0-23 hundred hours "military" time
const uint8_t CENTER_X = 42;
const uint8_t CENTER_Y = 40;
const uint8_t CLOCK_RADIUS = 39;
const uint8_t HOUR_HAND_LENGTH = 20;
const uint8_t MINUTE_HAND_LENGTH = 30;
const uint8_t SECOND_HAND_LENGTH = 38;

unsigned long previousMillis = 0;
const long interval = 500; 

// pointer-fu from https://stackoverflow.com/a/13353227
// I load a six byte buffer from the AXP192, and use four bytes for a 
// float for the battery capacity when fully charged in mAh, and for a byte
// for the screen the user last selected. There is a byte avaliable for
// future use as well: pointer_to_uint8_in_buffer[5]
//
uint8_t axp_data_buffer[6] = {0};          // array to hold data buffer
uint8_t *pointer_to_axp_data_buffer = axp_data_buffer;
float *pointer_to_float_in_buffer = (float*)axp_data_buffer;
uint8_t *pointer_to_uint8_in_buffer = (uint8_t*)axp_data_buffer;
 
#include <M5StickC.h>

RTC_TimeTypeDef RTC_TimeStruct;

uint8_t screen = 0; // will be set later from data loaded from AXP data buffer
const uint8_t max_screen = 2;
bool LCD = true;
float last_x;
float last_y;
float Radians;
float hour_radians;
float last_hour_radians;
float minute_radians;
float last_minute_radians;
float second_radians;
float last_second_radians;
float rotation;
float battery_full_capacity = 0; // in mAh. stored in AXP PMIC "data buffer"
float x;
float y;

bool blink = 0;
bool pm = 0;
float hours = 0;
uint8_t minutes = 0;
uint8_t seconds = 0;
unsigned long wake_time_millis = 0;

void setup() {
 Serial.begin(115200);
 
 //Wire.begin(0,26); //todo is this the default, and thus can be removed?

 M5.begin();
 M5.Axp.EnableCoulombcounter();

 M5.Lcd.setRotation(1); // rotation 3 is typical. USB-C and M5 button on left
             // rotation 1 if you mount it with the M5 button on the right
             // which might be more conveneient when worn on the left wrist
             
 M5.Lcd.fillScreen(BLACK);
  
 RTC_TimeTypeDef TimeStruct;

 // Use this once to set the date and time in the RTC chip, by changing
 // it to if(1) and filling in the real date and time, and programming
 // the device.
 // Then disable it with if(0) so you don't clobber the time you just set.
 //
 if(0) 
 {
  TimeStruct.Hours  = 16; // 24 hour time. for 5 PM, set 17, etc
  TimeStruct.Minutes = 21;
  TimeStruct.Seconds = 00;
  M5.Rtc.SetTime(&TimeStruct);
  RTC_DateTypeDef DateStruct;
  DateStruct.WeekDay = 1;
  DateStruct.Month = 5;
  DateStruct.Date = 4;
  DateStruct.Year = 2020;
  M5.Rtc.SetData(&DateStruct);
 }

 //M5.MPU6886.Init(); // so we can get Temperature from it

 // fetch data buffer from AXP PMIC
 M5.Axp.Read6BytesStorage(pointer_to_axp_data_buffer);

 // set the capacity of the battery that we loaded from the AXP Data Buffer RAM
 // used for battery percentage
 battery_full_capacity = *pointer_to_float_in_buffer;

 // set the previously active screen number
 screen = pointer_to_uint8_in_buffer[4];

 wake_time_millis = millis();
}

void loop() {
 // update button state
 M5.update();
 buttons_code();

 unsigned long currentMillis = millis();

 // in support of an accurate battery state-of-charge
 // we have to do a few housekeeping things:
 
 // when battery is empty (the PMIC would cut us off at 3.0V.
  // at 3.1 V, it's basically all the way drained.
 //
 if (M5.Axp.GetBatVoltage() < 3.1)
 {
  M5.Axp.SetCoulombClear();
  M5.Axp.PowerOff();
 }
 
 // when battery is full
 //
 // fixme bug todo
 // when first connected, batCurrent is zero for a brief time
 // before charging starts 
 // a better way is probalby to read registers to find the charging 
 // status from the AXP chip
 //
 if (M5.Axp.GetBatCurrent() == 0)
 {
  battery_full_capacity = M5.Axp.GetCoulombData(); // in mAh

  *pointer_to_float_in_buffer = battery_full_capacity;
  
  // write this into AXP "data buffer" battery backed RAM
  M5.Axp.Write6BytesStorage(pointer_to_axp_data_buffer);

 }

 
 if (currentMillis - wake_time_millis >= SLEEP_TIME * 1000)
 {
  // save the active screen number to battery backed RAM in the AXP PMIC
  pointer_to_uint8_in_buffer[4] = screen;

  // write this into AXP "data buffer" battery backed RAM
  M5.Axp.Write6BytesStorage(pointer_to_axp_data_buffer);

  if (SLEEP_TIME > 0)
  { 
   M5.Axp.DeepSleep(0);
   // the M5 button on the face can wake the device
  }
 }

 if (currentMillis - previousMillis >= interval) 
 {
  blink = !blink; // concise way to simply flip from 0 -> 1, or 1 -> 0

  previousMillis = currentMillis;
  if(screen == 0)
  {
   analog_and_digital();
  }
  else if (screen == 1)
  {
   digital_clock();
  }
  else if (screen == 2)
  {
   coulomb();
  }
  else
  {
      // maybe we got bad data from the AXP192.  If the battery went completely flat,
      // register 0x0A would default to 
   screen = 0;
  }
 }
 

}

// from https://m5stack.hackster.io/herbert-stiebritz/very-simple-m5stickc-clock-08275b
//
void buttons_code() {
 // BtnA is the big M5 button on the front face
 // BtnB is the button on the bottom
 
 // change the screen
 if (M5.BtnA.wasPressed()) 
 {
  wake_time_millis = millis(); // reset the sleep timer
  
  M5.Lcd.fillScreen(BLACK);
  screen = screen + 1;
 }
 if (screen > max_screen)
 {
  screen = 0;
 }

 // this might be unnecessary, now that the device goes to deep sleep after SLEEP_TIME seconds
 //
 // control the LCD (ON/OFF)
 if (M5.BtnB.wasPressed()) 
 {
  if (LCD) {
   M5.Lcd.writecommand(ST7735_DISPOFF);
   M5.Axp.ScreenBreath(0);
   LCD = !LCD;
 } else {
   M5.Lcd.writecommand(ST7735_DISPON);
   M5.Axp.ScreenBreath(255);
   LCD = !LCD;
  }
 }


 // Button B long press
 if (M5.BtnB.pressedFor(2000)) {
  // M5 button long press
  //
  // do something else
 }
}

void digital_clock() {

 M5.Lcd.setCursor(0, 0);
 
 M5.Rtc.GetTime(&RTC_TimeStruct);

 // hours 
 M5.Lcd.setTextColor(RED, BLACK);
 M5.Lcd.setTextSize(1);
 M5.Lcd.setCursor(6, 2, 7);

 hours = RTC_TimeStruct.Hours;
 
 if (twelve_hour_time)
 {
  if (hours > 12)
  {
   pm = 1;
   hours = hours - 12;
   M5.Lcd.setCursor(6, 2, 7);
   M5.Lcd.setTextColor(BLACK, BLACK);
   M5.Lcd.printf("8");
  }
  else
  {
   pm = 0;
  }
 }
 
 M5.Lcd.setTextColor(RED, BLACK);

 M5.Lcd.printf("%d", uint8_t(hours));


 // blinking : character
 if(blink)
 {
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.printf(":");
 }
 else
 {
  M5.Lcd.setTextColor(BLACK, BLACK);
  M5.Lcd.printf(":");
 }


 // Minutes
 M5.Lcd.setTextColor(GREEN, BLACK);
 M5.Lcd.setTextSize(1);
 M5.Lcd.printf("%02d",RTC_TimeStruct.Minutes);


 // Seconds
 M5.Lcd.setTextColor(BLUE, BLACK);
 M5.Lcd.setTextSize(1);
 M5.Lcd.setCursor(RTC_TimeStruct.Seconds*2, 55, 4);
 M5.Lcd.printf(" %d",RTC_TimeStruct.Seconds);

 // this block erases the 59 seconds when we roll to 0 seconds
 if(RTC_TimeStruct.Seconds == 0)
 {
  M5.Lcd.setTextColor(BLACK, BLACK);
  M5.Lcd.setCursor(59*2, 55, 4);
  M5.Lcd.printf(" 59");
 }

 
}

void analog_and_digital()
{
 M5.Rtc.GetTime(&RTC_TimeStruct);

 // second hand
 seconds = RTC_TimeStruct.Seconds; 
 last_second_radians = second_radians;
 second_radians = ((seconds / 60.0) * 2 * PI);

 if (second_radians != last_second_radians)
 {
  // erase the previous second hand
  draw_clock_hand(BLACK, SECOND_HAND_LENGTH, CENTER_X, CENTER_Y, last_second_radians);
 }

 draw_clock_hand(BLUE, SECOND_HAND_LENGTH, CENTER_X, CENTER_Y, second_radians);





 //minutes = RTC_TimeStruct.Minutes;
 minutes = RTC_TimeStruct.Minutes + (RTC_TimeStruct.Seconds / 60.0);
 last_minute_radians = minute_radians;
 minute_radians = ((minutes / 60.0) * 2 * PI);

 if (minute_radians != last_minute_radians)
 {
  // erase the previous minute hand
  draw_clock_hand(BLACK, MINUTE_HAND_LENGTH, CENTER_X, CENTER_Y, last_minute_radians);
 }

 draw_clock_hand(GREEN, MINUTE_HAND_LENGTH, CENTER_X, CENTER_Y, minute_radians);


 //
 // hours
 //
 
 // The hour hand can't just be a whole number (integer). If it is, at
 // 1:30 the hour hand would still point straight at 1 o'clock, not
 // halfway between 1 o'clock and 2 o'clock as it would on a real analog clock.


 
 hours = RTC_TimeStruct.Hours + (RTC_TimeStruct.Minutes / 60.0);


 //
 // display digital hours and minutes
 if (hours > 13 && twelve_hour_time)
 {
  hours = hours - 12.0;
 }
 M5.Lcd.setCursor(100, 55, 4);
 
 M5.Lcd.setTextColor(RED, BLACK);
 M5.Lcd.printf("%d", (int) hours);

 // draw blinking : colon character
 if(blink)
 {
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.printf(":");
 }
 else
 {
  M5.Lcd.setTextColor(BLACK, BLACK);
  M5.Lcd.printf(":");
 }

 // draw minutes
 M5.Lcd.setTextColor(GREEN, BLACK);
 M5.Lcd.printf("%02d", RTC_TimeStruct.Minutes);

 
 // todo: if we're in 24 hour mode, should the hour hand make one rotation
 // per 24 hours?!
 
 last_hour_radians = hour_radians;
 hour_radians = ((hours / 12.0) * 2 * PI);
 
 if (hour_radians != last_hour_radians)
 {
  // erase the previous hour hand
  draw_clock_hand(BLACK, HOUR_HAND_LENGTH, CENTER_X, CENTER_Y, last_hour_radians);
 }
 
 draw_clock_hand(RED, HOUR_HAND_LENGTH, CENTER_X, CENTER_Y, hour_radians);



 // circle clock outline
 M5.Lcd.drawCircle(CENTER_X, CENTER_Y, CLOCK_RADIUS, WHITE);

 
 // draw radial marks
 if (1)
 {
  for (uint8_t a = 0; a < 12; a = a + 1)
  {
   //draw_clock_mark(int color, float Radians, uint8_t diameter, uint8_t Length, uint8_t center_x, uint8_t center_y)
   draw_clock_mark(WHITE, a/12.0 * 2 * PI, CLOCK_RADIUS, 10, CENTER_X, CENTER_Y);
   
  }
 }
 // draw battery icon
 // fixme todo
 // should I add 0.5 to the next to account for floor rounding when I cast it to a uint? 
 uint8_t battery_percent = uint8_t((M5.Axp.GetCoulombData() / battery_full_capacity) * 100.0);
 //         x, y, width, height, b%
 draw_battery_icon(128, 5, 30, 10, battery_percent);
}
 
void coulomb()
{
 // this screen was for debugging getting the coulomb counter working.
 // the fix ended being pretty easy - I had to rest the coulomb counter 
 // in and out counts when the battery was basically depleted, and note the
 // mAh of energy that were put into the battery when it became fully charged,
 // and that value has to be stored in battery-backed RAM, which the AXP has.
 
 // for reference another watch project 
 // https://github.com/techiesms/M5Stick-C-Smart-Watch-/blob/master/M5StickC_Smart_Watch/M5StickC_Smart_Watch.ino
 // simply reads the battery voltage with vbat = M5.Axp.GetVbatData() * 1.1 / 1000;
 // and has thresholds of 
 // >= 4 V
 // >= 3.7 V
 // < 3.7 V

 // article that makes the point that spedometer is instanenous speed
 // and odometer is disatance traveled
 // https://learn.sparkfun.com/tutorials/ltc4150-coulomb-counter-hookup-guide/all
 
 // do I need to manually set or clear the coulomb counter?
 // https://github.com/ContextQuickie/TTGO-T-Beam/wiki/Register-B8H:-Coulomb-Control-Register
 // 

 // battery gauge, battery meter:
 //
 // dug out of previous version of examples/Basics/AXP192/AXP192.in from:
 // https://github.com/m5stack/M5StickC/pull/27/files#diff-947fdad95630acd4a397d9ea1e93e035
 //


 M5.Lcd.setCursor(0, 0, 2);
 M5.Lcd.setTextColor(WHITE, BLACK);
 
 M5.Lcd.printf(" C In %d, Out %d \r\n",M5.Axp.GetCoulombchargeData(), M5.Axp.GetCoulombdischargeData());
 M5.Lcd.printf(" Capacity %.2f mAh  \r\n",M5.Axp.GetCoulombData());

 M5.Lcd.printf(" Bat Chg %.0f%%   \r\n", ((M5.Axp.GetCoulombData() / battery_full_capacity)) * 100.0);
 M5.Lcd.printf(" full cap %.2f  \r\n", battery_full_capacity);


 M5.Lcd.printf(" Bat %.3f V, %.1f mA  ", M5.Axp.GetBatVoltage(), M5.Axp.GetBatCurrent());
 
 

}

void draw_clock_hand(int color, uint8_t Length, uint8_t center_x, uint8_t center_y, float Radians)
{
 x = Length * sin(Radians) * -1;
 y = Length * cos(Radians);

 M5.Lcd.drawLine(center_x - x, center_y - y, center_x, center_y, color);
}


void draw_clock_mark(int color, float Radians, uint8_t diameter, uint8_t Length, uint8_t center_x, uint8_t center_y)
{
 x = center_x + ((diameter - 10) * sin(Radians)) * -1;
 y = center_y + ((diameter - 10) * cos(Radians));

 float x2 = center_x + ((diameter - 2) * sin(Radians)) * -1;
 float y2 = center_y + ((diameter - 2) * cos(Radians)) ;


 M5.Lcd.drawLine(x, y, x2, y2, color);
}


void print_data_buffer(uint8_t *local_pointer_to_axp_data_buffer)
{
 for (uint8_t i = 0; i < 6; i++)
 {
  Serial.printf("%X\t", local_pointer_to_axp_data_buffer[i]); 
 }
 Serial.println();
}


void draw_battery_icon(uint8_t x, uint8_t y, uint8_t width, uint8_t height, uint8_t battery_percent)
{
 // outline
 M5.Lcd.drawRoundRect(x, y, width, height, 3, WHITE);

 // battery tip
 M5.Lcd.fillRect(x + width, y + 4, 1, height - 8, WHITE);

  // green battery percentage bar
  uint16_t battery_color = GREEN;
  if (battery_percent <= 20)
  {
    battery_color = RED;
  }
  M5.Lcd.fillRect(x + 2, y + 2, (width - 4) * (battery_percent/100.0), height - 4, battery_color);
}

/*
* I had the hardest time finding the commands for drawing to the OLED display
* on the M5StickC. The best reference seems to be
* https://github.com/m5stack/M5StickC/blob/master/src/M5StickC.h
*    
    M5.Lcd.drawPixel(int16_t x, int16_t y, uint16_t color);
    M5.Lcd.drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
    M5.Lcd.fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    M5.Lcd.fillScreen(uint16_t color);
    M5.Lcd.drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
    M5.Lcd.drawCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t cornername,uint16_t color);
    M5.Lcd.fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
    M5.Lcd.fillCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t cornername,int16_t delta, uint16_t color);
    M5.Lcd.drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
    M5.Lcd.fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
    M5.Lcd.drawRoundRect(int16_t x0, int16_t y0, int16_t w, int16_t h, int16_t radius, uint16_t color);
    M5.Lcd.fillRoundRect(int16_t x0, int16_t y0, int16_t w, int16_t h, int16_t radius, uint16_t color);
    M5.Lcd.drawBitmap(int16_t x, int16_t y, const uint8_t bitmap[], int16_t w, int16_t h, uint16_t color);
    M5.Lcd.drawRGBBitmap(int16_t x, int16_t y, const uint16_t bitmap[], int16_t w, int16_t h),
    M5.Lcd.drawChar(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg, uint8_t size);
    M5.Lcd.setCursor(uint16_t x0, uint16_t y0);
    M5.Lcd.setTextColor(uint16_t color);
    M5.Lcd.setTextColor(uint16_t color, uint16_t backgroundcolor);
    M5.Lcd.setTextSize(uint8_t size);
    M5.Lcd.setTextWrap(boolean w);
    M5.Lcd.printf();
    M5.Lcd.print();
    M5.Lcd.println();
    M5.Lcd.drawCentreString(const char *string, int dX, int poY, int font);
    M5.Lcd.drawRightString(const char *string, int dX, int poY, int font);
    //M5.Lcd.drawJpg(const uint8_t *jpg_data, size_t jpg_len, uint16_t x, uint16_t y);
    //M5.Lcd.drawJpgFile(fs::FS &fs, const char *path, uint16_t x, uint16_t y);
    //M5.Lcd.drawBmpFile(fs::FS &fs, const char *path, uint16_t x, uint16_t y);
*
*
* 
*/
