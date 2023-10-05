
#include <Arduino.h>
#include <Preferences.h>

// pin allocations 

#define BTN D2
#define LED_BLUE D7
#define LED_RED D8
#define LED_GREEN D6
#define BELL D4
#define NONE 99;

// run modes
#define MODE_WAIT 0
#define MODE_DING 1
#define MODE_CONT 3
#define MODE_ALARM 9
#define MODE_PEND_CONT 2
#define MODE_PEND_ALARM 8



int mode = -1;

// BTN STATES
#define BTN_UP 0
#define BTN_DOWN 1


int BELL_ON = LOW;
int BELL_OFF = HIGH;

int settings_ding_time = -1;
int settings_ding_off_time = -1;
int settings_ding_pause_time = -1;
bool settings_invert_bell = false;

String actionString_DingDing = "";
String actionString_Cont = "";
String actionString = "";


int PRV_BTN_STATE = 0;
long old_millis = 0;
long down_time = 0;
long tot_dwn_time = 0;
bool action_mode_changed = false;
int seq_Step = -1;
long unsigned time_to_wait = 0;
bool doSequence = false;

Preferences prefs;



void ledColour(int val){
    digitalWrite(LED_BLUE, val == LED_BLUE ? HIGH:LOW );
    digitalWrite(LED_RED, val == LED_RED ? HIGH:LOW );
    digitalWrite(LED_GREEN, val == LED_GREEN ? HIGH:LOW );
}

void buildActionSequences(){

    String DingON = "";
    String DingOFF = "";
    String DingPAUSE = "";

    // Explanation 
    //2 = green
    //3 = Bell + green
    //4 = blue
    //5 = blue+bell
    //E = end Sequence 
    //R = repeat sequence

    //mask    BLUE  |  GREEN  |  BELL  |  DEC     
    //        0           0       0       0
    //        0           0       1       1
    //        0           1       0       2
    //        0           1       1       3
    //        1           0       0       4
    //        1           0       1       5       

    // ding ding =========================

    for ( int x = 1; x <= settings_ding_time ; x++){
        DingON += "3";
    }
    for (int x=1; x <= settings_ding_off_time; x++){
        DingOFF += "2";
    }
    actionString_DingDing = DingON+DingOFF+DingON + "E";

    Serial.print(" Ding Ding Action String [");
    Serial.print(actionString_DingDing);
    Serial.println("]");

    DingON = "";
    DingOFF = "";
    DingPAUSE = "";
    // Cont ==========================

    for ( int x = 1; x <= settings_ding_time ; x++){
        DingON += "5";
    }
    for (int x=1; x <= settings_ding_off_time; x++){
        DingOFF += "4";
    }
    for (int x=1; x <= settings_ding_pause_time; x++){
        DingPAUSE += "4";
    }
    actionString_Cont = DingON+DingOFF+DingON+DingPAUSE + "R";

    Serial.print(" CONT Action String [");
    Serial.print(actionString_Cont);
    Serial.println("]");

}


void setup(){

        Serial.begin(115200);
        while(!Serial){ }

        // get Prefernces 
        prefs.begin("TramDinger",false); // use "my-app" namespace
        bool settings_invert_bell = prefs.getBool("invertBell", false);

        settings_ding_time = prefs.getInt("dingTime", 1);
        settings_ding_off_time = prefs.getInt("dingOffTime", 2);
        settings_ding_pause_time = prefs.getInt("dingPauseTime", 7);

        if(settings_invert_bell){
            BELL_OFF = LOW;
            BELL_ON = HIGH;
        }

        //while(Serial.available() == 0) { }
        //Serial.println("hello world!");

        // pin modes 
        pinMode(BTN, INPUT_PULLUP);
        pinMode(LED_BLUE, OUTPUT);
        pinMode(LED_GREEN, OUTPUT);

        pinMode(LED_RED, OUTPUT);
        pinMode(BELL, OUTPUT);

        digitalWrite(LED_BLUE, LOW);
        digitalWrite(LED_GREEN, LOW);
        digitalWrite(LED_RED, LOW);
        digitalWrite(BELL, BELL_OFF);
       //Serial.println(millis());

        delay(1000);
        digitalWrite(LED_BLUE, HIGH);
        digitalWrite(LED_GREEN, HIGH);
        digitalWrite(LED_RED, HIGH);
        //Serial.println(millis());

        delay(1000);
        digitalWrite(LED_BLUE, LOW);
        digitalWrite(LED_GREEN, LOW);
        digitalWrite(LED_RED, LOW);
  
        buildActionSequences();

        delay(1000);

        // wait for btn press and release 
        bool startCheck = false;
        bool btn_down = false;

        while(!startCheck){
            // check btn state
            if(digitalRead(BTN)== LOW){
                btn_down=true;
            }
            if(btn_down && digitalRead(BTN == HIGH)){
                //released
                startCheck = true;
            }
            ledColour(LED_GREEN);
            delay(200);
            if(btn_down && digitalRead(BTN == HIGH)){
                //released
                startCheck = true;
            }
            ledColour(LED_BLUE);
            delay(200);
            if(btn_down && digitalRead(BTN == HIGH)){
                //released
                startCheck = true;
            }
            ledColour(LED_RED);
            delay(200);

        }

        digitalWrite(BELL, BELL_ON);
        delay(100);
        digitalWrite(BELL, BELL_OFF);


        mode = MODE_WAIT;
        ledColour(LED_GREEN);

}


void loop(){
    if(digitalRead(BTN) == LOW){
        if (PRV_BTN_STATE != BTN_DOWN){  // the first time
            //Serial.println("first Button down");
            old_millis = millis();
            PRV_BTN_STATE = BTN_DOWN;
        } else {
            // check to change LED colour 
            down_time = millis() - old_millis;
            //Serial.println(down_time);
            if (down_time > 1000 && down_time<3000){
                // change color to Blue
                ledColour(LED_BLUE);
                mode = MODE_PEND_CONT;
            } else if(down_time>3000){
                // ALARM
                // change led to RED 
                ledColour(LED_RED);
                mode = MODE_PEND_ALARM;
            }
        }
    } else {
        // BTN UP
        
        if(PRV_BTN_STATE == BTN_DOWN){
            Serial.println("Released");
            PRV_BTN_STATE = BTN_UP;
            // released
            tot_dwn_time = millis()-old_millis;
            //Serial.println(tot_dwn_time);
            if (tot_dwn_time < 1000 ){ // short press 
                Serial.print("Old Mode  3 = cont , 9 = alarm, pend alarm =8, pend cont = 2 ||  => ");
                Serial.println(mode);
                Serial.println("short Press");
                if (mode == MODE_CONT || mode == MODE_ALARM || mode == MODE_PEND_ALARM || mode == MODE_PEND_CONT){
                    
                    // reset mode 
                    mode = MODE_WAIT;
                    ledColour(LED_GREEN);
                    action_mode_changed = true;
                    doSequence=false;
                } else {
                    Serial.println("Do ding Ding");
                    mode = MODE_DING; // run ding ... ding (once)
                    action_mode_changed = true;
                    doSequence = true;
                }
            } else {  // longer than 1000
                if( mode == MODE_PEND_CONT){
                    // set cont 
                    mode = MODE_CONT;
                    ledColour(LED_BLUE);
                    action_mode_changed = true;
                    Serial.println("Set CONT");
                    doSequence = true;
                } else if( mode == MODE_PEND_ALARM){
                    // set alarm
                    mode = MODE_ALARM;
                    ledColour(LED_RED);
                    action_mode_changed = true;
                    Serial.println("Set ALARM");
                    doSequence=false;
                }
            }
        }        
    }
    // ACTION part
    //bool doSequence = false;

    if(action_mode_changed) {
        Serial.println("in doing action");
        switch (mode){
            case MODE_WAIT:
                action_mode_changed = false;
                ledColour(LED_GREEN);
                digitalWrite(BELL, BELL_OFF);
                doSequence = false;
                actionString = "";
                Serial.println("reverting to WAIT");
                break;
            case MODE_DING:
                action_mode_changed = false;
                seq_Step = 0;
                actionString = actionString_DingDing;
                doSequence = true;
                Serial.println("Performing Sequence Ding Ding");
                break;
            case MODE_CONT:
                action_mode_changed = false;
                seq_Step = 0;
                actionString = actionString_Cont;
                doSequence = true;
                Serial.println("Performing Sequence Cont");
                break;
            case MODE_ALARM:
                action_mode_changed = false;
                actionString = "";
                digitalWrite(BELL, BELL_ON);
                ledColour(LED_RED);
                doSequence = false;
                Serial.println("Setting ALARM");

                break;
        }
    } else if (doSequence){
        //Serial.println("doing Sequence");
        int actionStringSize = actionString.length();
        //Serial.print("Sequence String Length");
        //Serial.println(actionStringSize);
        if(seq_Step > actionStringSize-1){  // zero based char array
            // ERROR   - dump the error to the monitor and reset mode to wait 
            Serial.println("ERROR -------------- ");
            Serial.println("Action String size vs Step #");
            Serial.print(" Action String : [");
            Serial.print(actionString);
            Serial.println("]");
            Serial.print(" Action String Length: [");
            Serial.print(actionString.length());
            Serial.println("]");
            Serial.print(" Step #  : [");
            Serial.print(seq_Step);
            Serial.println("]");
            doSequence = false;
            mode = MODE_WAIT;
            action_mode_changed = true;
        } else {
            // wait until it is time to do next step 
            if(time_to_wait <= millis()){
                char thisStep = actionString[seq_Step];
                switch(thisStep){
                    case 'E':
                        Serial.println("Action Char = E : stopping Sequence");
                        doSequence = false;
                        actionString = "";
                        mode = MODE_WAIT;
                        action_mode_changed = true;
                        seq_Step = 0;
                        time_to_wait = 0;
                        digitalWrite(LED_BLUE, LOW);
                        digitalWrite(LED_GREEN, LOW);
                        digitalWrite(BELL, BELL_OFF);
                        digitalWrite(LED_RED, LOW);
                        break;
                    case 'R':
                        Serial.println("Action Char = R : re-Starting Sequence");
                        seq_Step = 0;
                        time_to_wait = 0;
                        break;
                    case '2':
                        Serial.print("2");
                        digitalWrite(LED_BLUE, LOW);
                        digitalWrite(LED_GREEN, HIGH);
                        digitalWrite(BELL, BELL_OFF);
                        digitalWrite(LED_RED, LOW);
                        seq_Step++;
                        time_to_wait = millis() + 100;
                        break;
                    case '3':
                        Serial.print("3");
                        digitalWrite(LED_BLUE, LOW);
                        digitalWrite(LED_GREEN, HIGH);
                        digitalWrite(BELL, BELL_ON);
                        digitalWrite(LED_RED, LOW);
                        seq_Step++;
                        time_to_wait = millis() + 100;
                        break;
                    case '4':
                        Serial.print("4");
                        digitalWrite(LED_BLUE, HIGH);
                        digitalWrite(LED_GREEN, LOW);
                        digitalWrite(BELL, BELL_OFF);
                        digitalWrite(LED_RED, LOW);
                        seq_Step++;
                        time_to_wait = millis() + 100;
                        break;
                    case '5':
                        Serial.print("5");
                        digitalWrite(LED_BLUE, HIGH);
                        digitalWrite(LED_GREEN, LOW);
                        digitalWrite(BELL, BELL_ON);
                        digitalWrite(LED_RED, LOW);
                        seq_Step++;
                        time_to_wait = millis() + 100;
                        break;
                    default:
                        Serial.println("switch Default reached - we should never get here ! ");
                        Serial.println("Error Condition -  ! ");
                        Serial.print(" Action Char = [");
                        Serial.print(thisStep);
                        Serial.println("]");
                    
                        digitalWrite(LED_BLUE, LOW);
                        digitalWrite(LED_GREEN, LOW);
                        digitalWrite(BELL, BELL_OFF);
                        digitalWrite(LED_RED, LOW);

                        doSequence = false;
                        actionString = "";
                        mode = MODE_WAIT;
                        action_mode_changed = true;
                        time_to_wait = 0;
                        seq_Step = 0;
                        break;
                }
            }
            yield();
        }

    }
}
