/* DataLooper Firmware V1.0
  Experimental branch: 
  Loop 1 clear stops clock
  Loop 2 clear stops all loops
  Loop 3 clear mutes all loops while held down
*/
#include <Bounce2.h>
#include <EEPROM.h>


//GLOBAL CONST
const int NUM_LOOPERS = 3;
const int LED_PINS = 3;
const int NUM_CONTROLS = 4;
const int CONFIG_ADDRESS = 0;
const int MASTER_STOP_NOTE_VAL = 127;
const int MASTER_MUTE_CC_VAL = 126;

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
unsigned long configOffDelay = -1;
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

//PIN CONFIG
int led1[LED_PINS] = {3, 4, 6};
int loop1controls[NUM_CONTROLS] = {0, 1, 2, 5};

int led2[LED_PINS] = {9, 10, 16};
int loop2controls[NUM_CONTROLS] = {7, 8, 11, 12};

int led3[LED_PINS] = {17, 20, 22};
int loop3controls[NUM_CONTROLS] = {13, 14, 15, 18};

long master_stop_time = -1;
long loop_stop_time = -1;
long config_press_time = -1;


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
    void writeColor(int color){
      if (color == GREEN) {
        writeColor(255,100,255);
      } else if (color == RED) {
        writeColor(100,255,255);
      } else if (color == BLUE) {
        writeColor(255,255,100);
      } else if (color == YELLOW) {
        writeColor(100,100,255);
      } else if (color == WHT) {
        writeColor(100,100,100);
      } else if (color == NONE) {
        writeColor(255,255,255);
      } else if (color == PURPLE) {
        writeColor(100,255,100);
      } 
    }
    void setColor(int color) {
      curColor = color;
      writeColor(color);
    }
   void writeColor(int red, int green, int blue){
      analogWrite(pins[RED], red);
      analogWrite(pins[GREEN], green);
      analogWrite(pins[BLUE], blue);
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
    boolean is_pressed[NUM_CONTROLS] = {false, false, false, false};
    long press_time[NUM_CONTROLS];
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

Looper *loopers[NUM_LOOPERS];

void setup()
{
  Serial.begin(9600); // USB is always 12 Mbit/sec

  //Checks for stored config value
  instance = EEPROM.read(CONFIG_ADDRESS);
  if(instance == 255){
    EEPROM.write(CONFIG_ADDRESS, 0);
  }

  //initializes new loopers
  loopers[0] = new Looper(led1, loop1controls, 0, instance);
  loopers[1] = new Looper(led2, loop2controls, 1, instance);
  loopers[2] = new Looper(led3, loop3controls, 2, instance);

  //registers sysex handler
  usbMIDI.setHandleSystemExclusive(onSysEx);
}

void loop()
{
  
  unsigned long current_time = millis();
  
  if(onBeat){
    beat_time = current_time;
    onBeat = false;
  }
  
  if (current_time - beat_time >= BLINK_TIME) {
    for (int i = 0; i < NUM_LOOPERS; i++)
    {
      loopers[i]->led->restoreColor();
    }
  } else if(!configActive) {
    for (int i = 0; i < NUM_LOOPERS; i++)
    {
      loopers[i]->led->writeColor(NONE);
    }
  }

  if(configActive &&  configOffDelay != -1 && current_time - configOffDelay >= 2000){
        for( int x = 0; x < NUM_LOOPERS; x++){
          loopers[x]->led->setColor(WHT);
          configActive = false;
        }
    configOffDelay = -1;
  }
  //==============================================================================
  // Update all the buttons/switch. There should not be any long
  // delays in loop(), so this runs repetitively at a rate
  // faster than the buttons could be pressed and released.

  for (int i = 0; i < NUM_LOOPERS; i++)
  {
    for (int n = 0; n < NUM_CONTROLS; n++) {
      loopers[i]->buttons[n].update();

      //Buttons accidentely backwards on loop 3 in v1.1; here's the software fix; oops.
      if ((i == NUM_LOOPERS-1 && loopers[i]->buttons[n].fallingEdge()) || ( i != NUM_LOOPERS-1 && loopers[i]->buttons[n].risingEdge()))
      {
        //On press
        onButtonPress(i, n, current_time);
      }
      else if ((i == NUM_LOOPERS-1 && loopers[i]->buttons[n].risingEdge()) || ( i != NUM_LOOPERS-1 && loopers[i]->buttons[n].fallingEdge()))
      {
        //On release
        if(n==1 && current_time - loopers[i]->press_time[n] < 1000){
          Serial.print("sending stop");
          if(loopers[i]->state == RECORDING){
            clearLoop(i);
          } else{
            usbMIDI.sendNoteOn (loopers[i]->ccs[n], 127, MIDI_CHAN);
          }
        }
       
        usbMIDI.sendNoteOff (loopers[i]->ccs[n], 0, MIDI_CHAN);
        loopers[i]->is_pressed[n] = false;
        loopers[i]->press_time[n] = -1;
      }
    }  
    
      if(loopers[i]->is_pressed[1] && current_time - loopers[i]->press_time[1] >= 1000){
          clearLoop(i);
          loopers[i]->is_pressed[1] = false;
      }
  }
  //Checks if looper 1 clear button held for 5 seconds
  if(loopers[0]->is_pressed[3] && current_time - loopers[0]->press_time[3] >= 5000 && !configActive){
    enterConfig();
  } else if(loopers[0]->is_pressed[3] && current_time - loopers[0]->press_time[3] >= 1000 && !configActive){
      clearLoops(false);
  }
       
  
  while (usbMIDI.read())
  {
  
  }

}

void onButtonPress(int i, int n, long current_time){
  //diagnoseButton(i,n, loopers[i]->ccs[n]);
  //Marks time that button was initially pressed
  if(!loopers[i]->is_pressed[n]){
      loopers[i]->press_time[n] = current_time;
      loopers[i]->is_pressed[n] = true; 
  }
  
  //default send note message 
  if(checkSpecialFeatures(current_time, i, n)){
    //Don't send undo until release, because 2 second hold sends out clear
    if( n != 1 && n != 3){  
      usbMIDI.sendNoteOn (loopers[i]->ccs[n], 127, MIDI_CHAN);
    }
   
  }

}

bool checkSpecialFeatures(long current_time, int looper, int controlNum){
  if(configActive){
    configureLooper(looper, controlNum);
    return false;
  } else{
    if(controlNum == 3){
      if(looper == 0){
        Serial.print("stop transport");
        loopStop();
        transportStop();
      } else if(looper == 1){
        Serial.print("stop loops");
        loopStop();
      } else if(looper == 2){
        Serial.print("mute tracks");
        usbMIDI.sendControlChange (MASTER_MUTE_CC_VAL, 127, MIDI_CHAN);        
      }
      return false;
    }
    return true;
  }
}
void clearLoops(boolean hardClear){
  Serial.print("clearing loops");
  for (int i = 0; i < NUM_LOOPERS; i++)
      {
        if(hardClear || !loopers[i]->isClear){
          clearLoop(i);
        }
      }
}
void clearLoop(int i){
      //Turns LED white if clear is pressed; no state listener for clearing Ableton Looper, so we just assume it will clear. Need to build custom plugin to fix this
    if (loopers[i]->state != OVERDUBBING) {
      Serial.print("sending clear");
      loopers[i]->led->curColor = WHT;
      loopers[i]->isClear = true;
      usbMIDI.sendNoteOn (loopers[i]->ccs[3], 127, MIDI_CHAN);
    } 
}
void enterConfig(){
  configActive = true;
  Serial.print("Entering config");
  for (int l = 0; l < NUM_LOOPERS; l++){
    loopers[l]->led->setColor(PURPLE);
  }
}
void configureLooper(int i,int n){
  instance = ( i * NUM_CONTROLS) + n;
  Serial.print("configuring looper as: ");
  Serial.print(instance);
  EEPROM.write(CONFIG_ADDRESS, instance);
  for( int x = 0; x < NUM_LOOPERS; x++){
    loopers[x]->configureLooper(x, instance);
    loopers[x]->led->setColor(GREEN);
    configOffDelay = millis();
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
  else if (looperCommand == RESET){
    clearLoops(true);
    for (int i = 0; i < NUM_LOOPERS; i++){
      loopers[i]->led->curColor = WHT; 
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










