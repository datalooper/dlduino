


/* Simple Teensy DIY USB-MIDI controller.
  Created by Liam Lacey, based on the Teensy USB-MIDI Buttons example code.

   Contains 8 push buttons for sending MIDI messages,
   and a toggle switch for setting whether the buttons
   send note messages or CC messages.

   The toggle switch is connected to input pin 0,
   and the push buttons are connected to input pins 1 - 8.

   You must select MIDI from the "Tools > USB Type" menu for this code to compile.

   To change the name of the USB-MIDI device, edit the STR_PRODUCT define
   in the /Applications/Arduino.app/Contents/Java/hardware/teensy/avr/cores/usb_midi/usb_private.h
   file. You may need to clear your computers cache of MIDI devices for the name change to be applied.

   See https://www.pjrc.com/teensy/td_midi.html for the Teensy MIDI library documentation.

*/
#include <Bounce2.h>
#include <EEPROM.h>


//GLOBAL CONST
const int NUM_LOOPERS = 3;
const int LED_PINS = 3;
const int NUM_CONTROLS = 4;
const int CONFIG_ADDRESS = 0;
const int MASTER_STOP_NOTE_VAL = 127;
//button debounce time
const int DEBOUNCE_TIME = 50;
const int DOUBLE_HIT_TIME = 1500;
// the MIDI channel number to send messages
const int MIDI_CHAN = 14;
//DOWNBEAT BLINK TIME
const int BLINK_TIME = 50;
//STATE CONST
const int STOPPED = 0;
const int RECORDING = 1;
const int PLAYING = 2;
const int OVERDUBBING = 3;
const int CLEAR = 4;
//LOOPER COMMAND
const int RESET = 0;
const int CHANGE_STATE = 1;
const int DOWNBEAT = 2;
//SYSEX STUFF
int looperNum = 0;
int looperCommand = 0;
int instance;

boolean configActive = false;
boolean onBeat = false;
unsigned long beat_time = 0;     

//LED STUFF
const int RED = 0;
const int GREEN = 1;
const int BLUE = 2;
const int YELLOW = 3;
const int WHT = 4;
const int NONE = 5;
const int PURPLE = 6;

class Led
{
  public:
    int *pins;
    int curColor = WHT;

    Led(int ledPins[]) {
      pins = ledPins;
      for(int n = 0; n < LED_PINS; n++){
        pinMode(pins[n], OUTPUT);
      }
    }
    void setColor(int color) {
      if (color == GREEN) {
        analogWrite(pins[RED], 255);
        analogWrite(pins[GREEN], 100);
        analogWrite(pins[BLUE], 255);
      } else if (color == RED) {
        analogWrite(pins[RED], 100);
        analogWrite(pins[GREEN], 255);
        analogWrite(pins[BLUE], 255);
      } else if (color == BLUE) {
        analogWrite(pins[RED], 255);
        analogWrite(pins[GREEN], 255);
        analogWrite(pins[BLUE], 100);
      } else if (color == YELLOW) {
        analogWrite(pins[RED], 100);
        analogWrite(pins[GREEN], 100);
        analogWrite(pins[BLUE], 255);
      } else if (color == WHT) {
        analogWrite(pins[RED], 100);
        analogWrite(pins[GREEN], 100);
        analogWrite(pins[BLUE], 100);
      } else if (color == NONE) {
        analogWrite(pins[RED], 255);
        analogWrite(pins[GREEN], 255);
        analogWrite(pins[BLUE], 255);
      } else if (color == PURPLE) {
        analogWrite(pins[RED], 100);
        analogWrite(pins[GREEN], 255);
        analogWrite(pins[BLUE], 100);
      } 
    }
    void restoreColor() {
      setColor(curColor);
    }
};

class Looper
{
  public:
    Led *led;
    int *controls;
    int ccs[NUM_CONTROLS];
    int state;
    boolean isClear = true;
    Bounce buttons[NUM_CONTROLS] = {
      Bounce (),
      Bounce (),
      Bounce (),
      Bounce ()
    };
    Looper(int ledPins[], int controlPins[], int looperNum, int instance) {
      led = new Led(ledPins);
      controls = controlPins;
      configureLooper(looperNum, instance);
    }

    void configureLooper(int looperNum, int instance){
    for(int n = 0; n < NUM_CONTROLS; n++){
        ccs[n] = (instance * NUM_LOOPERS * NUM_CONTROLS) + (looperNum * NUM_CONTROLS ) + n + 1;
        pinMode(controls[n], INPUT_PULLUP);
        buttons[n].attach(controls[n]);
        buttons[n].interval(DEBOUNCE_TIME);
      }
   }
};


//PIN CONFIG
int led1[LED_PINS] = {3, 4, 6};
int loop1controls[NUM_CONTROLS] = {0, 1, 2, 5};

int led2[LED_PINS] = {9, 10, 16};
int loop2controls[NUM_CONTROLS] = {7, 8, 11, 12};

int led3[LED_PINS] = {17, 20, 22};
int loop3controls[NUM_CONTROLS] = {13, 14, 15, 18};

Looper *loopers[NUM_LOOPERS];


void setup()
{
  // Configure the pins for input mode with pullup resistors.
  // The buttons/switch connect from each pin to ground.  When
  // the button is pressed/on, the pin reads LOW because the button
  // shorts it to ground.  When released/off, the pin reads HIGH
  // because the pullup resistor connects to +5 volts inside
  // the chip.  LOW for "on", and HIGH for "off" may seem
  // backwards, but using the on-chip pullup resistors is very
  // convenient.  The scheme is called "active low", and it's
  // very commonly used in electronics... so much that the chip
  // has built-in pullup resistors!

  Serial.begin(9600); // USB is always 12 Mbit/sec
  
  instance = EEPROM.read(CONFIG_ADDRESS);
  if(instance == 255){
    EEPROM.write(CONFIG_ADDRESS, 0);
  }

  loopers[0] = new Looper(led1, loop1controls, 0, instance);
  loopers[1] = new Looper(led2, loop2controls, 1, instance);
  loopers[2] = new Looper(led3, loop3controls, 2, instance);
  usbMIDI.setHandleSystemExclusive(onSysEx);
}

void loop()
{
  
  unsigned long current_time = millis();

  if(onBeat){
    beat_time = current_time;
    onBeat = false;
  }
  if (current_time - beat_time >= BLINK_TIME && !configActive) {
    for (int i = 0; i < NUM_LOOPERS; i++)
    {
      loopers[i]->led->restoreColor();
    }
  } else if(!configActive) {
    for (int i = 0; i < NUM_LOOPERS; i++)
    {
      loopers[i]->led->setColor(NONE);
    }
  }

  //==============================================================================
  // Update all the buttons/switch. There should not be any long
  // delays in loop(), so this runs repetitively at a rate
  // faster than the buttons could be pressed and released.

  for (int i = 0; i < NUM_LOOPERS; i++)
  {
    for (int n = 0; n < NUM_CONTROLS; n++) {
      loopers[i]->buttons[n].update();

      if ((i == NUM_LOOPERS-1 && loopers[i]->buttons[n].fallingEdge()) || ( i != NUM_LOOPERS-1 && loopers[i]->buttons[n].risingEdge()))
      {
        onButtonPress(i, n);

        if(i == 0 && n == 3){
          configActive = true;
        }

        if(configActive == true && i == NUM_LOOPERS - 1){
          Serial.print("configuring looper as: ");
          Serial.print(n);
          EEPROM.write(CONFIG_ADDRESS, n);
          for (int l = 0; l < NUM_LOOPERS; l++){
            loopers[l]->led->setColor(PURPLE);
            loopers[l] -> configureLooper(l, n);
          }
        }
      }
      else if ((i == NUM_LOOPERS-1 && loopers[i]->buttons[n].risingEdge()) || ( i != NUM_LOOPERS-1 && loopers[i]->buttons[n].fallingEdge()))
      {
        usbMIDI.sendNoteOff (loopers[i]->ccs[n], 0, MIDI_CHAN);

        if(i == 0 && n == 3){
          configActive = false;
        }
      }
    }
  }


  //Buttons accidentely backwards on loop 3 in v1.1; here's the software fix; oops.
  


  
  while (usbMIDI.read())
  {
  
  }

}

unsigned long transport_stop_double_hit_timer = 0;
unsigned long loop_stop_double_hit_timer = 0;

boolean checkMasterStop(int curLooper, int curControl, int looperNum, int controlNum, unsigned long &counter){
  if (curLooper == looperNum && curControl == controlNum && counter == 0) {
    counter = millis();
    return false;
  } else if (curLooper == looperNum && curControl == controlNum && (millis() - counter < DOUBLE_HIT_TIME)) {
    Serial.println();
    Serial.print("curLooper=");
    Serial.print(curLooper);
    counter = 0;
    return true;
  } else if (counter != 0 && (millis() - counter >= 1000)) {
    counter = 0;
    return false;
  }
}
void onButtonPress(int i, int n){
  //diagnoseButton(i,n, loopers[i]->ccs[n]);
  if (n == 3 && loopers[i]->state != OVERDUBBING) {
    loopers[i]->led->curColor = WHT;
    loopers[i]->isClear = true;
  }
  if(checkMasterStop(i,n, 0, 1, transport_stop_double_hit_timer)){
    Serial.println();
    Serial.print(i);
    Serial.print(" ");
    Serial.print(n);
    Serial.print(" stopping all");
    loopStop();
    transportStop();
  } 
  else if(checkMasterStop(i,n, 1, 1, loop_stop_double_hit_timer)){
    Serial.print("stopping loops");
    loopStop();
  } 
  else{
    usbMIDI.sendNoteOn (loopers[i]->ccs[n], 127, MIDI_CHAN);
  }

}
void diagnoseButton(int i, int n, int num){
  Serial.print("looper:");
  Serial.print( i );
  Serial.println();
  Serial.print("button: ");
  Serial.print(n);
  Serial.println();
  Serial.print("cc#: ");
  Serial.print(num);
  Serial.println();
}

void onSysEx(byte* sysExData, unsigned int sysExSize)
{
  //Translate SYSEX to variable data
  looperNum = sysExData[5];
  looperCommand = sysExData[4];
  int looperAddress = looperNum % 3;

  if (looperCommand == DOWNBEAT) {
    onBeat = true;
  }
  else if (looperCommand == CHANGE_STATE && (looperNum >= instance * 3 && looperNum <= (instance*3) + 2)) {

    loopers[looperAddress]->state = sysExData[6];
    if (sysExData[6] == STOPPED && !loopers[looperAddress]->isClear) {
      loopers[looperAddress]->led->curColor = BLUE;
    } else if (sysExData[6] == STOPPED && loopers[looperAddress]->isClear) {
      loopers[looperAddress]->led->curColor = WHT;
    } else if (sysExData[6] == PLAYING) {
      loopers[looperAddress]->led->curColor = GREEN;
    } else if (sysExData[6] == RECORDING) {
      loopers[looperAddress]->led->curColor = RED;
      loopers[looperAddress]->isClear = false;
    } else if (sysExData[6] == OVERDUBBING) {
      loopers[looperAddress]->led->curColor = YELLOW;
    } else if (sysExData[6] == CLEAR) {
      loopers[looperAddress]->led->curColor = WHT;
    }
  }
}

void loopStop() {
  for (int i = 2; i < 127; i+=4) {
    usbMIDI.sendNoteOn (i, 127, MIDI_CHAN);
    usbMIDI.sendNoteOff (i, 0, MIDI_CHAN);
  }
}
void transportStop(){
  usbMIDI.sendNoteOn (MASTER_STOP_NOTE_VAL, 127, MIDI_CHAN);
  usbMIDI.sendNoteOff (MASTER_STOP_NOTE_VAL, 0, MIDI_CHAN);
}


void master_clear() {

  Serial.print("MASTER CLEAR");
  //usbMIDI.sendNoteOn (MIDI_CC_NUMS[LOOP_2_CLEAR], 127, MIDI_CHAN);
  //usbMIDI.sendNoteOff (MIDI_CC_NUMS[LOOP_2_CLEAR], 0, MIDI_CHAN);
  //usbMIDI.sendNoteOn (MIDI_CC_NUMS[LOOP_1_CLEAR], 127, MIDI_CHAN);
  //usbMIDI.sendNoteOff (MIDI_CC_NUMS[LOOP_1_CLEAR], 0, MIDI_CHAN);
  //loopclear[0] = true;
  //loopclear[1] = true;
  //loopclear[2] = true;
}








