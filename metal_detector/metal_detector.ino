#include <LiquidCrystal_I2C.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);


uint16_t pulse_array[32] = { 0 };
constexpr uint8_t pulse_array_length = sizeof(pulse_array) / sizeof(pulse_array[0]);
uint8_t pulse_array_next = 0;
uint16_t pulses_this_int = 0;  //how many pulses since the last timer interrupt
#define TIMER_FREQ 10          //how often the timer int fires, in Hertz
constexpr uint8_t TIMER_CMP =  //timer compare value. RESULT MUST BE SMALLER THAN 255, otherwise increase prescaler in setup()
  (F_CPU /*cpu freq*/ / (4096L /*prescaler*/ * (uint16_t)TIMER_FREQ /*timer freqency*/)) - 1L;

float freq_air = 0;
float freq_iron = 0;

ISR(PCINT3_vect) {
  pulses_this_int++;
}

ISR(TIMER1_COMPA_vect) {
  pulse_array[pulse_array_next] = pulses_this_int;  //save pulse count
  pulses_this_int = 0;                              //reset pulsecount
  pulse_array_next++;                               //select next position
  pulse_array_next %= pulse_array_length;           //limit to array length. no buffer overflows here
}

bool btn_flag = false;
uint32_t last_btn_down = 0;
ISR(PCINT1_vect) {
  if (millis() - last_btn_down > 50) {
    last_btn_down = millis();
    btn_flag = true;
  }
}

float get_freq() {
  double average_pulses = 0;
  for (uint8_t i = 0; i < pulse_array_length; i++) {  //add up all pulse counts
    average_pulses += pulse_array[i];
  }
  average_pulses /= pulse_array_length;  //divide by number of counts. now we have the average

  return average_pulses / (TIMER_FREQ / 1000);
}

void do_cal() {
  btn_flag = false;

  lcd.home();
  lcd.clear();
  lcd.print(F("Remove all metal"));
  lcd.setCursor(0, 1);
  lcd.print(F("from coil"));
  while (!btn_flag) wdt_reset();  //wait for button press
  btn_flag = false;
  freq_air = get_freq();

  lcd.home();
  lcd.clear();
  lcd.print(F("Add smallest obj."));
  lcd.setCursor(0, 1);
  lcd.print(F("to be detected"));
  while (!btn_flag) wdt_reset();
  btn_flag = false;
  freq_iron = get_freq();

  lcd.home();
  lcd.clear();
  lcd.print(F("Calibrated"));
  delay(500);
}

void setup() {
  wdt_enable(WDTO_8S);
  pinMode(1, INPUT_PULLUP);
  pinMode(3, INPUT);

  lcd.init();
  lcd.clear();
  lcd.home();
  lcd.print(F("Setting up..."));

  /*//freq measureing
  cli();
  //enable pin change int
  GIMSK |= (1 << PCIE);
  PCMSK |= (1 << PCINT3);
  PCMSK |= (1 << PCINT1);
  //enable timer interrtupt
  TCCR1 = 0;
  TCCR1 |= (1 << CTC1);       //enable clearing timer on compare
  TCCR1 |= (0b1101 << CS10);  //prescaler set to 4096
  TCNT1 = 0;
  OCR1C = TIMER_CMP;
  TIMSK |= (1 << OCIE1A);  //enable timer interrupt
  sei();*/

  do_cal();
}

void draw_display() {
  //lcd.clear();
  lcd.home();
  float freq = get_freq();
  char row1[17];
  snprintf(row1, 17, "Freq: %8.3fHz", freq);

  lcd.setCursor(0, 1);
  uint8_t bars = map(freq, freq_air, freq_iron * 2, 0, 16);
  for (uint8_t i = 1; i <= 16; i++)
    if (i >= bars) lcd.write(255);
    else lcd.write(' ');
}

void loop() {
  static uint32_t last_disp_update = 0;
  if (millis() - last_disp_update > 1000) {
    last_disp_update = millis();
    draw_display();
  }

  if (btn_flag) do_cal();

  wdt_reset();
}
