/*
  Name:	      OTAupdate_ESP8266.ino
  Created:     15/01/2021
  Author:      Vladimir Bely <vlwwwwww@gmail.com>
  Description: an example that check for incoming messages
              and install rom update remotely.
*/
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266httpUpdate.h>
#include "AsyncTelegram.h"

AsyncTelegram myBot;

const char* ssid = "XXXXXXXXX";     // REPLACE mySSID WITH YOUR WIFI SSID
const char* pass = "XXXXXXXXX";     // REPLACE myPassword YOUR WIFI PASSWORD, IF ANY
const char* token = "XXXXXXXXX:XXXXXXXXXXXXXXXXXXXXXXX";   // REPLACE myToken WITH YOUR TELEGRAM BOT TOKEN


#define CANCEL  "CANCEL"
#define CONFIRM "FLASH_FW"

const char* firmware_version = __TIME__;
const char* fw_password = "update";

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  // initialize the Serial
  Serial.begin(115200);
  Serial.println("Starting TelegramBot...");

  WiFi.setAutoConnect(true);
  WiFi.mode(WIFI_STA);

  WiFi.begin(ssid, pass);
  delay(500);

  // We have to handle reboot manually after sync with TG server
  ESPhttpUpdate.rebootOnUpdate(false);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(100);
  }

  // Set the Telegram bot properies
  myBot.setUpdateTime(1000);
  myBot.setTelegramToken(token);

  // Check if all things are ok
  Serial.print("\nTest Telegram connection... ");
  myBot.begin() ? Serial.println("OK") : Serial.println("NOK");

  Serial.print("Bot name: @");
  Serial.println(myBot.getBotName());
}



void loop() {

  static uint32_t ledTime = millis();
  if (millis() - ledTime > 300) {
    ledTime = millis();
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }

  // a variable to store telegram message data
  TBMessage msg;

  // if there is an incoming message...
  if (myBot.getNewMessage(msg))
  {
    static String document;    
    switch (msg.messageType){
      case MessageDocument :
        {
            document = msg.document.file_path;
            if (msg.document.file_exists) {
    
              // Check file extension of received document (firmware must be .bin)
              if (document.indexOf(".bin") > -1 ) {
                String report = "Start firmware update\nFile name: ";
                report += msg.document.file_name;
                report += "\nFile size: ";
                report += msg.document.file_size;
    
                // Inform user and query for flash confirmation with password
                myBot.sendMessage(msg, report, "");
                myBot.sendMessage(msg, "Please insert password", "", true);   // Force reply == true
              }
            }
            else {
              myBot.sendMessage(msg, "File is unavailable. Maybe size limit 20MB was reached or file deleted");
            }
            break;
        }

      case MessageReply: 
        {
            String tgReply = msg.text;     
            // User has confirmed flash start with right password
            if ( tgReply.equals(fw_password) ) {
              myBot.sendMessage(msg, "Start flashing... please wait (~30/60s)");
              handleUpdate(msg, document);
              document.clear();
            }
            // Wrong password
            else {
              myBot.sendMessage(msg, "You have entered wrong password");
            }
            break;
        }

      default:
        {
            if (tgReply.equalsIgnoreCase("/version")) {
              String fw = "Version: " + String(firmware_version);
              myBot.sendMessage(msg, fw);
            }
            else {
              myBot.sendMessage(msg, "Send firmware binary file ###.bin");
            }
            break;
        }
    }
  }

}



// Install firmware update
void handleUpdate(TBMessage msg, String file_path) {

  // Create client for rom download
  WiFiClientSecure client;
  client.setInsecure();

  String report;

  Serial.print("Firmware path: ");
  Serial.println(file_path);

  t_httpUpdate_return ret = ESPhttpUpdate.update(client, file_path);
  Serial.println("Update done!");
  client.stop();

  switch (ret)
  {
    case HTTP_UPDATE_FAILED:
      report = "HTTP_UPDATE_FAILED Error (";
      report += ESPhttpUpdate.getLastError();
      report += "): ";
      report += ESPhttpUpdate.getLastErrorString();
      myBot.sendMessage(msg, report, "");
      break;

    case HTTP_UPDATE_NO_UPDATES:
      myBot.sendMessage(msg, "HTTP_UPDATE_NO_UPDATES");
      break;

    case HTTP_UPDATE_OK:
      myBot.sendMessage(msg, "UPDATE OK.\nRestarting...");

      // Wait until bot synced with telegram to prevent cyclic reboot
      while (!myBot.getUpdates()) {
        Serial.print(".");
        delay(50);
      }
      ESP.restart();

      break;
    default:
      break;
  }

}