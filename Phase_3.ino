#include <MozziGuts.h>
#include <Oscil.h>
#include <tables/sin2048_int8.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#define CONTROL_RATE 64
#define MAX_POLYPHONY 7  // A-G (7 notes)
#define DEBOUNCE_DELAY 50 // ms

Oscil<SIN2048_NUM_CELLS, AUDIO_RATE> oscillators[MAX_POLYPHONY];

// Note frequencies: A=C4, B=D4, C=E4, D=F4, E=G4, F=A5, G=B5
const float freqs[] = {
  261.63,  // A: C4
  293.66,  // B: D4
  329.63,  // C: E4
  349.23,  // D: F4
  392.00,  // E: G4
  880.00,  // F: A5
  987.77   // G: B5
};

// Track which notes are active
bool activeNotes[MAX_POLYPHONY] = {false};
unsigned long lastDebounceTime[MAX_POLYPHONY] = {0};

// 74HC165 Pins
#define LOAD_PIN 13
#define CE_PIN 12
#define CLOCK_PIN 27
#define DATA_PIN 32

QueueHandle_t noteQueue;

int updateAudio() {
  int mix = 0;
  byte activeCount = 0;
  
  for(int i = 0; i < MAX_POLYPHONY; i++) {
    if(activeNotes[i]) {
      mix += oscillators[i].next();
      activeCount++;
    }
  }
  return activeCount > 0 ? mix / max(1, activeCount/2 + 1) : 0;
}

void updateControl() {
  static byte lastState = 0xFF;
  byte currentState;
  
  if(xQueueReceive(noteQueue, &currentState, 0)) {
    byte reversedState = 0;
    for(int i = 0; i < 8; i++) {
      reversedState |= ((currentState >> i) & 1) << (7 - i);
    }
    
    unsigned long now = millis();
    
    Serial.print("Inputs: ");
    for(int i = 0; i < MAX_POLYPHONY; i++) {
      char inputName = 'A' + i;
      bool blocked = !(reversedState & (1 << (7 - i)));
      Serial.print(inputName);
      Serial.print("=");
      Serial.print(blocked ? "1" : "0");
      Serial.print(" ");
      
      bool wasBlocked = !(lastState & (1 << (7 - i)));
      
      if(blocked != wasBlocked) {
        lastDebounceTime[i] = now;
      }
      
      if(now - lastDebounceTime[i] > DEBOUNCE_DELAY) {
        if(blocked && !activeNotes[i]) {
          activeNotes[i] = true;
          oscillators[i].setFreq(freqs[i]);
          Serial.print("ON:");
          Serial.print(inputName);
          Serial.print(" ");
        } 
        else if(!blocked && activeNotes[i]) {
          activeNotes[i] = false;
          Serial.print("OFF:");
          Serial.print(inputName);
          Serial.print(" ");
        }
      }
    }
    Serial.println();
    lastState = reversedState;
  }
}

byte readShiftRegister() {
  digitalWrite(LOAD_PIN, LOW);
  delayMicroseconds(5);
  digitalWrite(LOAD_PIN, HIGH);
  
  digitalWrite(CE_PIN, LOW);
  byte data = 0;
  for(int i = 0; i < 8; i++) {
    digitalWrite(CLOCK_PIN, HIGH);
    delayMicroseconds(1);
    data |= (digitalRead(DATA_PIN) << i); 
    digitalWrite(CLOCK_PIN, LOW);
    delayMicroseconds(1);
  }
  digitalWrite(CE_PIN, HIGH);
  return data;
}

void pisoTask(void *pvParameters) {
  while(true) {
    byte data = readShiftRegister();
    xQueueOverwrite(noteQueue, &data);
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);
  
  // Initialize PISO pins
  pinMode(LOAD_PIN, OUTPUT);
  pinMode(CE_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(DATA_PIN, INPUT);
  
  digitalWrite(LOAD_PIN, HIGH);
  digitalWrite(CE_PIN, HIGH);
  digitalWrite(CLOCK_PIN, LOW);
  
  // Initialize oscillators
  for(int i = 0; i < MAX_POLYPHONY; i++) {
    oscillators[i].setTable(SIN2048_DATA);
  }
  
  noteQueue = xQueueCreate(1, sizeof(byte));
  xTaskCreatePinnedToCore(pisoTask, "PISO", 2048, NULL, 1, NULL, 0);
  
  startMozzi(CONTROL_RATE);
}

void loop() {
  audioHook();
}