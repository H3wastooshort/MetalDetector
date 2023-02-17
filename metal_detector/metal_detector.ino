//Compiled with ATtinyCore (ATtiny85, 16mHz PLL)

#include <LiquidCrystal_I2C.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <EEPROM.h>
#include "gfx.h"

#define SHORT_CLICK_TIME 50  //minimum time for a short click to register
#define LONG_CLICK_TIME 500

uint32_t pulse_array[5] = { 0 };  //ring buffer of latest pulsecounts
constexpr uint8_t pulse_array_length = sizeof(pulse_array) / sizeof(pulse_array[0]);
uint8_t pulse_array_next = 0;
uint32_t pulses_this_int = 0;  //how many pulses since the last timer interrupt
#define TIMER_FREQ 4           //how often the timer int fires, in Hertz
constexpr uint8_t TIMER_CMP =  //timer compare value. RESULT MUST BE SMALLER THAN 255, otherwise increase prescaler in setup()
  (F_CPU /*cpu freq*/ / (16348L /*prescaler*/ * (uint16_t)TIMER_FREQ /*timer freqency*/)) - 1;

LiquidCrystal_I2C lcd(0x27, 16, 2);

struct cal_data_s {
  float pulses_air = 0;
  float pulses_iron = 0;
} cal_data;


uint8_t beep_flag = 2;  // 0 or 1 mean beep, 2 means off, 3 means passthru
ISR(PCINT0_vect) {
  pulses_this_int++;
  //if (beep_flag == 3) PORTB &= (0xFF & (uint8_t(PINB && 0b00001000) << 4 /*out pin*/)); //set bit for beeper to bit of freq input
  if (beep_flag == 3) digitalWrite(4, digitalRead(3));
}

ISR(TIMER1_COMPA_vect) {
  pulse_array[pulse_array_next] = pulses_this_int;  //save pulse count
  pulses_this_int = 0;                              //reset pulsecount
  pulse_array_next++;                               //select next position
  pulse_array_next %= pulse_array_length;           //limit to array length. no buffer overflows here

  if (beep_flag < 2) {
    beep_flag &= 1;
    beep_flag ^= 1;  //toggle LSB by XOR with 1
    digitalWrite(4, beep_flag);
  }
}

float get_pulses() {
  uint32_t all_pulses = 0;
  for (uint8_t i = 0; i < pulse_array_length; i++) {  //add up all pulse counts
    all_pulses += pulse_array[i];
  }

  return all_pulses / (double)pulse_array_length;  //divide by number of counts. now we have the average
}

uint32_t last_btn_down = 0;
bool btn_was_down = false;
/*
* 0 -> no press
* 1 -> currently held
* 2 -> was short clicked
* 3 -> was long clicked
*/
uint8_t get_btn() {
  if (!digitalRead(1)) {
    if (!btn_was_down) {
      last_btn_down = millis();
      btn_was_down = true;
    }
    return 1;
  } else {
    if (btn_was_down) {
      btn_was_down = false;
      if (millis() - last_btn_down > LONG_CLICK_TIME) return 3;
      else if (millis() - last_btn_down > SHORT_CLICK_TIME) return 2;
    }
    return 0;
  }
}

void do_cal() {
  lcd.home();
  lcd.clear();
  lcd.print(F("Remove all metal"));
  lcd.setCursor(0, 1);
  lcd.print(F("from coil"));
  while (get_btn() < 2) wdt_reset();  //wait for button press
  cal_data.pulses_air = get_pulses();

  lcd.home();
  lcd.clear();
  lcd.print(F("Add smallest obj."));
  lcd.setCursor(0, 1);
  lcd.print(F("to be detected"));
  while (get_btn() < 2) wdt_reset();  //wait for button press
  cal_data.pulses_iron = get_pulses();

  lcd.home();
  lcd.clear();
  EEPROM.put(0, cal_data);
  lcd.print(F("Calibrated"));
  delay(500);
}

void setup() {
  wdt_enable(WDTO_8S);
  pinMode(1, INPUT_PULLUP);  //button
  pinMode(3, INPUT_PULLUP);  //freq
  pinMode(4, OUTPUT);        //speaker

  lcd.init();
  Wire.setClock(400000);
  lcd.createChar(1, gfx_pb_l0);
  lcd.createChar(2, gfx_pb_l1);
  lcd.createChar(3, gfx_pb_m0);
  lcd.createChar(4, gfx_pb_m1);
  lcd.createChar(5, gfx_pb_r0);
  lcd.createChar(6, gfx_pb_r1);
  lcd.createChar(7, gfx_pb_c);
  lcd.clear();
  lcd.home();
  lcd.print(F("Setting up..."));
  lcd.backlight();

  //freq measureing
  cli();
  //enable pin change int
  GIMSK |= (1 << PCIE);
  PCMSK |= (1 << PCINT3);
  //enable timer interrtupt
  TCCR1 = 0;
  TCCR1 |= (1 << CTC1);       //enable clearing timer on compare
  TCCR1 |= (0b1111 << CS10);  //prescaler set to 16348
  TCNT1 = 0;
  OCR1C = TIMER_CMP;
  TIMSK |= (1 << OCIE1A);  //enable timer interrupt
  sei();

  //do_cal();
  EEPROM.get(0, cal_data);
}

void draw_display() {
  uint32_t pulses = get_pulses();

  if (pulses > 0) {
    //set custom char 0/8 as diagram
    byte graph_char[8] = { 0 };
    uint32_t highest_pulse = 0, lowest_pulse = 0;

    for (uint8_t i = 0; i < 5; i++) highest_pulse = max(highest_pulse, pulse_array[i]);
    for (uint8_t i = 0; i < 5; i++) lowest_pulse = min(lowest_pulse, pulse_array[i]);
    for (uint8_t col = 0; col < 5; col++)
      graph_char[max(min(map(pulse_array[col], lowest_pulse, highest_pulse, 0, 7), 7), 0)] =
        (1 << (5 - col));
    lcd.createChar(0, graph_char);

    //lcd.clear();
    lcd.home();

    char line1[17];
    //snprintf(row1, 17, "Freq: %8.3fHz", freq);
    snprintf(line1, 17, "Freq: \x08 %6luHz", (pulses / 2) * TIMER_FREQ);
    lcd.print(line1);

    lcd.setCursor(0, 1);
    uint8_t bars = max(min(map(pulses, cal_data.pulses_air, cal_data.pulses_iron - (cal_data.pulses_air - cal_data.pulses_iron), 0, 16), 16), 0);
    for (uint8_t i = 1; i <= 16; i++) {
      if (i > bars) switch (i) {
          default: lcd.write(3); break;
          case 1: lcd.write(1); break;
          case 8: lcd.write(7); break;
          case 16: lcd.write(5); break;
        }
      else switch (i) {
          default: lcd.write(4); break;
          case 1: lcd.write(2); break;
          case 16: lcd.write(6); break;
        }
    }

    if (beep_flag != 3) {  //if passthru disabled
      /*if (pulses < cal_data.pulses_iron) tone(4, 500);
  else noTone(4);*/
      if (pulses < cal_data.pulses_iron) beep_flag = 1;  //turn beep on
      else {
        beep_flag = 2;  //turn beep off
        digitalWrite(4, LOW);
      }
    }
  } else {
    beep_flag = 2;  //turn beep off
    digitalWrite(4, LOW);

    lcd.clear();
    lcd.home();
    lcd.print(F("Osciltr. stopped"));
    lcd.setCursor(0, 1);
    lcd.print(F("   CHECK COIL!  "));
  }
}

void loop() {
  static uint32_t last_disp_update = 0;
  if (millis() - last_disp_update > 1000) {
    last_disp_update = millis();
    draw_display();
  }

  static bool last_bl = false;
  switch (get_btn()) {
    case 2:
      //beep_flag = beep_flag == 3 ? 2 : 3;  //toggle between passthru and off

      last_bl ? lcd.backlight() : lcd.noBacklight();
      last_bl = !last_bl;
      break;
    case 3:
      do_cal();
      break;
  }

  wdt_reset();
}
