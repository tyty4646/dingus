#include <HTTPClient.h>
#include <WiFi.h>
#include <TFT_eSPI.h>

/***********************************
*           USER CONFIG            *
***********************************/

// 1 for USB-C port on right
// 3 for USB-C port on left
#define orientation 1

// tyler or aidan (must be all lowercase!)
const String user = "tyler";

const char* NET_SSID = "Verizon_6NSP4Q";
const char* NET_PASS = "splash9-fax-con";

// use http://greekgeeks.net/#maker-tools_convertColor
// "5:6:5 packed" value
const uint32_t themeColor = 0xC99F;
const uint32_t notifColor = 0xFFE3;

const unsigned int MAX_MESSAGES = 5000;


/***********************************
*         EVERYTHING ELSE          *
***********************************/

#if orientation == 1
  #define TOP_BUTTON_PIN 1
  #define MIDDLE_BUTTON_PIN 2
  #define BOTTOM_BUTTON_PIN 43
#else
  #define TOP_BUTTON_PIN 43
  #define MIDDLE_BUTTON_PIN 2
  #define BOTTOM_BUTTON_PIN 1
#endif

// misc stuff
HTTPClient httpMsg;
HTTPClient httpList;
TFT_eSPI lcd = TFT_eSPI();
unsigned int currentMillis = 0, displayTimeout;
int sigStrength = 0;
bool newMessage = true, displayOn = true, flashOn = true, newMessageFirstLoop = false;
String tmpDelimiter;

// button stuff
unsigned int millisMiddleButtonPressed, millisTopButtonPressed, millisBottomButtonPressed;
bool pvBottomButton, pvMiddleButton, pvTopButton, bottomButtonPressed = false, middleButtonPressed = false, topButtonPressed = false;
bool pvDebounce = 1;

// list stuff
String message = "";
String messagePath = "";
String indexString = "";
unsigned int newMessageCount = 0, selectedMessage = 0, indexValue = 0, pvIndexValue = 0, lastReadIndexValue = 0;
bool messageUnread[MAX_MESSAGES], unreadMessagesBefore = false, unreadMessagesAfter = false;
String indexPath = "https://tylerlindsay.com/messages/v2.1/" + user + "/index.log";

// delays
unsigned int pvMillisSigStrength = 5000, intervalMillisSigStrength = 5000;
unsigned int pvMillisNotificationFlash = 1000, intervalMillisNotificationFlash = 1000;
const TickType_t intervalList = 30000;
unsigned int pvMillisDebounce = 0, intervalMillisDebounce = 35;

unsigned int intervalLongPress = 1500;
unsigned int intervalDisplayTimeout = 30000;

// device states
int state = 0;
  // 0 -> standby
  // 1 -> new message

int wifiState = 0;
  // 0 -> disconnected
  // 1 -> connected


// ------------- setup -------------


void setup() {
  Serial.begin(500000);
  
  // triggers the LCD backlight on startup (pin 15 is display toggle)
  pinMode(15, OUTPUT); 
  digitalWrite(15, HIGH);
  
  // set button pin mode to internal pullup
  pinMode(TOP_BUTTON_PIN, INPUT_PULLUP);
  pinMode(MIDDLE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BOTTOM_BUTTON_PIN, INPUT_PULLUP);

  pvTopButton = digitalRead(TOP_BUTTON_PIN);
  pvMiddleButton = digitalRead(MIDDLE_BUTTON_PIN);
  pvBottomButton = digitalRead(BOTTOM_BUTTON_PIN);
  
  setupDisplay();

  // setup brightness control and set brightness to max
  ledcSetup(0, 10000, 8);
  ledcAttachPin(38, 0);
  ledcWrite(0, 255);
  
  printStartupLogo();
//  initWiFi();
  setupWiFi();
  
  httpList.begin(indexPath.c_str()); // begins HTTP at indexPath

  // set up updateList for core 0
  xTaskCreatePinnedToCore(
      updateListFunction, /* Function to implement the task */
      "updateList", /* Name of the task */
      10000,  /* Stack size in words */
      NULL,  /* Task input parameter */
      tskIDLE_PRIORITY,  /* Priority of the task */
      NULL,  /* Task handle. */
      0); /* Core where the task should run */
}

// ------------- loop -------------

void loop() {
  currentMillis = millis();

  // standby and wifi disconnected
  if (state == 0 && wifiState == 0) {
    
  }
    
  // standby and wifi connected
  else if (state == 0 && wifiState == 1) {
    if (currentMillis - pvMillisSigStrength >= intervalMillisSigStrength) {
      updateSigStrength();
      pvMillisSigStrength = currentMillis;
    }
    checkButtonPresses();
  }

  // new message and wifi disconnected
  else if (state == 1 && wifiState == 0) {
    
  }
    

  // new message and wifi connected
  else {
    drawNewMessageNotification();
    checkButtonPresses();
  }
}

// ------------- main functions -------------

void updateListFunction(void * pvParameters) {
  TickType_t xLastWakeTime;
  for(;;){
    int startMillis = millis();
    int httpResponseCode = httpList.GET(); // Send HTTP GET request
    if (httpResponseCode > 0) { // if there is a response, process the index
      indexString = httpList.getString(); // gets full list from web server using HTTP
      indexValue = atol(indexString.c_str());

      if (pvIndexValue != indexValue) {
        newMessageCount = indexValue - lastReadIndexValue;
        state = 1;
        newMessageFirstLoop = true;
        for (int i = lastReadIndexValue; (i <= newMessageCount) && (i < MAX_MESSAGES); i++) {
          messageUnread[i] = true;
        }
      }
      pvIndexValue = indexValue;
    }
    else { // if the GET request returns code 0, assume there was an issue
      clearDisplay();
      lcd.println("List not found...");
    }
    Serial.print("updateListFunction took ");
    Serial.print(millis() - startMillis);
    Serial.println(" ms to run");
    vTaskDelayUntil(&xLastWakeTime, intervalList);
  }
}

void drawStatusBar() {
  lcd.setTextColor(themeColor, TFT_BLACK);
  unreadMessagesAfter = false;
  unreadMessagesBefore = false;

  for (int i = 1; i < selectedMessage; i++) {
    if (messageUnread[i]) {
      unreadMessagesBefore = true;
      break;
    }
  }

  for (int i = selectedMessage; i < MAX_MESSAGES; i++) {
    if (messageUnread[i]) {
      unreadMessagesAfter = true;
      break;
    }
  }
  
  if (selectedMessage == 0) {
    // do nothing
  } else if (indexValue == 1) { // if only one message exists
    lcd.fillTriangle(274, 10, 284, 10, 279, 0, TFT_DARKGREY); // up arrow darkened
    lcd.fillTriangle(288, 0, 298, 0, 293, 10, TFT_DARKGREY); // down arrow darkened
  } else if (selectedMessage == indexValue) { // if last message selected
    if (unreadMessagesBefore) {
      lcd.fillTriangle(274, 10, 284, 10, 279, 0, notifColor); // up arrow notif
    } else {
      lcd.fillTriangle(274, 10, 284, 10, 279, 0, themeColor); // up arrow
    }
    lcd.fillTriangle(288, 0, 298, 0, 293, 10, TFT_DARKGREY); // down arrow darkened
  } else if (selectedMessage == 1) { // if first message selected
    if (unreadMessagesAfter) {
      lcd.fillTriangle(288, 0, 298, 0, 293, 10, notifColor); // down arrow notif
    } else {
      lcd.fillTriangle(288, 0, 298, 0, 293, 10, themeColor); // down arrow
    }
    lcd.fillTriangle(274, 10, 284, 10, 279, 0, TFT_DARKGREY); // up arrow darkened
  } else { // if neither first nor last message selected
    if (unreadMessagesBefore) {
      lcd.fillTriangle(274, 10, 284, 10, 279, 0, notifColor); // up arrow notif
    } else {
      lcd.fillTriangle(274, 10, 284, 10, 279, 0, themeColor); // up arrow
    }
    if (unreadMessagesAfter) {
      lcd.fillTriangle(288, 0, 298, 0, 293, 10, notifColor); // down arrow notif
    } else {
      lcd.fillTriangle(288, 0, 298, 0, 293, 10, themeColor); // down arrow
    }
  }

  lcd.setCursor(225, 3, 1);
  lcd.print(selectedMessage);
  lcd.print("/");
  lcd.print(indexValue);
  lcd.setCursor(0, 0, 2);
  
  //lcd.drawLine(0, 16, 320, 16, 0xffff);
  //lcd.drawLine(0, 17, 320, 17, 0xffff);
  updateSigStrength();
}

void getMessage() {
  int startMillis = millis();
  messagePath = "https://tylerlindsay.com/messages/v2.1/" + user + "/" + selectedMessage + ".txt";
  httpMsg.begin(messagePath.c_str());
  
  int httpResponseCode = httpMsg.GET();
  if (httpResponseCode>0) {
      message = httpMsg.getString();
  } else {
      clearDisplay();
      lcd.println("Msg not found...retrying");
      delay(2000);
      getMessage();
  }
  httpMsg.end();
  Serial.print("getMessage took ");
  Serial.print(millis() - startMillis);
  Serial.println(" ms to run");
}

void drawNewMessageNotification() {
  lcd.setTextColor(themeColor, TFT_BLACK);
  if (newMessageFirstLoop) { // if it's the first time through the loop when there is a new message
      lcd.fillScreen(TFT_BLACK);

      if (newMessageCount == 1) {
        lcd.setCursor(90, 65, 2);
        lcd.println("1 NEW MESSAGE!!! :3");
      } else {
        lcd.setCursor(95, 65, 2);
        lcd.print(newMessageCount);
        lcd.println(" NEW MESSAGES!!! :3");
      }
      
      lcd.setCursor(62, 80, 2);
      lcd.println("PRESS MIDDLE BUTTON TO VIEW");
      newMessageFirstLoop = false;
  }
    
  if (currentMillis - pvMillisNotificationFlash >= intervalMillisNotificationFlash) {
    if (!flashOn) {
      ledcWrite(0, 255); // toggles the display on (brightness = 255)
    } else {
      ledcWrite(0, 0); // toggles the display off (brightness = 0)
    }
    flashOn = !flashOn;
    pvMillisNotificationFlash = currentMillis;
  }
}

void printMessage() {
  lastReadIndexValue = selectedMessage;
  clearDisplay();
  ledcWrite(0, 255);
  lcd.setTextColor(themeColor, TFT_BLACK);
  
  // this god-forsaken substring manipulation is so stupid but it works for v2 and v2.1 date formats *shrugs*
  tmpDelimiter = message.substring(13, 14);  
  lcd.setCursor(0, 3, 1);
  if (!tmpDelimiter.equals(":")) {
    lcd.print(message.substring(0, 21));
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setCursor(0, 10, 2);
    lcd.print(message.substring(22, message.length()));
  } else {
    lcd.print(message.substring(0, 26));
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setCursor(0, 10, 2);
    lcd.print(message.substring(27, message.length()));
  }
}

void updateSigStrength() {
  sigStrength = WiFi.RSSI();
  lcd.fillRect(306, 0, 12, 12, TFT_BLACK);
      if (sigStrength > -50) {                        // 4 bars
        lcd.fillRect(318, 0, 2, 11, themeColor);
        lcd.fillRect(314, 3, 2, 8, themeColor);
        lcd.fillRect(310, 6, 2, 5, themeColor);
        lcd.fillRect(306, 9, 2, 2, themeColor);
      } else if (sigStrength > -60) {                 // 3 bars
        lcd.fillRect(318, 0, 2, 11, TFT_DARKGREY);
        lcd.fillRect(314, 3, 2, 8, themeColor);
        lcd.fillRect(310, 6, 2, 5, themeColor);
        lcd.fillRect(306, 9, 2, 2, themeColor);
      } else if (sigStrength > -70) {                 // 2 bars
        lcd.fillRect(318, 0, 2, 11, TFT_DARKGREY);
        lcd.fillRect(314, 3, 2, 8, TFT_DARKGREY);
        lcd.fillRect(310, 6, 2, 5, themeColor);
        lcd.fillRect(306, 9, 2, 2, themeColor);
      } else if (sigStrength > -80) {                 // 1 bar
        lcd.fillRect(318, 0, 2, 11, TFT_DARKGREY);
        lcd.fillRect(314, 3, 2, 8, TFT_DARKGREY);
        lcd.fillRect(310, 6, 2, 5, TFT_DARKGREY);
        lcd.fillRect(306, 9, 2, 2, themeColor);
      } else {                                        // 0 bars
        lcd.fillRect(318, 0, 2, 11, TFT_DARKGREY);
        lcd.fillRect(314, 3, 2, 8, TFT_DARKGREY);
        lcd.fillRect(310, 6, 2, 5, TFT_DARKGREY);
        lcd.fillRect(306, 9, 2, 2, TFT_DARKGREY);
      }
}

void printStartupLogo() {
  lcd.setFreeFont(&DejaVu_Sans_Mono_12);
  lcd.setTextColor(TFT_RED, TFT_BLACK);
  lcd.println("           ___");
  lcd.setTextColor(TFT_ORANGE, TFT_BLACK);
  lcd.println("      ____/ (_)___  ____  __  _______");
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.println("     / __  / / __ \\/ __ `/ / / / ___/");
  lcd.setTextColor(TFT_GREEN, TFT_BLACK);
  lcd.println("    / /_/ / / / / / /_/ / /_/ (__  )");
  lcd.setTextColor(TFT_BLUE, TFT_BLACK);
  lcd.println("   \/___,_/_/_/ /_/\___, /\\__,_/____/ ");
  lcd.setTextColor(TFT_PURPLE, TFT_BLACK);
  lcd.println("                /____/    ___   ___");
  lcd.setTextColor(TFT_RED, TFT_BLACK);
  lcd.println("                    _  __|_  | <  /");
  lcd.setTextColor(TFT_ORANGE, TFT_BLACK);
  lcd.println("                   | |/ / __/_ / /");
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.println("                   |___/____(_)_/");
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.setTextFont(1);
  lcd.setCursor(223, 162);
  lcd.println("by Tyler Lindsay");
  lcd.setTextFont(2);
}

void checkButtonPresses() {
  if(!debounceBtn(MIDDLE_BUTTON_PIN) || !debounceBtn(BOTTOM_BUTTON_PIN) || !debounceBtn(TOP_BUTTON_PIN)) {
    displayTimeout = currentMillis;
    state = 0;
    if (!displayOn) {
      ledcWrite(0, 255); // toggles the display on (brightness = 255)
      displayOn = true;
      drawStatusBar();
    }
  }
  
  if (state == 0 && displayOn && currentMillis - displayTimeout >= intervalDisplayTimeout) {
    ledcWrite(0, 0); // toggles the display off (brightness = 0)
    displayOn = false;
  }

  // ---- BOTTOM BUTTON ----
  if (!debounceBtn(BOTTOM_BUTTON_PIN) && pvBottomButton) { // on button press
    millisBottomButtonPressed = currentMillis;
    bottomButtonPressed = true;
  } else if (currentMillis - millisBottomButtonPressed < intervalLongPress && debounceBtn(BOTTOM_BUTTON_PIN) && bottomButtonPressed) { // on button release, hasn't been held down
    bottomButtonPressed = false;
    if (selectedMessage <= indexValue - 1) {
      selectedMessage++;
    }
    if (messageUnread[selectedMessage]) {
      newMessageCount--;
      messageUnread[selectedMessage] = false;
    }
    getMessage();
    printMessage();
  }
  if (currentMillis - millisBottomButtonPressed >= intervalLongPress && bottomButtonPressed) { // has been held down
    bottomButtonPressed = false;
    selectedMessage = indexValue;
    for (int i = 0; i < MAX_MESSAGES; i++) {
      messageUnread[i] = false;
    }
    newMessageCount = 0;
    getMessage();
    printMessage();
  }
  pvBottomButton = debounceBtn(BOTTOM_BUTTON_PIN);

  // ---- TOP BUTTON ----
  if (!debounceBtn(TOP_BUTTON_PIN) && pvTopButton) { // on button press
    millisTopButtonPressed = currentMillis;
    topButtonPressed = true;
  } else if (currentMillis - millisTopButtonPressed < intervalLongPress && debounceBtn(TOP_BUTTON_PIN) && topButtonPressed) { // on button release, hasn't been held down
    topButtonPressed = false;
//    Serial.println(millisTopButtonPressed);
//    Serial.println(debounceBtn(TOP_BUTTON_PIN));
//    Serial.println(topButtonPressed);
    if (selectedMessage > 1) {
      selectedMessage--;
    }
    messageUnread[selectedMessage] = false;
    getMessage();
    printMessage();
  }
  if (currentMillis - millisTopButtonPressed >= intervalLongPress && topButtonPressed) { // has been held down
    topButtonPressed = false;
    Serial.println("top button held");
    selectedMessage = 1;
    getMessage();
    printMessage();
  }
  pvTopButton = debounceBtn(TOP_BUTTON_PIN);

  // ---- MIDDLE BUTTON ----
  if (!debounceBtn(MIDDLE_BUTTON_PIN) && pvMiddleButton) { // on button press
    millisMiddleButtonPressed = currentMillis;
    middleButtonPressed = true;
  } else if (currentMillis - millisMiddleButtonPressed < intervalLongPress && debounceBtn(MIDDLE_BUTTON_PIN) && middleButtonPressed) { // on button release, hasn't been held down
    middleButtonPressed = false;
    if (newMessageCount != 0) {
      selectedMessage = indexValue+1 - newMessageCount;
      if (messageUnread[selectedMessage]) {
        newMessageCount--;
        messageUnread[selectedMessage] = false;
      }
    }
    getMessage();
    printMessage();
  }
  if (currentMillis - millisMiddleButtonPressed >= intervalLongPress && middleButtonPressed) { // has been held down
    middleButtonPressed = false;
    newMessageCount = 0;
    selectedMessage = indexValue;
    for (int i = 0; i < MAX_MESSAGES; i++) {
      messageUnread[i] = false;
    }
    newMessageCount = 0;
    getMessage();
    printMessage();
  }
  pvMiddleButton = debounceBtn(MIDDLE_BUTTON_PIN);
}

// ------------- utility functions -------------

bool debounceBtn(int btnPin) {
  pvDebounce = digitalRead(btnPin);
  delay(2);
  if (pvDebounce == digitalRead(btnPin)) {
    return digitalRead(btnPin);
  }
  
}

void setupDisplay() {
  lcd.init();
  lcd.setRotation(orientation);
  lcd.setSwapBytes(true);
  lcd.setTextColor(themeColor, TFT_BLACK);
  lcd.setTextDatum(1);
  lcd.setTextSize(1);
  lcd.fillScreen(TFT_BLACK);
  lcd.setCursor(0, 20, 2);
}

void initWiFi() {
  WiFi.begin(NET_SSID, NET_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    lcd.fillRect(318, 0, 2, 11, TFT_DARKGREY);
    lcd.fillRect(314, 3, 2, 8, TFT_DARKGREY);
    lcd.fillRect(310, 6, 2, 5, TFT_DARKGREY);
    lcd.fillRect(306, 9, 2, 2, TFT_DARKGREY);
    lcd.drawLine(306, 11, 320, 0, TFT_RED);
    lcd.drawLine(306, 0, 320, 11, TFT_RED);
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiState = 1;
    updateSigStrength();
  }
}

void setupWiFi() { // lists networks, connects to specified network
  //WiFi.disconnect();
  WiFi.mode(WIFI_STA);
//  WiFi.onEvent(WiFiStationDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  delay(100);
  initWiFi();
}

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info){
  if (WiFi.status() != WL_CONNECTED) {
    wifiState = 0;
    clearDisplay();
    lcd.setCursor(0, 10, 2);
    lcd.println("WiFi connection lost.");
    lcd.println("Reconnecting in... ");
    for (int i = 10; i > 0; i--) {
      lcd.print(i);
      lcd.print(" ");
      delay(1000);
    }
    lcd.println("Reconnecting...");
    WiFi.disconnect();
    WiFi.reconnect();
  } else if (WiFi.status() == WL_CONNECTED) {
    wifiState = 1;
    updateSigStrength();
  }
}

void clearDisplay() {
  lcd.fillScreen(TFT_BLACK);
  lcd.setCursor(0, 20, 2);
  drawStatusBar();
}
