#include <EEPROM.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RotaryEncoder.h>

#define ROW 4
#define COL 20
LiquidCrystal_I2C lcd(0x27, COL, ROW);  

#define CLK 3    
#define DT 2  
#define SW 4
RotaryEncoder encoder(CLK, DT);

// Definizione delle uscite
const byte OutLED  = 13;

bool Enable = true;
bool Ready = true;
uint16_t Time1 = 2500; 
uint16_t Time2 = 5000;
float Setpoint = 150.0F;
float Kp = 4.50F;
float Ki = 0.10F;
float Kd = 0.80F;
#define NUM_VAR 8
#define LONGCLICK 1000

/* Salviamo nell'array gli indirizzi di tutte le variabili da mostrare nel menu. 
 * In questo modo possiamo costruire le pagine del menu dinamicamente, iterando l'array in un ciclo for */
uint16_t *VarPtrs[NUM_VAR] = {(uint16_t*)&Enable, (uint16_t*)&Ready, &Time1, &Time2, 
         (uint16_t*)&Setpoint, (uint16_t*)&Kp,(uint16_t*)&Ki, (uint16_t*)&Kd};       

/* Per l'utilizzo corretto, è necessario sapere che tipo di variabili sono (l'ordine deve essere lo stesso) 
 * Siccome l'array contenente gli indirizzi dell variabili è stato dichiarato di unsigned int, 
 * per usare gli altri tipi di variabili è necessario fare un casting dei puntori salvati */
bool* b_ptr; float* f_ptr;      // puntatori ai tipi di variabili utilizzati oltre uint16_t (bool e float)
enum types { BOOL = 0, UINT = 1, FLOAT = 3};
types  VarType[] = {BOOL, BOOL, UINT, UINT, FLOAT, FLOAT, FLOAT, FLOAT};

         
/* Memorizziamo le stringhe per le etichette delle variabili da mostrare nel menu.
 * Anche se complica un po' la procedura di recupero delle stringhe dalla memoria flash,
 * usiamo la keyword PROGMEM per non sprecare la preziosa RAM inutilmente.
 * (rif. https://www.arduino.cc/reference/en/language/variables/utilities/progmem/) */
const char L0[] PROGMEM = "Enable:     "; 
const char L1[] PROGMEM = "Ready:      ";
const char L2[] PROGMEM = "Time 1:     ";
const char L3[] PROGMEM = "Time 2:     ";
const char L4[] PROGMEM = "Setpoint:   ";
const char L5[] PROGMEM = "Constant kp:";
const char L6[] PROGMEM = "Constant ki:";
const char L7[] PROGMEM = "Constant kd:";
// Array che ci consentirà  di recuperare nel ciclo "for" le stringhe corrette.
const char* const VarLabels[] PROGMEM = {L0, L1, L2, L3, L4, L5, L6, L7};

/////////////////////////////////////////////////////////////////////////////
// Variabili usate per "navigare" all'interno del menu di setup
int varSel = 0, oldVarSel = -1;
char lcdBuffer[COL+1];

// Altre variabili
bool SetupMode = false;
uint16_t encPos = 0;
uint32_t pressTime = millis();


/////////////////////////// Ciclo di Setup //////////////////////////////////
void setup(){        
  Serial.begin(9600);   
  Serial.println();
   
  // etc etc
  pinMode(SW, INPUT_PULLUP);  
  pinMode(DT, INPUT_PULLUP); 
  pinMode(CLK, INPUT_PULLUP);

  /* Inizializzazione del display LCD */
  lcd.init();
  lcd.backlight();
  lcd.clear();  
  /* Siccome l'encoder è collegato sui pin 2 e 3, possiamo usare gli "external interrupt",
     ma è possibile anche usare il "port changhe interrupt" sugli altri piedini oppure
     la tecnica del polling (senza interrupt) perÃ² con variazioni delle variabili molto lente.
     Fare riferimento agli esempi inclusi nella libreria "RotaryEncoder".     
   */
  attachInterrupt(digitalPinToInterrupt(DT), encoder_isr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(CLK), encoder_isr, CHANGE);
  /* Elimina il commento alla riga alla prima esecuzione per caricare i valori dalla EEPROM */
  //readEeprom();    
}


/////////////////////////// Ciclo Principale ////////////////////////////////
void loop(){     
  // Gestione pulsante
  pressTime = millis();
  checkButton();
  
  if(SetupMode){     
    /* L'encoder rotativo è stato usato in fase di programmazione? modifichiamo la variabile */    
    EditVars();  
    MenuSetup();    
  } 
  else {
    // tutto il resto del codice
    ////////////////////////////
    lcd.noBlink(); 
    lcd.setCursor(5, 1); 
    lcd.print(F("TEST MENU"));     
    delay(100);
    ///////////////////////////        
  }         
}


// Interrupt Service Routine per gestione encoder 
void encoder_isr(void){
  encoder.tick();
}

// Controlliamo se il pulsante è stato premuto brevemente o più a lungo
void checkButton(void){
  if(digitalRead(SW) == LOW){
    delay(50);    
    bool xLong = false;
    while(digitalRead(SW) == LOW){
      if(millis() - pressTime > LONGCLICK) {  
        xLong = true;    
        break;
      }          
    }
    xLong ? LongClick() : SingleClick();      
  }  
}

///////////////// Azioni da eseguire alla pressione dei pulsanti  ///////////////////////
void LongClick(){   
  SetupMode = !SetupMode;      
  Serial.print(F("Modo Programmazione: ")); 
  Serial.println(SetupMode ? "true":"false");         
  if(SetupMode){   
    oldVarSel = -1; 
    lcd.clear();  
    lcd.setCursor(5, 1); 
    lcd.print(F("MENU SETUP"));  
    delay(1000);
  }  
  else {
    Serial.println(F("Salvataggio parametri"));
    lcd.clear();
    lcd.noBlink(); 
    lcd.setCursor(2, 1); 
    lcd.print(F("SALVO PARAMETRI")); 
    delay(2000);
    ProgrammazioneFinita();     
  }
}

void SingleClick(){
  if(SetupMode){    
    varSel = ++varSel % NUM_VAR;       
  }
}

// Fine della programmazione. Procediamo a memorizzare i nuovi valori
void ProgrammazioneFinita(){             
  writeEeprom();  
  lcd.clear();   
}


/////////////////    GESTIONE MENU E SALVATAGGIO EEPROM    ///////////////////////
// Funzione che mostra sul display le variabili utente modificabili nel menu setup
void MenuSetup(){ 
  if(varSel != oldVarSel){  
    oldVarSel = varSel;  
    lcd.clear();
    uint8_t page = floor(varSel/ROW);    // Cambio pagina in funzione della variabile selezionata        
    for(uint8_t row=0; row<ROW; row++){         
      uint8_t varNum = page*ROW + row ;    // Selezione della variabile da mostrare nel menu           
      /* Rif. https://www.arduino.cc/reference/en/language/variables/utilities/progmem/   */
      char label_buf[COL];                 // Buffer che conterrÃ  l'etichetta della variabile
      strcpy_P(label_buf, (char*)pgm_read_word(&(VarLabels[varNum])));        
      switch(VarType[varNum]){
        case UINT:          
          sprintf(lcdBuffer,  "%s  %05d", label_buf, *VarPtrs[varNum]);            
          break; 
        case BOOL:                 
          b_ptr =  (bool*)VarPtrs[varNum];  // Cast del puntatore a bool
          sprintf(lcdBuffer, "%s  %s", label_buf, *b_ptr ? "true":"false");                    
          break;    
        case FLOAT:            
          f_ptr = (float*)VarPtrs[varNum];  // Cast del puntatore a float
          sprintf(lcdBuffer, "%s  %02u.%02u", label_buf, (uint16_t)*f_ptr, (uint16_t)(*f_ptr*100)%100 );                  
          break;
      }
      lcd.setCursor(0, row);   
      lcd.print(lcdBuffer);        
    }   
   lcd.setCursor(0, varSel%ROW);  
   lcd.blink();       
  }
}

void EditVars(){  
  encPos = encoder.getPosition();  
  if(encPos != 0){         
    // Variazione dei parametri. La variabile varSel determina quale parametro viene modificato
    switch(VarType[varSel]){
      case UINT:
        *VarPtrs[varSel] = *VarPtrs[varSel] + (uint16_t)encPos;                 
        break; 
      case BOOL:         
        b_ptr =  (bool*)VarPtrs[varSel];  // Cast del puntatore a bool
        *b_ptr = ! *b_ptr;                  
        break;    
      case FLOAT:      
        f_ptr = (float*)VarPtrs[varSel];  // Cast del puntatore a float
        *f_ptr = *f_ptr + (float)encPos*0.01;                 
        break;
    }       
    encoder.setPosition(0);     
    oldVarSel = -1;             // Forza aggiornameno display LCD             
  }    
}

void readEeprom(){
  uint8_t eeAddress = 0;
  for(uint8_t i=0; i<NUM_VAR; i++){    
    switch(VarType[i]){
      case UINT:          
        EEPROM.get(eeAddress, *VarPtrs[i]);
        eeAddress += sizeof(uint16_t); 
        break; 
      case BOOL:          
        b_ptr =  (bool*)VarPtrs[i];  // Cast del puntatore a bool
        EEPROM.get(eeAddress, *b_ptr);        
        eeAddress += sizeof(bool); 
        break;    
      case FLOAT:         
        f_ptr = (float*)VarPtrs[i];  // Cast del puntatore a float
        EEPROM.get(eeAddress, *f_ptr);  
        eeAddress += sizeof(float);
        break; 
    }   
  } 
}

void writeEeprom(){
  uint8_t eeAddress = 0;  
  for(uint8_t i=0; i<NUM_VAR; i++){  
    switch(VarType[i]){
      case UINT:          
        EEPROM.put(eeAddress, *VarPtrs[i]);
        eeAddress+=sizeof(uint16_t); 
        break; 
      case BOOL:          
        b_ptr =  (bool*)VarPtrs[i];  // Cast del puntatore a bool        
        EEPROM.put(eeAddress, *b_ptr);        
        eeAddress+=sizeof(bool); 
        break;    
      case FLOAT:         
        f_ptr = (float*)VarPtrs[i];  // Cast del puntatore a float
        EEPROM.put(eeAddress, *f_ptr);  
        eeAddress+=sizeof(float); 
        break; 
    }
  } 
}



/*
Buongiorno! In questa mattinata di ozio, ho buttato giÃ¹ uno sketch che avevo in mente giÃ  da un po'. 
Mi capita spesso, come a tutti noi del resto, di avere un progetto con un display ed un numero indefinito di variabili modificabili dall'utente finale. So che ci sono delle librerie che consentono di creare dei menu di configurazione, ma non mi hanno mai convinto del tutto e non sono flessibili come vorrei (quasi tutte infatti prevedono l'utilizzo di 4/5 pulsanti di navigazione).
Ho scritto allora uno sketch, diciamo di esempio, che posso di volta in volta modificare rapidamente mantenendo la struttura base per adeguarlo alle esigenze del momento. 
Le variabili da modificare vengono presentate nella forma di lista etichetta:valore. 

Le "pagine" sono generate dinamicamente e la navigazione del menu e/o modifica dei valori si fa con l'uso di un comune rotary encoder da pannello. Potrebbe perÃ² essere facilmente modificato per usare dei classici pulsanti (3, o eventualmente 2).
Per gestire il rotary encoder ho deciso, dopo averla testata a fondo, di usare la libreria "RotaryEncoder".

*/
