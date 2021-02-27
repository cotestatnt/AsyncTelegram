#include "Utilities.h"
#include "AsyncTelegram.h"

// bool getFingerPrint( uint8_t* oldFingerprint){
//     WiFiClientSecure client;
//     HTTPClient http;
//     String request((char *)0);
//     uint8_t new_fingerprint[20];

//     request = "https://www.grc.com/fingerprints.htm?chain=";
//     request += TELEGRAM_HOST;

// #if defined(ESP8266)
//     client.setInsecure();
// #endif

//     log_debug("[HTTP] begin...");
//     if(!WiFi.isConnected())
//         return false;

//     if (http.begin(client, request)) {
//         log_debug("[HTTP] GET...");
//         int httpCode = http.GET();
//         if (httpCode > 0) {
//             // HTTP header has been send and Server response header has been handled
//             if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
//                 char _fingerPrintStr[59];   // Example "F2:AD:29:9C:34:48:DD:8D:F4:CF:52:32:F6:57:33:68:2E:81:C1:90"
//                 char * pch;

//                 // get lenght of document (is -1 when Server sends no Content-Length header)
//                 int len = http.getSize();
//                 WiFiClient * stream = http.getStreamPtr();

//                 while(http.connected() && (len > 0 || len == -1)) {
//                     // Find table cell with our fingerprint label string (skip all unnecessary data from stream)
//                     if(stream->find("<td class=\"ledge\">api.telegram.org</td>")){
//                         // Find next table cell where updated string is placed
//                         if(stream->find("<td>") ){
//                             stream->readBytes(_fingerPrintStr, 59);
//                             http.end();
//                             break;
//                         }
//                         delay(1);
//                     }
//                 }

//                 // Split char _fingerPrintStr[] in uint8_t new_fingerprint[20]
//                 uint8_t i = 0;
//                 for (pch = strtok(_fingerPrintStr,":"); pch != NULL; pch = strtok(NULL,":"), i++) {
//                     if(pch != NULL)
//                         new_fingerprint[i] = (uint8_t)strtol(pch, NULL, 16);
//                 }
//                 #if DEBUG_MODE > 0
//                     Serial.printf("\nFingerprint updated:\n");
//                     Serial.printf("%02X", new_fingerprint[0]);
//                     for(uint8_t i=1; i<sizeof(new_fingerprint); i++)
//                         Serial.printf(":%02X", new_fingerprint[i]);
//                     Serial.println();
//                 #endif
//             }
//         }
//         else {
//             log_debug("GET... failed");
//             return false;
//         }
//         http.end();
//     }
//     else {
//         log_debug("Unable to connect to host \"https://www.grc.com\"");
//         return false;
//     }

// 	memcpy(oldFingerprint, new_fingerprint, sizeof(new_fingerprint));
//     return true;
// }


bool getFingerPrint( uint8_t* new_fingerprint){
    WiFiClientSecure client;
    const char* url = "GET /fingerprints.htm?chain=" TELEGRAM_HOST  " HTTP/1.1\r\n";
    client.setInsecure();

    log_debug("[HTTP] begin...");
    if(!WiFi.isConnected())
        return false;

     if (!client.connect("www.grc.com", 443)) {
        Serial.println("connection failed");
        return false;
    }

    // This will send the request to the server
    client.print(url);
    client.print("Host: www.grc.com\r\nConnection: close\r\n\r\n");
    unsigned long timeout = millis();
    while (client.available() == 0) {
        if (millis() - timeout > 5000) {
            Serial.println(">>> Client Timeout !");
            client.stop();
            return false;
        }
    }

    char _fingerPrintStr[60];
    _fingerPrintStr[59] = '\0';
    // Read all the lines of the reply from server and print them to Serial
    while(client.available()) {
         if(client.find("<td class=\"ledge\">api.telegram.org</td>")){
            // Find next table cell where updated string is placed
            if(client.find("<td>") ){
                client.readBytes(_fingerPrintStr, 59);
                client.stop();
                break;
            }
            yield();
        }
    }

    //Split char _fingerPrintStr[] in uint8_t new_fingerprint[20]
    uint8_t i = 0;
    char * pch;
    for (pch = strtok(_fingerPrintStr,":"); pch != NULL; pch = strtok(NULL,":"), i++) {
        if(pch != NULL)
            new_fingerprint[i] = (uint8_t)strtol(pch, NULL, 16);
    }

    #if DEBUG_MODE == 0
        Serial.printf("\nFingerprint updated:\n");
        Serial.printf("%02X", new_fingerprint[0]);
        for(uint8_t i=1; i<20; i++)
            Serial.printf(":%02X", new_fingerprint[i]);
        Serial.println();
    #endif

    Serial.println();
    Serial.println("closing connection");
    return true;
}



String toUTF8(String message)
{
	String converted;
	uint16_t i = 0;
	while (i < message.length()) {
		String subMessage(message[i]);
		if (message[i] != '\\') {
			converted += subMessage;
			i++;
		} else {
			// found "\"
			i++;
			if (i == message.length()) {
				// no more characters
				converted += subMessage;
			} else {
				subMessage += (String)message[i];
				if (message[i] != 'u') {
					converted += subMessage;
					i++;
				} else {
					//found \u escape code
					i++;
					if (i == message.length()) {
						// no more characters
						converted += subMessage;
					} else {
						uint8_t j = 0;
						while ((j < 4) && ((j + i) < message.length())) {
							subMessage += (String)message[i + j];
							j++;
						}
						i += j;
						String utf8;
						if (unicodeToUTF8(subMessage, utf8))
							converted += utf8;
						else
							converted += subMessage;
					}
				}
			}
		}
	}
	return converted;
}