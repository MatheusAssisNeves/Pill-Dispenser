#include <Wire.h>
#include <Stepper.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <EEPROM.h>

///////////////////////////////////// PINS

const int buzzer = 7;
const int buttonPin[2] = {2, 3};

///////////////////////////////////// DEFINES

//number of segments on Carousel
const int numberSegments = 12;
//max stored dispense times possible
const int MaxDispTimes = 6;

///////////////////////////////////// BUTTONS

int buttonState[2];
int lastButtonState[2] = {LOW, LOW};

unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

///////////////////////////////////// LCD

LiquidCrystal_I2C lcd(0x27, 20, 4);

//clock symbol
byte customChar0[] = {
  B00000,
  B01110,
  B10101,
  B10111,
  B10001,
  B01110,
  B00000,
  B00000
};

//check symbol
byte customChar1[] = {
  B00000,
  B00000,
  B00001,
  B00011,
  B10110,
  B11100,
  B01000,
  B00000
};

///////////////////////////////////// STEPPER

const int stepsPerRevolution = 2048;

Stepper stepper(stepsPerRevolution, 8, 10, 9, 11);

///////////////////////////////////// RTC
RTC_DS3231 rtc;

///////////////////////////////////// VARIABLES


//store number of dispense times
int dispTimes = 0;

//store dispense times
struct Time {
  uint8_t  hh;
  uint8_t  mm;
};

Time dispTime[MaxDispTimes];

//controll of the dispense times that has already been dispensed
unsigned dispensed;

//control variable to lock dispenser from dispensing untill midnight
bool lock = false;



////////////////////////////////////////////////////////////////////// SETUP

void setup() {
    Serial.begin(9600);

    load_dispTime(); //pulls data from EEPROM back to global variables
    
    //pins
    pinMode(buttonPin[0], INPUT);
    pinMode(buttonPin[1], INPUT);
    pinMode(buzzer, OUTPUT);


    //lcd
    lcd.init();
    lcd.setBacklight(HIGH);
    lcd.createChar(0, customChar0);
    lcd.createChar(1, customChar1);

    //stepper
    stepper.setSpeed(5);

    //rtc
    if (! rtc.begin()) {
      Serial.println("Couldn't find RTC");
      Serial.flush();
      while (1) delay(10);
    }

    if (rtc.lostPower()) {
      Serial.println("RTC lost power, let's set the time!");
      setTime();
    }

    working();

    lcd.clear();
    lcd.print("ERROR");
}

////////////////////////////////////////////////////////////////////// LOOP

void loop() {
}

///////////////////////////////////////////////////////////////////// FUNCTIONS

//Working state
void working() {
  bool once = true;
  int next;
  while (1) {
      if (once) { //run once
        once = false;
        next = nextDispense();
        lcd.setBacklight(HIGH);
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Cur. time: ");
        lcd.setCursor(0,2);
        lcd.print("Next hour: ");

        lcd.setCursor(17,3);
        lcd.print("+");
        lcd.write(0);
        lcd.setCursor(0,3);

        tone(buzzer, 523, 250);
        delay(250);
        tone(buzzer, 587, 250);
        delay(250);
        tone(buzzer, 659, 500);
      }

      DateTime now = rtc.now();  //pull curent time from RTC
      //print curent time
      lcd.setCursor(11,0);
      char buf1[] = "hh:mm:ss";
      lcd.print(now.toString(buf1));
      //print next dispense time
      lcd.setCursor(11,2);
      timeDisplay(dispTime[next].hh ,dispTime[next].mm);

      //lcd.setCursor(0,3);
      //lcd.print(lock);

      //first dispense of day is now, so unlock block from previous day
      if(lock == true && dispTime[0].hh > now.hour() && dispTime[0].mm > now.minute()) {
        lock = false;
      }

      //time match dispense now
      if(dispTime[next].hh <= now.hour() && dispTime[next].mm <= now.minute() && !lock)  {
        Serial.println("DISPENSING");
        dispense(1);
        confirmCollect();
        once = true;
        next++;
        if (next >= dispTimes) {
          next = 0;     //next dispense will be the first one from the next day
          lock = true;  //lock until first dipense time of next day
        }
      }
      if(buttonPress(1)) {
        newDispTimes();
        lock = false;
        once = true;
      }
      //delay(1000);
  }
}

//Insert new dispense times
void newDispTimes() {

  lcd.clear();
  lcd.setCursor(1,0);
  lcd.print("Modify schedule?");
  lcd.setCursor(0,1);
  printDispTimes();

  lcd.setCursor(5,3);
  lcd.print("No");

  lcd.setCursor(12,3);
  lcd.print("Yes");

  while (1) {
    if(buttonPress(0)) {//  NO
      return;
    }
    if(buttonPress(1)) {//  YES
      break;
    }
  }

  bool check = true;
  int indx = 0;
  int control = 0;
  int correctTime = 0;
  while (1) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Inserting hours:");
    lcd.setCursor(0,2);
    lcd.print("Hour ");
    lcd.print(indx + 1);
    lcd.print(":");

    lcd.setCursor(3,3);
    lcd.print("+");

    lcd.setCursor(16,3);
    lcd.write(1);

    //preset time for better user interaction
    if (correctTime == 0) {
      if (indx == 0) {
        dispTime[indx].hh = 0;
        dispTime[indx].mm = 0;
      } else {
        dispTime[indx].hh = dispTime[indx - 1].hh;
        dispTime[indx].mm = dispTime[indx - 1].mm;
      }
    }
    correctTime = 0;
    unsigned long timePassed = millis();
    bool forceTime = 1;//force digits to apear(ignore blick once)

    while (1) {
      if (control == 0) { //choosing hours
        if(buttonPress(0)) {// +
          forceTime = 1;
          if(dispTime[indx].hh < 24) {
            dispTime[indx].hh++;
          } else {
            dispTime[indx].hh = 0;
          }
        }
        if(buttonPress(1)) {// OK
          control = 1;
        }
      }

      if (control == 1) { //choosing minutes
        if(buttonPress(0)) {// +
          forceTime = 1;
          if(dispTime[indx].mm < 59) {
            dispTime[indx].mm += 1;
          } else {
            dispTime[indx].mm = 0;
          }
        }
        if(buttonPress(1)) {// OK
          break;
        }
      }

      //update selecting time display and blink digits
      if ((millis() - timePassed) == 500 || forceTime) {
        if (forceTime) timePassed = millis();
        forceTime = 0;
        lcd.setCursor(8,2);
        timeDisplay(dispTime[indx].hh ,dispTime[indx].mm);
      }
      if ((millis() - timePassed) >= 1000) {
        timePassed = millis();
        if (control == 0) lcd.setCursor(8,2);
        if (control == 1) lcd.setCursor(11,2);  
        lcd.print("  ");

      }
    }
    // confirm adding dispense time?
    lcd.clear();
    lcd.setCursor(3,1);
    lcd.print("Confirm ");
    timeDisplay(dispTime[indx].hh ,dispTime[indx].mm);
    lcd.print("?");

    lcd.setCursor(5,3);
    lcd.print("No");

    lcd.setCursor(12,3);
    lcd.print("Yes");

    while (1) {
      if(buttonPress(0)) {//  NO
        control = 0;
        dispTimes = indx;
        correctTime = 1;
        break;
      }
      if(buttonPress(1)) {//  YES
        indx++;
        dispTimes = indx;
        control = 2;
        break;
      }
    }
    while (control == 2) { //Finish, want to store and execute or add more dispense times?
      lcd.clear();
      lcd.setCursor(1,0);
      lcd.print("Record ");
      lcd.print(dispTimes);
      if (dispTimes == 1) lcd.print(" hour?");
      else lcd.print(" hours");
      lcd.setCursor(0,1);

      lcd.setCursor(5,3);
      lcd.print("+");
      lcd.write(0);

      lcd.setCursor(12,3);
      lcd.print("Yes");

      //display all dispense times
      sort_dispTime();
      lcd.setCursor(0,1);
      printDispTimes();
      while (1) {
        if (buttonPress(0)){ // +
          control = 0;
          if (dispTimes == MaxDispTimes) {//max Dispense Time limit reached
            control = 2;
            lcd.clear();
            lcd.setCursor(1,1);
            lcd.print("Maximum limit of ");
            lcd.print(MaxDispTimes);
            lcd.setCursor(1,2);
            lcd.print("hours reached");
            tone(buzzer, 110, 250);
            delay(2000);
          }
          break; 
        }
        if(buttonPress(1)) {//confirms saved times
          sort_dispTime();
          store_dispTime();

          lcd.clear();
          lcd.setCursor(1,1);
          lcd.print("Schedule recorded");
          lcd.setCursor(4,2);
          lcd.print("Starting...");
          delay(3500);

          return;//back to working()
        }
      }
    }
  }
}

//set time on RTC
void setTime () {
  unsigned long timePassed = millis();
  bool forceTime = 1;//force digits to apear(ignore blick once)
  bool control = 0;
  Time setTime;
  setTime.hh = 0;
  setTime.mm = 0;
  while (1) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Enter current time:");

    lcd.setCursor(3,3);
    lcd.print("+");

    lcd.setCursor(16,3);
    lcd.write(1);
    while (1) {
      if (control == 0) { //choosing hours
        if(buttonPress(0)) {// +
          forceTime = 1;
          if(setTime.hh < 24) {
            setTime.hh++;
          } else {
            setTime.hh = 0;
          }
        }
        if(buttonPress(1)) {// OK
          control = 1;
        }
      }

      if (control == 1) { //choosing minutes
        if(buttonPress(0)) {// +
          forceTime = 1;
          if(setTime.mm < 59) {
            setTime.mm += 1;
          } else {
            setTime.mm = 0;
          }
        }
        if(buttonPress(1)) {// OK
          break;
        }
      }

      //update selecting time display and blink digits
      if ((millis() - timePassed) == 500 || forceTime) {
        if (forceTime) timePassed = millis();
        forceTime = 0;
        lcd.setCursor(8,2);
        timeDisplay(setTime.hh ,setTime.mm);
      }
      if ((millis() - timePassed) >= 1000) {
        timePassed = millis();
        if (control == 0) lcd.setCursor(8,2);
        if (control == 1) lcd.setCursor(11,2);  
        lcd.print("  ");

      }
    }
    //confirm time
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Confirm ");
    timeDisplay(setTime.hh ,setTime.mm);
    lcd.print(" ?");

    lcd.setCursor(5,3);
    lcd.print("No");

    lcd.setCursor(12,3);
    lcd.print("Yes");

    while (1) {
      if(buttonPress(0)) {//  NO
        control = 0;
        break;
      }
      if(buttonPress(1)) {//  YES
        rtc.adjust(DateTime(2000, 01, 01, setTime.hh ,setTime.mm, 0));
        return;
      }
    }
  }
}

//alert and wait for the user to press the button
void confirmCollect() {
  bool ledState = true;
  unsigned long timePassed = millis();
  lcd.clear();
  lcd.setCursor(2,1);
  lcd.print("Collect medicine");
  lcd.setCursor(0,2);
  lcd.print("and press the button");

  while (1) {
    if ((millis() - timePassed) == 500)
      lcd.setBacklight(LOW);
    if ((millis() - timePassed) == 1000)
      lcd.setBacklight(HIGH);
    if ((millis() - timePassed) > 1000) {
      timePassed = millis();
      tone(buzzer, 500, 500);
    }

    if(buttonPress(1) || buttonPress(0)) { // press to confirm
      lcd.setBacklight(HIGH);
      return;// back to working
    }
  }
}

//sort dispense times
void sort_dispTime() {
  for (int step = 1; step < dispTimes; step++) {
    Time key = dispTime[step];
    int j = step - 1;

    while (((key.hh < dispTime[j].hh) || (key.hh == dispTime[j].hh && key.mm < dispTime[j].mm)) && j >= 0) {
      dispTime[j + 1] = dispTime[j];
      --j;
    }
    dispTime[j + 1] = key;
  }
}

//stores dispTime[] and dispTimes to EEPROM
void store_dispTime() {
  int addr = 0;
  EEPROM.put(addr, dispTimes);
  addr += sizeof(dispTimes);
  for (int i = 0; i < dispTimes; i++) {
    EEPROM.put(addr, dispTime[i]);
    addr += sizeof(struct Time);
  }
}

//loads dispTime[] and dispTimes from EEPROM
void load_dispTime() {
  int addr = 0;
  EEPROM.get(addr, dispTimes);
  addr += sizeof(dispTimes);
  for (int i = 0; i < dispTimes; i++) {
    EEPROM.get(addr, dispTime[i]);
    addr += sizeof(struct Time);
  }
}

//print stored Dispense Times
void printDispTimes() {
  lcd.setCursor(0,1);
  for (int i = 0; i < dispTimes; i++) {
    lcd.write(0);
    timeDisplay(dispTime[i].hh ,dispTime[i].mm);
    if (i < dispTimes - 1) lcd.print(" ");
    if (i == 2) lcd.setCursor(0,2);
  }
}

//Get next dispense time indice 
int nextDispense () {
  DateTime now = rtc.now();
  for (int i = 0; i < dispTimes; i++) {
   if(dispTime[i].hh > now.hour() || (dispTime[i].hh == now.hour() && dispTime[i].mm > now.minute())) {
      return i;
   }
  }
  //if no dispense time is bigger lock until midnight and return first one from next day
  lock = true;
  return 0;
}

//Print time format to LCD
void timeDisplay(int hour, int minute){  
  if(hour < 10)
    lcd.print('0');
  lcd.print(hour,DEC);
  lcd.print(":");
  if(minute < 10)
    lcd.print('0');
  lcd.print(minute,DEC); 
}

//Rotate stepper one particion
void dispense(int times) {
    stepper.step((stepsPerRevolution/numberSegments)*times);
}

//Get button input and debouce
bool buttonPress(int buttonPinToPress) {
  int reading = digitalRead(buttonPin[buttonPinToPress]);

  if (reading != lastButtonState[buttonPinToPress]) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState[buttonPinToPress]) {
      buttonState[buttonPinToPress] = reading;

      if (buttonState[buttonPinToPress] == HIGH) {
        lastButtonState[buttonPinToPress] = reading;
        
        return true;
      }
    }
  }

  lastButtonState[buttonPinToPress] = reading;
  return false;
}
