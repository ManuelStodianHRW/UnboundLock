#include <Preferences.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <TFT_eSPI.h>
#include <Stepper.h>
#include <Button2.h>

// MOTOR PINS
#define MOTOR_IN1 15
#define MOTOR_IN2 2
#define MOTOR_IN3 13
#define MOTOR_IN4 12

/*#define TASTENFELD_PIN 3
#define RFID_PIN 4*/
#define ULTRASONIC_TRIGGER_PIN 38
#define ULTRASONIC_ECHO_PIN 33
#define ISTGESCHLOSSEN_PIN 39

#define LEFT_BUTTON_PIN 0
#define RIGHT_BUTTON_PIN 35
#define TFT_BACKLIGHT_PIN 4

#define MSG_BUFFER_SIZE (50)
#define TOPIC_BUFFER_SIZE (256)
#define ID_BUFFER_SIZE (32)

// ESP OLED-SCREEN
TFT_eSPI tft = TFT_eSPI();
unsigned long sinceDisplay = 0;
void init_tft();

// UNIQUE DEVICE ID
Preferences preferences;
char unitId[ID_BUFFER_SIZE];
int id_checksum = 0;

// ESP BUTTONS
Button2 left_button(LEFT_BUTTON_PIN);
Button2 right_button(RIGHT_BUTTON_PIN);
void on_left_button_tap(Button2& btn);
void on_right_button_tap(Button2& btn);

// WIFI ACCESS-DATA
const char* ssid = "FRITZ!Box 6490 Cable"; 
const char* password = "65668467166403417168";

// MQTT ACCESS-DATA
const char* mqtt_server = "hrw-fablab.de";
const int mqtt_server_port = 1883; // 9001 does not reply
const char* mqtt_server_usern = "gruppe8";
const char* mqtt_server_password = "q3M@qGRXzEskj5UK";

// WIFI
WiFiClient espClient;
void init_wifi();
// MQTT-CLIENT
PubSubClient client(espClient);
void reconnect();
unsigned long lastUpdate = 0;
char updateMsg[MSG_BUFFER_SIZE];
int value = 0;

// MOTOR
int SPU = 2048; // Stufen pro Umdrehung
Stepper Motor(SPU, MOTOR_IN4, MOTOR_IN2, MOTOR_IN3, MOTOR_IN1);

// TOPICS
char* topic_root = "ES/WS20/gruppe8/";
char topic_doorSensors[TOPIC_BUFFER_SIZE];
char topic_doorSensors_isClosed[TOPIC_BUFFER_SIZE];
char topic_doorSensors_areActionsNearHandle[TOPIC_BUFFER_SIZE];
char topic_motor[TOPIC_BUFFER_SIZE];
char topic_motor_direction[TOPIC_BUFFER_SIZE];

char topic_customText[TOPIC_BUFFER_SIZE];
char topic_newId[ID_BUFFER_SIZE];
void subscribeToTopics();

// 
bool isClosed;
bool isClosedLoop();

bool areActionsNearHandle;
bool areActionsNearHandleLoop();
unsigned int timeToDoorHandle;
const unsigned int timeToDoorHandlePuffer = 25;

void setup() {
  preferences.begin("my-vars", false);
  //preferences.putString("value","0"); // HARD-CODE RESET ID
  snprintf(unitId, 32, "%s", preferences.getString("id", "0"));
  timeToDoorHandle = preferences.getUInt("standardDelay", 100);
  
  init_tft();

  Motor.setSpeed(10);

  /*  
  pinMode(TASTENFELD_PIN, INPUT);
  pinMode(RFID_PIN, INPUT);*/
  pinMode(ULTRASONIC_ECHO_PIN, INPUT);
  pinMode(ULTRASONIC_TRIGGER_PIN, OUTPUT);
  pinMode(ISTGESCHLOSSEN_PIN, INPUT);
  pinMode(LEFT_BUTTON_PIN, INPUT);

  pinMode(TFT_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(TFT_BACKLIGHT_PIN, LOW);

  left_button.setTapHandler(on_left_button_tap);
  right_button.setTapHandler(on_right_button_tap);

  // concatenates topic names with the id
  // topic_root + unitId + x = topic_x
  snprintf(topic_doorSensors, TOPIC_BUFFER_SIZE, "%s%s%s", topic_root, unitId, "/DoorSensors/#");
  snprintf(topic_doorSensors_isClosed, TOPIC_BUFFER_SIZE, "%s%s%s", topic_root, unitId, "/DoorSensors/isClosed");
  snprintf(topic_doorSensors_areActionsNearHandle, TOPIC_BUFFER_SIZE, "%s%s%s", topic_root, unitId, "/DoorSensors/actionsNearDoorHandle");
  snprintf(topic_motor, TOPIC_BUFFER_SIZE, "%s%s%s", topic_root, unitId, "/Motor/#");
  snprintf(topic_motor_direction, TOPIC_BUFFER_SIZE, "%s%s%s", topic_root, unitId, "/Motor/direction");
  
  snprintf(topic_customText, TOPIC_BUFFER_SIZE, "%s%s%s", topic_root, unitId, "/CustomUserText");
  snprintf(topic_newId, ID_BUFFER_SIZE, "%s%s%s", topic_root, unitId, "/newId");
  
  Serial.begin(115200);

  Serial.print("ID of device: "); Serial.println(unitId);
  Serial.print("TimeToDoorHandle: "); Serial.println(timeToDoorHandle);

  init_wifi();
  client.setServer(mqtt_server, mqtt_server_port);
  client.setCallback(callback);

  isClosed = isClosedLoop();
  areActionsNearHandle = areActionsNearHandleLoop();
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived ["); Serial.print(topic); Serial.print("]: ");
  for(int i = 0; i < length; i++) {
    Serial.print((char) payload[i]);
  }
  Serial.println();

  char message[length+1];
  snprintf(message, length+1, "%s", payload);

  // if the first received char of payload contains 1, the motor will be runnig clockwise if  0, counter-clockwise
  if(strcmp(topic_motor_direction, topic) == 0) {
    if(payload[0] == '1' && isClosed != true) {
      printMessageOnScreen("WELCOME");
      Motor.step(-SPU*2);
    } else if(payload[0] == '0' && isClosed != false) {
      printMessageOnScreen("SEE YOU SOON");
      Motor.step(SPU*2);
    } else {
      Serial.print("Latest received message ["); Serial.print(topic); Serial.println("]: not a valid message.");
    }
    if(isClosed) Serial.println("Door is closed.");
    else Serial.println("Door is not closed.");
  } 
  else if(strcmp(topic_customText, topic) == 0) {
    printMessageOnScreen(message);
  } 
  else if(strcmp(topic_newId, topic) == 0) {
    Serial.print("Inboxed new ID: "); Serial.println(message);
    preferences.putString("id", message);
    preferences.end();
    ESP.restart();
  }
}

void loop() {  
  if(!client.connected()) {
    reconnect();
  }
  client.loop();
  
  isClosedLoop();
  areActionsNearHandleLoop();
  
  // updates status every 10 seconds 
  unsigned long now = millis();
  if(now - lastUpdate >= 10000) { // update interval
    lastUpdate = now;
    //client.publish(topic_customText, "HALLO WELT");
    /*if(client.publish(topic_doorSensor_isClosed, v_isClosed)) {
        Serial.print("Message published [isClosed]: "); Serial.println(v_isClosed);
     } 
     if(client.publish(topic_doorSensor_isClosed, v_areActionsNearHandle)) {
       Serial.print("Message published [areActionsNearHandle]: "); Serial.println(v_areActionsNearHandle);
     } */
  }
  if(now - sinceDisplay >= 30000 && sinceDisplay != 0) { // blends out display text after 30 seconds if one is set
    sinceDisplay = 0;
    digitalWrite(TFT_BACKLIGHT_PIN, LOW);
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 28);
  }
  left_button.loop();
  right_button.loop();
}

/////////

void printMessageOnScreen(char* message) {
  sinceDisplay = millis();
  digitalWrite(TFT_BACKLIGHT_PIN, HIGH);
  delay(300);
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 28);
  tft.print(message);
}

bool isClosedLoop() {
  bool isClosedLocal = analogRead(ISTGESCHLOSSEN_PIN) > 4000;
  if(isClosed != isClosedLocal) {
    isClosed = isClosedLocal;
    client.publish(topic_doorSensors_isClosed, (isClosed ? "1" : "0"));
  }
  return isClosedLocal;
}
bool areActionsNearHandleLoop() {
  unsigned int timeToDoorHandleLocal = 0;
  bool areActionsNearHandleLocal;
  digitalWrite(ULTRASONIC_TRIGGER_PIN, LOW); 
  delay(5); 
  digitalWrite(ULTRASONIC_TRIGGER_PIN, HIGH); 
  delay(10);
  digitalWrite(ULTRASONIC_TRIGGER_PIN, LOW);
  timeToDoorHandleLocal = pulseIn(ULTRASONIC_ECHO_PIN, HIGH)/2;
  areActionsNearHandleLocal = ((timeToDoorHandle - timeToDoorHandleLocal) > timeToDoorHandlePuffer) && timeToDoorHandleLocal != 0;
  if(areActionsNearHandleLocal != areActionsNearHandle) {
    areActionsNearHandle = areActionsNearHandleLocal;
    Serial.print("ActionsNearDoorHandle updated: "); Serial.println(areActionsNearHandle);
    client.publish(topic_doorSensors_areActionsNearHandle, (areActionsNearHandleLocal ? "1" : "0"));
  }
  return areActionsNearHandleLocal;
}

void init_tft() {
  tft.begin();
  tft.setRotation(-1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setFreeFont(&Orbitron_Light_24);
  tft.setCursor(0, 28);
  tft.setTextWrap(true);
}

// Reset ROM
void on_left_button_tap(Button2& btn) {
  Serial.println("LEFT BUTTON BEEN PRESSED");
  preferences.clear();
  preferences.end();
  ESP.restart();
}

// Calibrate ultrasonic distance reader (set standard delay to door handle)
void on_right_button_tap(Button2& btn) {
  Serial.println("RIGHT BUTTON BEEN PRESSED");
  digitalWrite(ULTRASONIC_TRIGGER_PIN, LOW); 
  delay(5); 
  digitalWrite(ULTRASONIC_TRIGGER_PIN, HIGH); 
  delay(10);
  digitalWrite(ULTRASONIC_TRIGGER_PIN, LOW);
  int ttdh = pulseIn(ULTRASONIC_ECHO_PIN, HIGH)/2;
  Serial.print("TimeToDoorHandle updated: "); Serial.println(ttdh);
  preferences.putUInt("standardDelay", (ttdh));
  preferences.end();
  ESP.restart();
}

void init_wifi() {
  int i = 0;
  delay(10);
  //connecting to WiFi-network
  Serial.println();
  Serial.print("Connecting to "); Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while(WiFi.status() != WL_CONNECTED) {
    i++;
    delay(500);
    if(i >= 5) {
      preferences.end();
      ESP.restart(); 
    } else Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected"); 
  Serial.print("IP-address: "); Serial.println(WiFi.localIP());
}

void reconnect() {
  // loops until a connection the the MQTT-server is (re-)established
  while(!client.connected()) {
    Serial.print("Attempting MQTT-connection...");
    if(client.connect("ESP32Client", mqtt_server_usern, mqtt_server_password)) {
      Serial.println("connected.");
      subscribeToTopics();
      // for debugging
      client.publish("ES/W20/gruppe8/Debugging", "hello world");
      //client.subscribe("ES/W20/gruppe8/Debugging");
    } else {
      Serial.print("failed, rc = "); Serial.println(client.state());
      Serial.println("Trying again in 5 seconds...");
      delay(5000);
    }    
  }
}

void subscribeToTopics() {
  auto subscribeToTopic = [](char* topic) {
    if(client.subscribe(topic)) {
    Serial.print("Successfully subscribed to "); Serial.println(topic);
    } else {
      Serial.print("Could not subscribe to "); Serial.println(topic);
    }
  };
  subscribeToTopic(topic_motor);
  subscribeToTopic(topic_customText);
  subscribeToTopic(topic_motor_direction);
  subscribeToTopic(topic_newId);
}
