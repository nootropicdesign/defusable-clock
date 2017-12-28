/*
  Defusable Clock Firmware
  Copyright (C) 2011 nootropic design, LLC
  All rights reserved.
 
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  Countdown begins when red DET button is pressed. Default countdown
  duration is 10 seconds.

  To increase countdown time, hold the red DET button then press HOUR
  and MIN buttons to increase minutes and seconds before releasing
  the DET button.
  
  To decrease countdown time, hold the red DET button and the ALARM
  button, then press HOUR and MIN buttons to decrease minutes and seconds.

  If the countdown duration is changed, it will be remembered next time
  the device is powered on.

  At detonation, trigger goes HIGH for 3 seconds. To change trigger duration,
  change TRIGGER_DURATION_MS.

*/

#include <EEPROM.h>

#define CLOCK 2
#define LATCH 3


#define DATA 4
#define COLON 13
#define MIN_BUTTON 0
#define HOUR_BUTTON 1
#define DET_BUTTON 2
#define ALARM_BUTTON 3
#define MIN_BUTTON_PIN 9
#define HOUR_BUTTON_PIN 10
#define DET_BUTTON_PIN 12 
#define ALARM_BUTTON_PIN 15
#define LED_PM 16
#define LED_ALARM 17
#define LED_TOP 18
#define LED_DET 19
#define BUZZER 11
#define TRIGGER 14
#define WIRE_1 5
#define WIRE_2 6
#define WIRE_3 7
#define WIRE_4 8
#define TIMER1_SECOND_START 49910
#define DEFAULT_COUNTDOWN_DURATION 10
#define TRIGGER_DURATION_MS 3000
#define SNOOZE_MINUTES 9
#define ALARM_OFF 0
#define ALARM_ON 1
#define ALARM_DET 2
#define EEPROM_MAGIC_NUMBER 0xbad0

volatile byte hours = 12;
volatile byte minutes = 0;
volatile byte seconds = 0;
volatile boolean pm = false;
volatile unsigned int countdownDuration = DEFAULT_COUNTDOWN_DURATION;
volatile unsigned int countdownSeconds = DEFAULT_COUNTDOWN_DURATION;
unsigned int defaultCountdownSeconds;
boolean detPressed = false;
boolean displayZeros = false;
volatile boolean ticked = false;
boolean displayCountdown = false;
boolean countdownRunning = false;
boolean isDefused = false;
boolean silent = false;

byte buttonPins[4] = {MIN_BUTTON_PIN, HOUR_BUTTON_PIN, DET_BUTTON_PIN, ALARM_BUTTON_PIN};
byte buttonState[4] = {HIGH, HIGH, HIGH, HIGH};
unsigned long buttonChange[4] = {0L, 0L, 0L, 0L};

byte alarmHours = 12;
byte alarmMinutes = 0;
boolean alarmpm = false;
byte alarmMode = ALARM_OFF;
volatile boolean alarmRinging = false;
boolean displayAlarmTime = false;
// Set to true if you want the PM LED on during PM hours.  I think it's too bright and
// annoying, so I'm setting this to false by default.
boolean usePMIndicator = false;

byte snoozeHours = 12;
byte snoozeMinutes = 0;
byte snoozepm = false;
boolean snoozeActivated = false;

boolean blank = false;

volatile byte currentDigit = 0;

void setup() {

  pinMode(CLOCK, OUTPUT);
  pinMode(LATCH, OUTPUT);
  pinMode(DATA, OUTPUT);
  pinMode(COLON, OUTPUT);
  digitalWrite(COLON, LOW);

  pinMode(LED_PM, OUTPUT);      
  pinMode(LED_ALARM, OUTPUT);      
  pinMode(LED_TOP, OUTPUT);      
  pinMode(LED_DET, OUTPUT);      
  pinMode(BUZZER, OUTPUT);
  pinMode(TRIGGER, OUTPUT);

  pinMode(HOUR_BUTTON_PIN, INPUT);     
  pinMode(MIN_BUTTON_PIN, INPUT);     
  pinMode(ALARM_BUTTON_PIN, INPUT);     
  pinMode(DET_BUTTON_PIN, INPUT);     
  pinMode(WIRE_1, INPUT);     
  pinMode(WIRE_2, INPUT);     
  pinMode(WIRE_3, INPUT);     
  pinMode(WIRE_4, INPUT);     

  digitalWrite(HOUR_BUTTON_PIN, HIGH);
  digitalWrite(MIN_BUTTON_PIN, HIGH);
  digitalWrite(ALARM_BUTTON_PIN, HIGH);
  digitalWrite(DET_BUTTON_PIN, HIGH);
  digitalWrite(WIRE_1, HIGH);
  digitalWrite(WIRE_2, HIGH);
  digitalWrite(WIRE_3, HIGH);
  digitalWrite(WIRE_4, HIGH);

  // Read data from EEPROM
  // User can hold HOUR and MIN buttons to skip EEPROM read (factory reset procedure).
  if (EEPROMValid() && (!((buttonPressed(HOUR_BUTTON)) && (buttonPressed(MIN_BUTTON))))) {
    hours = EEPROM.read(2);
    minutes = EEPROM.read(3);
    seconds = EEPROM.read(4);
    pm = EEPROM.read(5);
    alarmHours = EEPROM.read(6);
    alarmMinutes = EEPROM.read(7);
    alarmpm = EEPROM.read(8);
    alarmMode = EEPROM.read(9);
    defaultCountdownSeconds = EEPROM.read(10);
    defaultCountdownSeconds = defaultCountdownSeconds << 8;
    defaultCountdownSeconds |= EEPROM.read(11);
    if (defaultCountdownSeconds > 5999) {
      // guard against bad data
      defaultCountdownSeconds = DEFAULT_COUNTDOWN_DURATION;
    }
  } else {
    hours = 12;
    minutes = 0;
    seconds = 0;
    alarmHours = 12;
    alarmMinutes = 0;
    pm = false;
    alarmpm = false;
    alarmMode = ALARM_OFF;
    defaultCountdownSeconds = DEFAULT_COUNTDOWN_DURATION;
    writeEEPROM();
  }



  // Initialize timers.
  // Timer1 is used to keep the clock time
  // Timer2 is used for the display multiplexing

  // Disable the timer overflow interrupt
  TIMSK2 &= ~(1 << TOIE2);

  // Set timer2 to normal mode
  TCCR2A &= ~((1 << WGM21) | (1 << WGM20));
  TCCR2B &= ~(1 << WGM22);

  // Use internal I/O clock
  ASSR &= ~(1 << AS2);

  // Disable compare match interrupt
  TIMSK2 &= ~(1 << OCIE2A);

  // Prescalar is clock divided by 128
  TCCR2B |= (1 << CS22);
  TCCR2B &= ~(1 << CS21);
  TCCR2B |= (1 << CS20);

  // Start the counting at 0
  TCNT2 = 0;

  // Enable the timer2 overflow interrupt
  TIMSK2 |= (1 << TOIE2);  


  // init timer1
  // set prescaler to 1024
  TIMSK1 &= ~(1<<TOIE1);
  TCCR1A = 0;
  TCCR1B = (1<<CS12) | (1<<CS10);
  TIMSK1 |= (1<<TOIE1);
  // With prescalar of 1024, TCNT1 increments 15,625 times per second
  // 65535 - 15625 = 49910
  TCNT1 = TIMER1_SECOND_START;

  randomSeed(analogRead(0));

  if (buttonPressed(DET_BUTTON)) {
    // enable silent mode for testing
    beep(3500, 50);
    silent = true;
    while (buttonPressed(DET_BUTTON)); // wait for release
  }

  while ((buttonPressed(HOUR_BUTTON)) || (buttonPressed(MIN_BUTTON))); // wait for release of factory reset procedure

}

void loop() {

  delay(10); // this helps with button debouncing

  if (ticked) {
    ticked = false;
    writeEEPROM();
  }

  if (alarmRinging) {
    if (alarmMode == ALARM_ON) {
      ringAlarm();
    }
    if (alarmMode == ALARM_DET) {
      for(int i=0;i<4;i++) {
	beep(3900, 250, false);
	delay(250);
      }
      displayCountdown = true;
      countdownSeconds = defaultCountdownSeconds;
      countdown();
      alarmRinging = false;
    }
  }

  // check input
  if ((buttonPressed(ALARM_BUTTON)) && (!displayCountdown)) {
    displayAlarmTime = true;
    if (alarmpm) {
      digitalWrite(LED_PM, HIGH);
    } else {
      digitalWrite(LED_PM, LOW);
    }
    if (alarmMode == ALARM_OFF) {
      digitalWrite(LED_ALARM, LOW);
      digitalWrite(LED_DET, LOW);
    } else {
      digitalWrite(LED_ALARM, HIGH);
      if (alarmMode == ALARM_DET) {
	digitalWrite(LED_DET, HIGH);
      } else {
	digitalWrite(LED_DET, LOW);
      }
    }
  } else {
    displayAlarmTime = false;
    digitalWrite(LED_ALARM, LOW);
    digitalWrite(LED_DET, LOW);
  }
    
  if (buttonPressedNew(HOUR_BUTTON) || buttonHeld(HOUR_BUTTON, 150)) {
    if ((!displayAlarmTime) && (!displayCountdown)) {
      hours++;
      if (hours == 12) {
	pm = !pm;
      }
      if (hours == 13) {
	hours = 1;
      }
      if (pm) {
	digitalWrite(LED_PM, HIGH);
      } else {
	digitalWrite(LED_PM, LOW);
      }
    }
    if (displayAlarmTime) {
      // setting the alarm
      alarmHours++;
      if (alarmHours == 12) {
	alarmpm = !alarmpm;
      }
      if (alarmHours == 13) {
	alarmHours = 1;
      }
      if (alarmpm) {
	digitalWrite(LED_PM, HIGH);
      } else {
	digitalWrite(LED_PM, LOW);
      }
      snoozeHours = alarmHours;
      snoozeMinutes = alarmMinutes;
      snoozepm = alarmpm;
    }
    if (displayCountdown) {
      if (!buttonPressed(ALARM_BUTTON)) {
	if (countdownSeconds < 5940) {
	  countdownSeconds += 60;
	  countdownDuration += 60;
	}
      } else {
	if (countdownSeconds >= 60 ) {
	  countdownSeconds -= 60;
	  countdownDuration -= 60;
	}
      }
    }
  } else {
    if ((!displayAlarmTime) && (!buttonPressed(HOUR_BUTTON))) {
      if ((pm) && (usePMIndicator)) {
	digitalWrite(LED_PM, HIGH);
      } else {
	digitalWrite(LED_PM, LOW);
      }
    }
  }

  if (buttonPressedNew(MIN_BUTTON) || buttonHeld(MIN_BUTTON, 150)) {
    if ((!displayAlarmTime) && (!displayCountdown)) {
      minutes++;
      if (minutes == 60) {
	minutes = 0;
      }
      seconds = 0;
      TCNT1 = TIMER1_SECOND_START;
    }
    if (displayAlarmTime) {
      // setting the alarm
      alarmMinutes++;
      if (alarmMinutes == 60) {
	alarmMinutes = 0;
      }
      snoozeHours = alarmHours;
      snoozeMinutes = alarmMinutes;
      snoozepm = alarmpm;
    }
    if (displayCountdown) {
      if (!buttonPressed(ALARM_BUTTON)) {
	if (countdownSeconds < 5999) {
	  countdownSeconds++;
	  countdownDuration++;
	}
      } else {
	if (countdownSeconds > 0) {
	  countdownSeconds--;
	  countdownDuration--;
	}
      }	
    }
  }

  if (buttonPressedNew(DET_BUTTON)) {
    if (displayAlarmTime) {
      alarmMode++;
      if (alarmMode > ALARM_DET) {
	alarmMode = ALARM_OFF;
      }
      if (alarmMode == ALARM_OFF) {
	snoozeActivated = false;
      }
      return;
    }
    if ((displayZeros) || (isDefused)) {
      isDefused = false;
      displayZeros = false;
      displayCountdown = false;
      return;
    }
    // The DET button has been pressed but not released yet.
    detPressed = true;
    countdownSeconds = defaultCountdownSeconds;
    displayCountdown = true;
  }

  if (!buttonPressed(DET_BUTTON)) {
    if (detPressed) {
      detPressed = false;
      defaultCountdownSeconds = countdownSeconds;
      writeEEPROM();
      countdown();
    }
  }

}

void ringAlarm() {
  int frequency = 3900;
  int duration = 250;  // each beep is .25s
  int us = 1000000 / frequency / 2;
  int toneLoopCount = (duration * ((float)frequency/1000.0));
  int pauseLoopCount = 20000;

  while (alarmRinging) {
    for(int i=0;i<toneLoopCount;i++) {
      PORTB |= (1 << 3);
      if (buttonPressed(ALARM_BUTTON)) {
	alarmRinging = false;
	snoozeActivated = false;
	break;
      }
      delayMicroseconds(us);
      PORTB &= ~(1 << 3);
      if (buttonPressed(DET_BUTTON)) {
	alarmRinging = false;
	snooze();
	break;
      }
      delayMicroseconds(us);
    }
    
    for(int i=0;i<pauseLoopCount;i++) {
      if (buttonPressed(ALARM_BUTTON)) {
	alarmRinging = false;
	snoozeActivated = false;
	break;
      }
      if (buttonPressed(DET_BUTTON)) {
	alarmRinging = false;
	snooze();
	break;
      }
    }
  } // while (alarmRinging)
}

void snooze() {
  snoozeActivated = true;

  // set the snooze time to current time plus 9 minutes
  snoozeHours = hours;
  snoozepm = pm;
  snoozeMinutes = minutes + SNOOZE_MINUTES;
  if (snoozeMinutes >= 60) {
    snoozeMinutes -= 60;
    snoozeHours++;
    if (snoozeHours == 12) {
      snoozepm = !snoozepm;
    }
    if (snoozeHours == 13) {
      snoozeHours = 1;
    }
  }
}

void countdown() {
  int ledCounter = 0;
  int ledCounterThreshold = 100000;
  byte ledCurrentState = HIGH;
  byte defusePin;
  byte detPin;
  boolean defused = false;
  countdownRunning = true;
  int fractionalSecond;


  // assign random pins
  defusePin = random(WIRE_1, (WIRE_4+1));
  detPin = defusePin;
  while (detPin == defusePin) {
    detPin = random(WIRE_1, (WIRE_4+1));
  }

  digitalWrite(LED_PM, LOW); // turn off the PM LED

  // Keep track of how far we are into the current
  // second so we can correct later.
  fractionalSecond = TCNT1 - TIMER1_SECOND_START;

  // Reset back to the last second boundary so we can start the countdown
  // immediately and so that the first second isn't truncated
  TCNT1 = TIMER1_SECOND_START;

  beep(3800, 30);
  digitalWrite(LED_DET, ledCurrentState);
  while ((countdownSeconds > 0) && (!defused)) {
    for(int i=0;i<10000;i++) {
      // get input
      if (digitalRead(detPin) == HIGH) {
	countdownSeconds = 0;
	break;
      }
      if (digitalRead(defusePin) == HIGH) {
	defused = true;
	break;
      }
    }
    delay(20);
    if (ledCounter++ > ledCounterThreshold) {
      ledCounter = 0;
      if (ledCurrentState == HIGH) {
	ledCurrentState = LOW;
      } else {
	ledCurrentState = HIGH;
      }
      digitalWrite(LED_DET, ledCurrentState);
    }
  }
  digitalWrite(LED_DET, LOW);
  countdownRunning = false;
  if (!defused) {
    detonate();
  } else {
    beep(4500, 80);
    isDefused = true;
    delay(2000);
  }

  // Now to keep the time accurate, add back in the fractional
  // second that we took off when we started the countdown sequence.
  // Wait until we can add it back to TCNT1 without overflowing.
  while (TCNT1 >= (65535 - fractionalSecond));
  TCNT1 += fractionalSecond;
}

void detonate() {
  for(int i=0;i<8;i++) {
    digitalWrite(LED_DET, HIGH);
    beep(5000, 50, false);
    delay(25);
    digitalWrite(LED_DET, LOW);
    delay(25);
  }

  blank = true;

  unsigned long triggerStart = millis();
  unsigned long triggerStop = triggerStart + TRIGGER_DURATION_MS;
  digitalWrite(TRIGGER, HIGH);

  for(int i=0;i<50;i++) {
    if (millis() >= triggerStop) {
      digitalWrite(TRIGGER, LOW);
    }
    digitalWrite(random(LED_PM, LED_DET+1), HIGH);
    digitalWrite(random(LED_PM, LED_DET+1), HIGH);
    for(int j=0;j<5;j++) {
      beep(random(100, 300), 10);
    }
    for(int led=LED_PM;led<=LED_DET;led++) {
      digitalWrite(led, LOW);
    }
  }

  displayCountdown = false;
  blank = false;
  displayZeros = true;

  while (millis() < triggerStop) {
    if (buttonPressedNew(DET_BUTTON)) {
      displayZeros = false;
      break;
    }
  }

  digitalWrite(TRIGGER, LOW);
}

// return true if the button is pressed.
boolean buttonPressed(byte button) {
  if (digitalRead(buttonPins[button]) == LOW) {
    // the button is currently pressed
    if (buttonState[button] == HIGH) {
      // if the button was not pressed before, update the state.
      buttonChange[button] = millis();
      buttonState[button] = LOW;
    }
    return true;
  } else {
    // The button is currently not pressed
    if (buttonState[button] == LOW) {
      // if the button was pressed before, update the state.
      buttonChange[button] = millis();
      buttonState[button] = HIGH;
    }
    return false;
  }
}

// return true if the button is pressed and it is a new press (not held)
boolean buttonPressedNew(byte button) {
  if (digitalRead(buttonPins[button]) == LOW) {
    // The button is currently pressed
    if (buttonState[button] == HIGH) {
      // This is a new press.
      buttonChange[button] = millis();
      buttonState[button] = LOW;
      return true;
    }
    // This is not a new press.
    return false; 
  } else {
    // The button is currently not pressed
    if (buttonState[button] == LOW) {
      buttonChange[button] = millis();
      buttonState[button] = HIGH;
    }
    return false;
  }
}

// return true if the button is pressed and has been held for at least n milliseconds
boolean buttonHeld(byte button, int n) {
  if (digitalRead(buttonPins[button]) == LOW) {
    // the button is currently pressed
    if (buttonState[button] == HIGH) {
      // if the button was not pressed before, update the state and return false.
      buttonChange[button] = millis();
      buttonState[button] = LOW;
      return false;
    }
    if ((millis() - buttonChange[button]) >= n) {
      // the button has been pressed for over n milliseconds.
      // update the state change time even though the state hasn't changed.
      // we update the state change time so we can start the counting over
      buttonChange[button] = millis();
      return true;
    }
    // The button is being held, but has not been held for longer than n milliseconds.
    return false;
  } else {
    // The button is currently not pressed
    if (buttonState[button] == LOW) {
      // if the button was pressed before, update the state.
      buttonChange[button] = millis();
      buttonState[button] = HIGH;
    }
    return false;
  }
}

void beep(int frequency, int duration) {
  beep(frequency, duration, true);
}

void beep(int frequency, int duration, boolean disableDisplayInterrupt) {
  int us = 1000000 / frequency / 2;
  int loopCount = (duration * ((float)frequency/1000.0));
  if (disableDisplayInterrupt) {
    TIMSK2 &= ~(1 << TOIE2);
  }
  for(int i=0;i<loopCount;i++) {
    if (!silent) PORTB |= (1 << 3);
    delayMicroseconds(us);
    if (!silent) PORTB &= ~(1 << 3);
    delayMicroseconds(us);
  }
  TIMSK2 |= (1 << TOIE2);
}

void writeEEPROM() {
  setEEPROMValid();
  EEPROM.write(2, hours);
  EEPROM.write(3, minutes);
  EEPROM.write(4, seconds);
  EEPROM.write(5, pm);
  EEPROM.write(6, alarmHours);
  EEPROM.write(7, alarmMinutes);
  EEPROM.write(8, alarmpm);
  EEPROM.write(9, alarmMode);
  EEPROM.write(10, (defaultCountdownSeconds >> 8));
  EEPROM.write(11, (defaultCountdownSeconds & 0xFF));
}

boolean EEPROMValid() {
  // determine if the EEPROM has ever been written by this firmware
  // so we can determine if the values can be trusted
  unsigned int magic = EEPROM.read(0);
  magic = magic << 8;
  magic |= EEPROM.read(1);
  return (magic == EEPROM_MAGIC_NUMBER);
}
  
void setEEPROMValid() {
  EEPROM.write(0, EEPROM_MAGIC_NUMBER >> 8);
  EEPROM.write(1, (EEPROM_MAGIC_NUMBER & 0xFF));
}

// This is the display interrupt to implement multiplexing of the digits.
ISR(TIMER2_OVF_vect) {
  byte nDigits = 4;
  byte data;
  byte digitValue;
  byte displayHours, displayMinutes;

  TCNT2 = 0;

  displayHours = hours;
  displayMinutes = minutes;
  if (displayAlarmTime) {
    displayHours = alarmHours;
    displayMinutes = alarmMinutes;
  }
  if (displayCountdown) {
    displayHours = countdownSeconds / 60;
    displayMinutes = countdownSeconds % 60;
  }
  if (displayZeros) {
    displayHours = 0;
    displayMinutes = 0;
  }

  if ((displayHours < 10) && (!displayCountdown) && (!displayZeros)) {
    nDigits = 3;
  }


  if (++currentDigit > (nDigits-1)) {
    currentDigit = 0;
  }

  switch (currentDigit) {
  case 0:
    digitValue = displayMinutes % 10;
    break;
  case 1:
      digitValue = displayMinutes / 10;
    break;
  case 2:
    digitValue = displayHours % 10;
    break;
  case 3:
    digitValue = displayHours / 10;
    break;
  }

  // Upper 4 bits of data are the value for the current digit.
  // They are loaded into shift register outputs QA-QD
  data = (digitValue << 4);

  // Lower 4 bits 3-0 represent which digit to turn on.
  // 3 is most significant digit, 0 is least
  // They are loaded into shift register outputs QE-QH
  // Digit transistors are active low, so set them all high
  data |= 0x0F;

  if (!blank) {
    // now turn off the bit for digit we want illuminated.
    data &= ~(1 << currentDigit);
  }

  digitalWrite(LATCH, LOW);
  shiftOut(DATA, CLOCK, LSBFIRST, data);
  digitalWrite(LATCH, HIGH);
}


// Timer 1 interrupt.  This executes every second.
ISR(TIMER1_OVF_vect) {
  TCNT1 = TIMER1_SECOND_START;

  ticked = true;
  seconds++;
  if (seconds == 60) {
    seconds = 0;
    minutes++;
    if (minutes == 60) {
      minutes = 0;
      hours++;
      if (hours == 12) {
	pm = !pm;
      }
      if (hours == 13) {
	hours = 1;
      }
    }
  }

  if ((!countdownRunning) && (alarmMode != ALARM_OFF)) {
    if ((alarmHours == hours) && (alarmMinutes == minutes) && (seconds == 0) && (alarmpm == pm)) {
      alarmRinging = true;
    }
    if ((snoozeActivated) && (snoozeHours == hours) && (snoozeMinutes == minutes) && (seconds == 0) && (snoozepm == pm)) {
      alarmRinging = true;
    }
  }

  if ((countdownRunning) && (countdownSeconds > 0)) {
    beep(3800, 30);
    countdownSeconds--;
  }
}
