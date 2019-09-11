/*
Created by H3ct1cL3o.
Arduino based program using HX711 with 2kg load cell to turn a grinder on until desired weight is reached.
Set weight is adjustable through rotary encoder input, calibration of load cell. Manual grinding is also possible through push button.
Created 11/9/19
*/
#include <Arduino.h>
#include <Adafruit_ST7735.h>
#include <Adafruit_GFX.h>
#include <SPI.h>
#include <EEPROMex.h>
#include <ClickEncoder.h>
#include <TimerOne.h>
#include <HX711.h>

//------------------------------------------------------------------LCD------------------------------------------------------------------//
#define TFT_DC A0
#define TFT_CS A1
#define TFT_RST A2

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

//------------------------------------------------------------------DEBUG------------------------------------------------------------------//
#define DEBUG                                     //If you comment this line, the DPRINT & DPRINTLN lines are defined as blank.
#ifdef DEBUG                                      //Macros are usually in all capital letters.
#define DBEGIN(...) Serial.begin(__VA_ARGS__)     //DBEGIN serial monitor
#define DPRINT(...) Serial.print(__VA_ARGS__)     //DPRINT is a macro, debug print
#define DPRINTLN(...) Serial.println(__VA_ARGS__) //DPRINTLN is a macro, debug print with new line
#else
#define DBEGIN(...)   //now defines a blank line
#define DPRINT(...)   //now defines a blank line
#define DPRINTLN(...) //now defines a blank line
#endif
//------------------------------------------------------------------ENCODER------------------------------------------------------------------//
ClickEncoder *encoder;
int16_t last, value;
ClickEncoder::Button encBtn;
void timerIsr()
{
  encoder->service();
}
//------------------------------------------------------------------LOADCELL------------------------------------------------------------------//

#define RESOLUTION 2 //decimal places of HX711 reading
#define AVG_FACT 10  //averagin factor of HX711 reading
const int loadCellDT = A3;
const int loadCellSCK = A4;
HX711 scale; // parameter "gain" is ommited; the default value 128 is used by the library
float scaleLoad;
float constrainedScaleLoad;

int cal1 = 0;
float eeCal1;

//------------------------------------------------------------------LOADCELL CALIBRATION------------------------------------------------------------------//

//#define EEP_CAL_FLAG_ADDR 9  //eeprom calibration flag address, flag occupies 1 byte
//#define EEP_CAL_DATA_ADDR 10 //eeprom calibration data address; data occupies 4 bytes

byte eepCalFlag = 0;
long calibrationFactor_l = 1030; //initial guess (for 0-2kg cell)

//------------------------------------------------------------------GRINDER SETUP------------------------------------------------------------------//

volatile float dose;
volatile float set;

const int manualBtn = 3; //Manually operate grinder through pushbutton
int manualBtnState = 0;
const int buttonPin = 2; // the number of the pushbutton pin (this will detect when the hadle is ready to be loaded with coffee)
int buttonState = 0;     // variable for reading the pushbutton status

//------------------------------------------------------------------MENUS------------------------------------------------------------------//
void mainScreen()
{
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setCursor(15, 2);
  tft.setTextColor(ST77XX_RED);
  tft.println("Weigh Grind");
  tft.setCursor(2, 30);
  tft.setTextColor(ST77XX_RED);
  tft.println("Dose");
  tft.setCursor(80, 30);
  tft.setTextColor(ST77XX_GREEN);
  tft.println("Set");
  tft.setTextColor(ST77XX_BLUE);
  tft.setCursor(2, 50);
  tft.print(dose, 1);
  tft.println("g");
  tft.setCursor(80, 50);
  tft.print(set, 1);
  tft.println("g");
  tft.setCursor(2, 80);
  tft.setTextColor(ST7735_CYAN);
  tft.println("Weight");
}

void settings()
{
  tft.setTextColor(ST77XX_BLUE);
  tft.setCursor(2, 50);
  tft.print(dose, 1);
  tft.println("g");
  tft.setCursor(80, 50);
  tft.print(set, 1);
  tft.println("g");
  tft.print("                   ");
}
void adjustment()
{
  unsigned long timer = 0;
  unsigned long lastTimer = 100;
  volatile float newDose = dose;
  while (timer <= lastTimer)
  {
    timer++;
    value += encoder->getValue();
    if (value != last)
    {
      last = value;
      newDose = (dose + value * 0.1);
      DPRINT("New Dose: ");
      DPRINTLN(newDose, 1);
      timer = 0;
    }
    encBtn = encoder->getButton();

    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.setCursor(2, 50);
    tft.print(newDose, 1);
    tft.println("g");
    tft.setTextSize(1);
    tft.print("Adjusting");
    tft.setTextSize(2);
    DPRINTLN(timer);
    if (encBtn == ClickEncoder::Clicked)
    {
      dose = newDose;
      set = dose;
      DPRINT("New Dose set: ");
      DPRINTLN(dose, 1);
      mainScreen();
      settings();
      DPRINTLN("Adjustment Saved");
      eeprom_update_float(0, set);
      DPRINTLN("EEPROM SAVED");
      delay(500);
      timer = lastTimer;
      return;
    }
  }
  {
    dose = set;
    mainScreen();
    settings();
  }
}

void calibration()
{
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setCursor(15, 2);
  tft.setTextColor(ST77XX_RED);
  tft.println("CALIBRATION");

  DPRINTLN("Calibration mode");
  DPRINTLN("Clear scales");
  DPRINTLN("After readings begin, place known weight on scale");
  delay(500);
  //initialize calibration
  eepCalFlag = EEPROM.read(9);
  //get cal value from eeprom if exists
  if (eepCalFlag == 128)
  {
    EEPROM.read(10);
    EEPROM.readLong(calibrationFactor_l);
  }
  float calibrationFactor_f = calibrationFactor_l;
  float newCal = calibrationFactor_f;

  scale.set_scale();                       //reset scale to default (0)
  scale.tare();                            //reset the scale to 0
  long zero_factor = scale.read_average(); //Get a baseline reading

  DPRINT("Zero factor: "); //This can be used to remove the need to tare the scale. Useful in permanent scale projects.
  DPRINTLN(zero_factor);
  tft.setCursor(2, 30);
  tft.setTextColor(ST77XX_RED);
  tft.println("grams");
  tft.setCursor(80, 30);
  tft.setTextColor(ST77XX_GREEN);
  tft.println("raw");
  tft.setCursor(2, 80);
  tft.setTextColor(ST7735_CYAN);
  tft.println("cal factor");
  tft.setTextColor(ST77XX_WHITE, ST7735_BLACK);

  boolean calibrateFlag = true;
  while (calibrateFlag)
  {
    scale.set_scale(calibrationFactor_f); //Adjust to this calibration factor
    DPRINT("Reading (grams, raw, cal factor): ");
    DPRINT(scale.get_units(4), 3);
    DPRINT(" , ");

    tft.setTextSize(2);
    tft.setCursor(2, 50);
    tft.print(scale.get_units(4), 3);

    DPRINT(scale.read_average());
    DPRINT(" , ");

    tft.setTextSize(1);
    tft.setCursor(80, 50);
    tft.print(scale.read_average(), 1);

    DPRINT(calibrationFactor_f);
    DPRINTLN();

    tft.setTextSize(2);
    tft.setCursor(2, 98);
    tft.print(calibrationFactor_f);

    value += encoder->getValue();
    if (value != last)
    {
      last = value;
      calibrationFactor_f = (newCal + value);
    }
    encBtn = encoder->getButton();

    if (encBtn == ClickEncoder::Clicked)
    {
      calibrateFlag = false;
    }
  }
  {
    eepCalFlag = 128;
    EEPROM.write(9, eepCalFlag);
    DPRINTLN(EEPROM.read(9));
    calibrationFactor_l = calibrationFactor_f;
    EEPROM.updateLong(10, calibrationFactor_l);
    DPRINTLN(EEPROM.readLong(10));
    Serial.println("Exiting calibration sequence");
    EEPROM.updateFloat(64, calibrationFactor_l);
  }
}

//------------------------------------------------------------------GRINDER RUN OPERATION------------------------------------------------------------------//
void grinding()
{
  buttonState = digitalRead(buttonPin); // read the state of the pushbutton value:
  while (buttonState == HIGH)
  {
    if (scaleLoad <= set)
    { // check if the pushbutton is pressed. If it is, the buttonState is HIGH:
      scale.tare();
      DPRINT("Waiting for scales to equalize");
      delay(1000);
      while (scaleLoad <= set)
      {
        scaleLoad = ((float)(int)(scale.get_units(10) * 10)) / 10;
        constrainedScaleLoad = constrain(scaleLoad, 0.0, 1000.0);
        tft.setCursor(2, 98);
        tft.setTextColor(ST7735_MAGENTA, ST7735_BLACK);
        tft.print((constrainedScaleLoad), 1);
        tft.print("g              ");
        // Simulate solid state relay output using blinking led for test purpose
        digitalWrite(LED_BUILTIN, HIGH);
        delay(50);
        digitalWrite(LED_BUILTIN, LOW);
        delay(50);
      }
    }
    else if (scaleLoad >= set)
    {
      while (buttonState == HIGH)
      {
        buttonState = digitalRead(buttonPin);
        DPRINT("OVER LOADED");
        scaleLoad = ((float)(int)(scale.get_units(10) * 10)) / 10;
        constrainedScaleLoad = constrain(scaleLoad, 0.0, 1000.0);
        // Simulate stopping solid state relay output turning off led for test purpose
        digitalWrite(LED_BUILTIN, LOW);
        tft.setCursor(2, 98);
        tft.setTextColor(ST7735_RED, ST7735_BLACK);
        tft.print((constrainedScaleLoad), 1);
        tft.print("g  ");
        tft.setCursor(80, 98);
        tft.setTextColor(ST7735_RED, ST7735_BLACK);
        tft.print("OVER!");
        delay(500);
      }
      return;
    }
    else if (scaleLoad == set)
    {
      DPRINT("DONE");
      digitalWrite(LED_BUILTIN, LOW);
      return;
    }
    else if (buttonState == LOW)
    {
      digitalWrite(LED_BUILTIN, LOW);
      return;
    }
  }
}

//------------------------------------------------------------------SETUP PROGRAM------------------------------------------------------------------//
void setup()
{
  DBEGIN(9600); //DEBUG to serial monitor
  //------------------------------------------------------------------GET EEPROM SETTINGS------------------------------------------------------------------//
  DPRINT("EEPROM DOSE: ");
  set = (eeprom_read_float(0));
  dose = set;
  DPRINTLN(set, true);

  DPRINT("EEPROM CAL: ");
  eeCal1 = (EEPROM.readFloat(64));
  DPRINTLN(eeCal1);

  delay(100);
  //------------------------------------------------------------------SCALES------------------------------------------------------------------//
  scale.begin(loadCellDT, loadCellSCK);
  delay(2500);
  scale.set_scale(eeCal1);
  delay(2500);
  scale.tare();

  pinMode(LED_BUILTIN, OUTPUT); // initialize the LED pin as an output:
  pinMode(buttonPin, INPUT);    // initialize the pushbutton pin as an input:
  //------------------------------------------------------------------ENCODER------------------------------------------------------------------//
  encoder = new ClickEncoder(5, 6, 7);
  Timer1.initialize(1000);
  Timer1.attachInterrupt(timerIsr);
  last = -1;

  //------------------------------------------------------------------LCD------------------------------------------------------------------//
  tft.initR(INITR_BLACKTAB); // Init ST7735S chip, black tab
  tft.fillScreen(ST77XX_BLACK);
  mainScreen();
  delay(100);
  DPRINTLN("Setup Initialized");
  delay(500);
}

//------------------------------------------------------------------LOOP------------------------------------------------------------------//
void loop()
{
  scaleLoad = ((float)(int)(scale.get_units(10) * 10)) / 10; // Displaying current weight on LCD
  constrainedScaleLoad = constrain(scaleLoad, 0.0, 1000.0);
  tft.setCursor(2, 98);
  tft.setTextColor(ST7735_MAGENTA, ST7735_BLACK);
  tft.print(constrainedScaleLoad, 1);
  tft.print("g              ");

  encBtn = encoder->getButton();
  if (encBtn != ClickEncoder::Open)
  {
    if (ClickEncoder::DoubleClicked)
    {
      cal1 = 1;
      calibration();
      return;
    }
    if (ClickEncoder::Clicked)
    {
      DPRINTLN("Entered Adjustment");
      adjustment();
      return;
    }
  }
  //------------------------------------------------------------------GRINDER RUN OPERATION------------------------------------------------------------------//
  buttonState = digitalRead(buttonPin); // read the state of the pushbutton value:
  if (buttonState == HIGH)              // check if the pushbutton is pressed. If it is, the buttonState is HIGH:
  {
    if (constrainedScaleLoad != 0.0)
    {
      scale.tare();
      delay(1000);
    }
    if (scaleLoad <= set)
    {
      grinding();
    }
    else
    {
      return;
    }
  }

  manualBtnState = digitalRead(manualBtn); // read the state of the pushbutton value:
  if (manualBtnState == HIGH)              // check if the pushbutton is pressed. If it is, the buttonState is HIGH:
  {
    digitalWrite(LED_BUILTIN, HIGH);
  }
  else
  {
    digitalWrite(LED_BUILTIN, LOW);
    return;
  }
}