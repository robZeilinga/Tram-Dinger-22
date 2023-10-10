
#include <Arduino.h>
#include <Preferences.h>
#ifdef ESP32
  #include <WiFi.h>
  #include <AsyncTCP.h>
#else
  #include <ESP8266WiFi.h>
  #include <ESPAsyncTCP.h>
#endif
#include <ESPAsyncWebServer.h>

// pin allocations 
#define BTN D2
#define LED_BLUE D7
#define LED_RED D8
#define LED_GREEN D6
#define BELL D4
#define NONE 99

// run modes
#define MODE_WAIT 0
#define MODE_DING 1
#define MODE_CONT 3
#define MODE_ALARM 9
#define MODE_PEND_CONT 2
#define MODE_PEND_ALARM 8

// Wifi Modes
#define WIFI_DEV 0
#define WIFI_PROD 1

int mode = -1;

// BTN STATES
#define BTN_UP 0
#define BTN_DOWN 1


int BELL_ON = LOW;
int BELL_OFF = HIGH;

int prefs_ding_time = -1;
int prefs_ding_off_time = -1;
int prefs_ding_pause_time = -1;

int settings_ding_time = -1;
int settings_ding_off_time = -1;
int settings_ding_pause_time = -1;
bool settings_invert_bell = false;

String actionString_DingDing = "";
String actionString_Cont = "";
String actionString = "";


// HTML web page to handle 3 input fields (input1, input2, input3)
const char index_html[]  = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <title>ESP Input Form</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  </head><body>
  <form action="/get">
  <div>To Test Values, Press Hardware Button</div>
  <table>
  <tr>
  <th> Description </th><th> Stored Value</th><th>Current Value</th></tr>
    <tr><td>Bell On Time [1-20] units of 100ms (0.1s) 1 == .1s : </td><td>%PREFS_ON_TIME%</td><td><input type="text" name="dingOnTime" id="dingOnTime" value="%ON_TIME%"></td></tr>
    <tr><td>Bell Off Time ( between the two rings) [1-20] 2 = .2s : </td><td>%PREFS_OFF_TIME%</td><td><input type="text" name="dingOffTime" id="dingOffTime" value="%OFF_TIME%"></td></tr>
    <tr><td>Cycle Pause Time ( between sets of rings in Repeat Mode) : </td><td>%PREFS_PAUSE_TIME%</td><td><input type="text" name="dingPauseTime" id="dingPauseTime" value="%PAUSE_TIME%" ></td></tr>
    <tr><td colspan="3"><input type="submit" value="Change Values"></form></td></tr>
    <tr><td colspan="3"><form action="/revert"><input type="submit" value="Revert Settings To Stored"></form></td></tr>
    </table>
  </br>
  <form action="/toggle" ><input type="submit" value="Toggle Bell Polarity"></form>
  <div> <b><i> NOTE: Changing the Values Above are NOT stored ! ( i.e. Losing Power will revert the Values to those SAVED to Persistant Memory) </i></b></br>
  To Save the Values to Persistant Memory, Press the Store Button Below.</br>
  <form action="/store">
  <input type="submit" value="STORE"></form></div></br>   
  

  </body></html>)rawliteral";

String processor(const String& var) {
    if (var =="ON_TIME"){
        return String(settings_ding_time);
    }
    if(var == "OFF_TIME"){
        return String(settings_ding_off_time);
    }
    if(var == "PAUSE_TIME"){
        return String(settings_ding_pause_time);
    }
    if(var == "PREFS_ON_TIME"){
        return String(prefs_ding_time);
    }
    if(var == "PREFS_OFF_TIME"){
        return String(prefs_ding_off_time);
    }
    if(var =="PREFS_PAUSE_TIME"){
        return String(prefs_ding_pause_time);
    }
    return String();
}

int PRV_BTN_STATE = 0;
long old_millis = 0;
long down_time = 0;
long tot_dwn_time = 0;
bool action_mode_changed = false;
int seq_Step = -1;
long unsigned time_to_wait = 0;
bool doSequence = false;

Preferences prefs;
AsyncWebServer server(80);


void ledColour(int val){
    digitalWrite(LED_BLUE, val == LED_BLUE ? HIGH:LOW );
    digitalWrite(LED_RED, val == LED_RED ? HIGH:LOW );
    digitalWrite(LED_GREEN, val == LED_GREEN ? HIGH:LOW );
}

void reloadBellPolarity(){
    if(settings_invert_bell){
        BELL_OFF = LOW;
        BELL_ON = HIGH;
    } else {
        BELL_OFF = HIGH;
        BELL_ON = LOW;

    }
}

void notFound(AsyncWebServerRequest *request){
    request->send(404,"text/plain","Not Found");
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

        // Prefs helper Scripts ( uncomment as needed ) _ NB DONT FORGET TO RE_COMMENT !! 
        // ========================================================
        // Clear all keys 
        // prefs.clear();
        // ------------------------------------
        // Procedure to Set DEV ssid & Password
        // ------------------------------------ 
        // 1. Uncomment lines below and replace with your network's details
        // 2. Save, upload & reset the 8266.  Your Network Creds will be set in Preferences 
        // 3. REVERT and RE-COMMENT the lines below & save Code. 
        //  NB NB NB - DON'T COMMIT CODE TO YOUR REPO WITH YOUR CREDS IN PLAIN TEXT!!!!!
        // 
        // Creds are stored in the Preferences area. 
        // 
        // and yes, i could of written a special hidden page to allow the user to enter the creds 
        // - but that is a lot of work for a setting that is only used once.
        
        //prefs.remove("DEV_SSID");
        //prefs.remove("DEV_PSK");

        // prefs.putString("DEV_SSID", "<<Your DEV SSID Here>>");
        // prefs.putString("DEV_PSK", "<<Your DEV Network Passord Here>>");


        // ============================================================
        // Rename Soft AP - PROD SSID ( the 8266's Network )
        // ---------------------------------------
        // 
        // prefs.putString("PROD_SSID", "Tram Dinger");
        // prefs.putString("PROD_PSK", "Secret");
        // ========================================
        // Hide / Show PROD SSID 
        //----------------
        // prefs.putInt("PROD_WIFI_HIDDEN", 0);   // 0 == Visible, 1 == Hidden
        
        // =====================================
        // Switch between PROD & DEV
        // ------------------------------------
        // prefs.putInt("WifiMode", WIFI_PROD);   // WIFI_PROD or WIFI_DEV
        

       // bool settings_invert_bell = prefs.getBool("invertBell", false);

        settings_ding_time = prefs.getInt("dingTime", 1);
        settings_ding_off_time = prefs.getInt("dingOffTime", 2);
        settings_ding_pause_time = prefs.getInt("dingPauseTime", 7);

        prefs_ding_time = settings_ding_time;
        prefs_ding_off_time = settings_ding_off_time;
        prefs_ding_pause_time = settings_ding_pause_time;

        reloadBellPolarity();

      


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

        // WIFI Connection Settings 
        int settings_wifi_mode = prefs.getInt("WifiMode", WIFI_PROD);  
        settings_wifi_mode = WIFI_DEV;
        bool Wifi_UP = false;
        if(settings_wifi_mode == WIFI_PROD){
            //  MODE 
            String settings_wifi_prod_ssid = prefs.getString("PROD_SSID", "TramDinger");
            String settings_wifi_prod_psk = prefs.getString("PROD_PSK", "");
            int settings_wifi_prod_hidden = prefs.getInt("PROD_WIFI_HIDDEN", 0);  // 1 == hidden
            Serial.print("Setting soft-AP ... ");
            Serial.print(settings_wifi_prod_ssid);
            Serial.print(" .... ");
            Wifi_UP = WiFi.softAP(settings_wifi_prod_ssid, settings_wifi_prod_psk,1,settings_wifi_prod_hidden);
            
            Serial.println(Wifi_UP ? "Ready" : "Failed!");  // hidden AP

            if(!Wifi_UP){
                // turn on RED LED & wait 
                ledColour(LED_RED);
                while(1){}
            } else {
                Serial.print("Soft-AP IP address = ");
                Serial.println(WiFi.softAPIP()); // default is 192.168.4.1.
            }
        } else {
            // DEV MODE
            String settings_wifi_dev_ssid = prefs.getString("DEV_SSID", "");
            String settings_wifi_dev_psk = prefs.getString("DEV_PSK","");
            if(settings_wifi_dev_ssid == ""){
                // invalid - revert to PROD mode 
                Serial.println("No DEV SSID Found, Rev erting to PROD MODE");
                settings_wifi_mode = WIFI_PROD;
            } else {

            WiFi.begin(settings_wifi_dev_ssid, settings_wifi_dev_psk);
            Serial.print("Connecting");
            while (WiFi.status() != WL_CONNECTED)
            {
                delay(250);
                Serial.print(".");
                ledColour(LED_GREEN);
                delay(250);
                ledColour(NONE);    

            }
            ledColour(NONE);
            Serial.println();

            Serial.print("Connected, IP address: ");
            Serial.println(WiFi.localIP());
            }
        }

        // =========================================================
        // Server routes
        // =========================================================

        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send_P(200,"text/html",index_html, processor);
        });

        server.on("/get", HTTP_GET, [] (AsyncWebServerRequest *request){
            if(request->hasParam("dingOnTime")) settings_ding_time = request->getParam("dingOnTime")->value().toInt();
            if(request->hasParam("dingOffTime")) settings_ding_off_time = request->getParam("dingOffTime")->value().toInt();
            if(request->hasParam("dingPauseTime")) settings_ding_pause_time = request->getParam("dingPauseTime")->value().toInt();
            buildActionSequences();
            request->send_P(200,"text/html",index_html, processor);
        });

        server.on("/revert", HTTP_GET, [] (AsyncWebServerRequest *request){
            settings_ding_time = prefs_ding_time;
            settings_ding_off_time = prefs_ding_off_time;
            settings_ding_pause_time = prefs_ding_pause_time;
            buildActionSequences();
            request->send_P(200,"text/html",index_html, processor);
        });

        server.on("/store", HTTP_GET, [] (AsyncWebServerRequest *request){
            prefs.putInt("dingTime", settings_ding_time);
            prefs.putInt("dingOffTime", settings_ding_off_time);
            prefs.putInt("dingPauseTime", settings_ding_pause_time);
            prefs_ding_time = settings_ding_time;
            prefs_ding_off_time = settings_ding_off_time;
            prefs_ding_pause_time = settings_ding_pause_time;
            request->send_P(200,"text/html",index_html, processor);

            // TODO: check out https://randomnerdtutorials.com/esp32-esp8266-input-data-html-form/ the second part shows Async updates without refresh 

        });

        server.on("/toggle", HTTP_GET, [] (AsyncWebServerRequest *request){
            bool oldValue = prefs.getBool("invertBell", false);
            prefs.putBool("invertBell", !oldValue);
            settings_invert_bell = !oldValue;
            reloadBellPolarity();
            request->send_P(200,"text/html",index_html, processor);
        });

        server.onNotFound(notFound);
        server.begin();

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
                        // edge case - check if ActionString has changed
                        if (mode == MODE_CONT){
                            actionString = actionString_Cont;
                        }
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
