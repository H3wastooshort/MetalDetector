#include <LiquidCrystal_I2C.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <EEPROM.h>

#define SHORT_CLICK_TIME 50  //minimum time for a short click to register
#define LONG_CLICK_TIME 500

uint16_t pulse_array[30] = { 0 };  //ring buffer of latest pulsecounts
constexpr uint8_t pulse_array_length = sizeof(pulse_array) / sizeof(pulse_array[0]);
uint8_t pulse_array_next = 0;
uint16_t pulses_this_int = 0;  //how many pulses since the last timer interrupt
#define TIMER_FREQ 10          //how often the timer int fires, in Hertz
constexpr uint8_t TIMER_CMP =  //timer compare value. RESULT MUST BE SMALLER THAN 255, otherwise increase prescaler in setup()
  (F_CPU /*cpu freq*/ / (4096L /*prescaler*/ * (uint16_t)TIMER_FREQ /*timer freqency*/)) - 1L;

LiquidCrystal_I2C lcd(0x27, 16, 2);

struct cal_data_s {
  float freq_air = 0;
  float freq_iron = 0;
} cal_data;


uint8_t beep_flag = 2;  // 0 or 1 mean beep, 2 means off, 3 means passthru
ISR(PCINT0_vect) {
  pulses_this_int++;
  if (beep_flag == 3) digitalWrite(4, digitalRead(3));
}

ISR(TIMER1_COMPA_vect) {
  pulse_array[pulse_array_next] = pulses_this_int;  //save pulse count
  pulses_this_int = 0;                              //reset pulsecount
  pulse_array_next++;                               //select next position
  pulse_array_next %= pulse_array_length;           //limit to array length. no buffer overflows here

  if (beep_flag < 2) {
    beep_flag ^= 1;  //toggle LSB by XOR with 1
    digitalWrite(4, beep_flag);
  }
}

float get_freq() {
  double average_pulses = 0;
  for (uint8_t i = 0; i < pulse_array_length; i++) {  //add up all pulse counts
    average_pulses += pulse_array[i];
  }
  average_pulses /= pulse_array_length;  //divide by number of counts. now we have the average

  return average_pulses / ((float)TIMER_FREQ / 1000);
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
  cal_data.freq_air = get_freq();

  lcd.home();
  lcd.clear();
  lcd.print(F("Add smallest obj."));
  lcd.setCursor(0, 1);
  lcd.print(F("to be detected"));
  while (get_btn() < 2) wdt_reset();  //wait for button press
  cal_data.freq_iron = get_freq();

  lcd.home();
  lcd.clear();
  EEPROM.put(0, cal_data);
  lcd.print(F("Calibrated"));
  delay(500);
}

void setup() {
  wdt_enable(WDTO_8S);
  pinMode(1, INPUT_PULLUP);  //button
  pinMode(3, INPUT);         //freq
  pinMode(4, OUTPUT);        //speaker

  lcd.init();
  lcd.clear();
  lcd.home();
  lcd.print(F("Setting up..."));

  //freq measureing
  cli();
  //enable pin change int
  /*GIMSK |= (1 << PCIE);
  PCMSK |= (1 << PCINT0);*/
  //enable timer interrtupt
  TCCR1 = 0;
  TCCR1 |= (1 << CTC1);       //enable clearing timer on compare
  TCCR1 |= (0b1101 << CS10);  //prescaler set to 4096
  TCNT1 = 0;
  OCR1C = TIMER_CMP;
  TIMSK |= (1 << OCIE1A);  //enable timer interrupt
  sei();

  //do_cal();
  EEPROM.get(0, cal_data);
}

void draw_display() {
  //lcd.clear();
  lcd.home();
  float freq = get_freq();
  char row1[17];
  snprintf(row1, 17, "Freq: %8.3fHz", freq);
  lcd.print(row1);

  lcd.setCursor(0, 1);
  uint8_t bars = map(freq, cal_data.freq_air, cal_data.freq_iron * 2, 0, 16);
  for (uint8_t i = 1; i <= 16; i++) {
    if (i >= bars) lcd.write(255);
    else lcd.write(' ');
  }

  if (beep_flag != 3) {  //if passthru disabled
    /*if (freq < cal_data.freq_iron) tone(4, 500);
  else noTone(4);*/
    if (freq < cal_data.freq_iron) beep_flag = 2;
    else beep_flag = 1;  //turn beep on
  } else beep_flag = 2;  //turn beep off
}

void loop() {
  static uint32_t last_disp_update = 0;
  if (millis() - last_disp_update > 500) {
    last_disp_update = millis();
    draw_display();
  }

  switch (get_btn()) {
    case 2:
      beep_flag = beep_flag == 3 ? 2 : 3;  //toggle between passthru and off
      break;
    case 3:
      do_cal();
      break;
  }

  wdt_reset();
}
