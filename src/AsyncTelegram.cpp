// for using int_64 data
#define ARDUINOJSON_USE_LONG_LONG   1
#define ARDUINOJSON_DECODE_UNICODE  1

#include "AsyncTelegram.h"

#if DEBUG_ENABLE
#define debugJson(X, Y)  { log_debug(); Serial.println(); serializeJsonPretty(X, Y); Serial.println();}
#define errorJson(E)  { log_error(); Serial.println(); Serial.println(E);}
#else
#define debugJson(X, Y)
#define errorJson(E)
#endif

// get fingerprints from https://www.grc.com/fingerprints.htm
uint8_t default_fingerprint[20] = { 0xF2, 0xAD, 0x29, 0x9C, 0x34, 0x48, 0xDD, 0x8D, 0xF4, 0xCF, 0x52, 0x32, 0xF6, 0x57, 0x33, 0x68, 0x2E, 0x81, 0xC1, 0x90 };

AsyncTelegram::AsyncTelegram() {
    telegramServerIP.fromString(TELEGRAM_IP);
    httpData.payload.reserve(BUFFER_BIG);
    httpData.param.reserve(512);
    httpData.command.reserve(32);
    m_minUpdateTime = MIN_UPDATE_TIME;
#if defined(ESP8266)
    m_session = new BearSSL::Session;
    m_cert = new BearSSL::X509List(digicert);
#endif
}

AsyncTelegram::~AsyncTelegram() {};

// Set time via NTP, as required for x.509 validation
void AsyncTelegram::setClock(const char* TZ)
{
    // Set timezone and NTP servers
#ifdef ESP8266
    configTime(TZ, "time.google.com", "time.windows.com", "pool.ntp.org");
#elif defined(ESP32)
    configTzTime(TZ, "time.google.com", "time.windows.com", "pool.ntp.org");
#endif

  Serial.print("Waiting for NTP time sync: ");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(200);
    Serial.print(".");
    now = time(nullptr);
  }
}


bool AsyncTelegram::begin(){

  // Check NTP time, set default if not (Rome, Italy)
  time_t now = time(nullptr);
  if (now < 8 * 3600 * 2) 
    setClock("CET-1CEST,M3.5.0,M10.5.0/3");  

    telegramClient = new WiFiClientSecure;
    telegramClient->setTimeout(SERVER_TIMEOUT);
#if defined(ESP8266)
  #if USE_FINGERPRINT
    setFingerprint(default_fingerprint);
    telegramClient->setFingerprint(m_fingerprint);
  #else
    telegramClient->setBufferSizes(TCP_MSS, TCP_MSS);
    telegramClient->setSession(m_session);
    if(m_insecure)
        telegramClient->setInsecure();
    else
        telegramClient->setTrustAnchors(m_cert);
  #endif

#elif defined(ESP32)
    if(m_insecure)
        telegramClient->setInsecure();
    else
        telegramClient->setCACert(digicert);
    //Start Task with input parameter set to "this" class
    xTaskCreatePinnedToCore(
        this->httpPostTask,     //Function to implement the task
        "httpPostTask",         //Name of the task
        6500,                   //Stack size in words
        this,                   //Task input parameter
        10,                     //Priority of the task
        &taskHandler,           //Task handle.
        0                       //Core where the task should run
    );
#endif

    checkConnection();
    return getMe(m_user);
}


bool AsyncTelegram::reset(void){
    if(WiFi.status() != WL_CONNECTED ){
        Serial.println("No connection available.");
        return false;
    }
    log_debug("Reset connection\n");
    telegramClient->stop();

    delete telegramClient;

    httpData.waitingReply = false;
    httpData.payload.clear();
    httpData.command.clear();
    httpData.timestamp = millis();
    return begin();
}


void AsyncTelegram::sendCommand(const char* const&  command, const char* const& param)
{
#if defined(ESP32)
    if(httpData.waitingReply == false){
        httpData.waitingReply = true;
        httpData.command = command;
        httpData.param = param;
    }
#else
    postCommand(command, param, false);
#endif
}


// Blocking https POST to server (used with ESP8266)
bool AsyncTelegram::postCommand(const char* const& command, const char* const& param, bool blocking)
{
    bool connected = checkConnection();
    if(connected){
        String request;
        request.reserve(BUFFER_MEDIUM);
        request = "POST https://" TELEGRAM_HOST "/bot";
        request += m_token;
        request += "/";
        request += command;
        request += " HTTP/1.1" "\nHost: api.telegram.org" "\nConnection: keep-alive" "\nContent-Type: application/json";
        request += "\nContent-Length: ";
        request += strlen(param);
        request += "\n\n";
        request += param;
        telegramClient->print(request);
        httpData.waitingReply = true;

         // Blocking mode
        if (blocking) {
            while (telegramClient->connected()) {
                yield();
                String line = telegramClient->readStringUntil('\n');
                if (line == "\r") break;
            }
            // If there are incoming bytes available from the server, read them and print them:
            while (telegramClient->available()) {
                yield();
                httpData.payload  += (char) telegramClient->read();
            }
            httpData.waitingReply = false;
            DeserializationError error = deserializeJson(smallDoc, httpData.payload);
            return !error;
        }
    }
    return false;
}



void AsyncTelegram::httpPostTask(void *args){
#if defined(ESP32)

    Serial.print("\nStart http request task on core ");
    Serial.println(xPortGetCoreID());

    bool resetTask = false;
    AsyncTelegram *_this = (AsyncTelegram *) args;
    HTTPClient https;
    //https.setReuse(true);
    https.setTimeout(SERVER_TIMEOUT);

    for(;;) {
        //bool connected = _this->checkConnection();
        if (_this->httpData.command.length() > 0 &&  WiFi.status()== WL_CONNECTED ) {
            char url[256];
            sniprintf(url, 256, "https://%s/bot%s/%s", TELEGRAM_HOST, _this->m_token, _this->httpData.command.c_str() );
            https.begin(*_this->telegramClient, url);
            _this->httpData.waitingReply = true;
            if( _this->httpData.param.length() > 0 ){
                https.addHeader("Host", TELEGRAM_HOST, false, false);
                https.addHeader("Connection", "keep-alive", false, false);
                https.addHeader("Content-Type", "application/json", false, false);
                https.addHeader("Content-Length", String(_this->httpData.param.length()), false, false );
            }

            int httpCode = https.POST(_this->httpData.param);
            if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
                // HTTP header has been send and Server response header has been handled
                _this->httpData.payload  = https.getString();
                _this->httpData.timestamp = millis();

                if(https.header("Connection").equalsIgnoreCase("close")){
                    resetTask = true;  // Force reset connection
                }
            }
            else {
                log_error("\nHTTPS error: %d\n", httpCode);
                //resetTask = true;    // Force reset connection
            }
            _this->httpData.command.clear();
            _this->httpData.param.clear();
            https.end();

            UBaseType_t uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
            //Serial.printf("Task free memory: %5d\n", (uint16_t)uxHighWaterMark);
            log_debug("FreeHeap: %6d, MaxBlock: %6d\n", heap_caps_get_free_size(0), heap_caps_get_largest_free_block(0));

            if (resetTask) {
                log_debug("\nGoing to delete this task and restart");
                break;
            }
        }
        delay(1);
    }
	delay(10);
    _this->httpData.timestamp = 0;  // Force reset on next call
    vTaskDelete(NULL);
#endif
}


bool AsyncTelegram::getUpdates(){
    // No response from Telegram server for a long time
    if(millis() - httpData.timestamp > 10*m_minUpdateTime) {
        Serial.println("Reset connection");
        reset();
    }

    // Send message to Telegram server only if enough time has passed since last
    if(millis() - m_lastUpdateTime > m_minUpdateTime){
        m_lastUpdateTime = millis();

        // If previuos reply from server was received
        if( httpData.waitingReply == false) {
            String param((char *)0);
            param.reserve(64);
            DynamicJsonDocument root(BUFFER_SMALL);
            root["limit"] = 1;
            // polling timeout: add &timeout=<seconds. zero for short polling.
            root["timeout"] = 3;
            root["allowed_updates"] = "message,callback_query";
            if (m_lastUpdate != 0) {
                root["offset"] = m_lastUpdate;
            }
            serializeJson(root, param);
            sendCommand("getUpdates", param.c_str());
        }
    }

    #if defined(ESP8266)

    // If there are incoming bytes available from the server, read them and store:
    while (telegramClient->available() ){
        httpData.payload += (char) telegramClient->read();
    }

    // We have a message, parse data received
    if(httpData.payload.length() != 0) {
        httpData.payload = httpData.payload.substring(httpData.payload.indexOf("{\"ok\":"), httpData.payload.length());
        return true;
    }
    #else
        return ! httpData.waitingReply;
    #endif
    return false;
}



// Parse message received from Telegram server
MessageType AsyncTelegram::getNewMessage(TBMessage &message )
{
    message.messageType = MessageNoData;
    getUpdates();
    // We have a message, parse data received
    if( httpData.payload.length() > 0 ) {

        DynamicJsonDocument root(BUFFER_BIG);
        deserializeJson(root, httpData.payload);
        httpData.payload.clear();
        httpData.timestamp = millis();
        httpData.waitingReply = false;

        bool ok = root["ok"];
        if (!ok) {
            errorJson(httpData.payload);
            return MessageNoData;
        }
        uint32_t updateID = root["result"][0]["update_id"];
        if (updateID == 0){
            return MessageNoData;
        }
        m_lastUpdate = updateID + 1;

        debugJson(root, Serial);

        if(root["result"][0]["callback_query"]["id"]){
            // this is a callback query
            message.callbackQueryID   = root["result"][0]["callback_query"]["id"];
            message.chatId            = root["result"][0]["callback_query"]["message"]["chat"]["id"];
            message.sender.id         = root["result"][0]["callback_query"]["from"]["id"];
            message.sender.username   = root["result"][0]["callback_query"]["from"]["username"];
            message.sender.firstName  = root["result"][0]["callback_query"]["from"]["first_name"];
            message.sender.lastName   = root["result"][0]["callback_query"]["from"]["last_name"];
            message.messageID         = root["result"][0]["callback_query"]["message"]["message_id"];
            message.text              = root["result"][0]["callback_query"]["message"]["text"].as<String>();
            message.date              = root["result"][0]["callback_query"]["message"]["date"];
            message.chatInstance      = root["result"][0]["callback_query"]["chat_instance"];
            message.callbackQueryData = root["result"][0]["callback_query"]["data"];
            message.messageType       = MessageQuery;
            m_inlineKeyboard.checkCallback(message);
        }
        else if(root["result"][0]["message"]["message_id"]){
            // this is a message
            message.messageID        = root["result"][0]["message"]["message_id"];
            message.chatId           = root["result"][0]["message"]["chat"]["id"];
            message.sender.id        = root["result"][0]["message"]["from"]["id"];
            message.sender.username  = root["result"][0]["message"]["from"]["username"];
            message.sender.firstName = root["result"][0]["message"]["from"]["first_name"];
            message.sender.lastName  = root["result"][0]["message"]["from"]["last_name"];
            message.group.title      = root["result"][0]["message"]["chat"]["title"];
            message.date             = root["result"][0]["message"]["date"];

            if(root["result"][0]["message"]["location"]){
                // this is a location message
                message.location.longitude = root["result"][0]["message"]["location"]["longitude"];
                message.location.latitude = root["result"][0]["message"]["location"]["latitude"];
                message.messageType = MessageLocation;
            }
            else if(root["result"][0]["message"]["contact"]){
                // this is a contact message
                message.contact.id          = root["result"][0]["message"]["contact"]["user_id"];
                message.contact.firstName   = root["result"][0]["message"]["contact"]["first_name"];
                message.contact.lastName    = root["result"][0]["message"]["contact"]["last_name"];
                message.contact.phoneNumber = root["result"][0]["message"]["contact"]["phone_number"];
                message.contact.vCard       = root["result"][0]["message"]["contact"]["vcard"];
                message.messageType = MessageContact;
            }
            else if(root["result"][0]["message"]["document"]){
                // this is a document message
                message.document.file_id      = root["result"][0]["message"]["document"]["file_id"];
                message.document.file_name    = root["result"][0]["message"]["document"]["file_name"];
                message.text                  = root["result"][0]["message"]["caption"].as<String>();
                message.document.file_exists  = getFile(message.document);
                message.messageType           = MessageDocument;
            }
            else if(root["result"][0]["message"]["reply_to_message"]){
                // this is a reply to message
                message.text        = root["result"][0]["message"]["text"].as<String>();
                message.messageType = MessageReply;
            }
            else if (root["result"][0]["message"]["text"]) {
                // this is a text message
                message.text        = root["result"][0]["message"]["text"].as<String>();
                message.messageType = MessageText;
            }
        }
        return message.messageType;
    }
    return MessageNoData;   // waiting for reply from server
}


// Blocking getMe function (we wait for a reply from Telegram server)
bool AsyncTelegram::getMe(TBUser &user)
{
    // getMe has top be blocking (wait server reply)
    if (!postCommand("getMe", "", true))
       return false;

    bool ok = smallDoc["ok"];
    if (!ok) {
        errorJson(httpData.payload);
        return MessageNoData;
    }
    debugJson(smallDoc, Serial);
    httpData.payload.clear();
    httpData.timestamp = millis();

    user.id           = smallDoc["result"]["id"];
    user.isBot        = smallDoc["result"]["is_bot"];
    user.firstName    = smallDoc["result"]["first_name"];
    user.username     = smallDoc["result"]["username"];
    user.lastName     = smallDoc["result"]["last_name"];
    user.languageCode = smallDoc["result"]["language_code"];

    userName = user.username;
    return true;
}


bool AsyncTelegram::getFile(TBDocument &doc)
{
    // getFile has to be blocking (wait server reply)
    char cmd[64];
    strcpy(cmd,  "getFile?file_id=");
    strcat(cmd, doc.file_id);

    if (!postCommand(cmd, "", true))
       return false;

    bool ok = smallDoc["ok"];
    if (!ok) {
        errorJson(httpData.payload);
        return MessageNoData;
    }
    debugJson(smallDoc, Serial);
    httpData.payload.clear();
    httpData.timestamp = millis();
    strcpy(doc.file_path, "https://api.telegram.org/file/bot" );
    strcat(doc.file_path, m_token);
    strcat(doc.file_path, "/");
    strcat(doc.file_path, smallDoc["result"]["file_path"]);
    doc.file_size  = smallDoc["result"]["file_size"].as<long>();
    return true;
}



void AsyncTelegram::sendMessage(const TBMessage &msg, const char* message, String keyboard)
{
    if (strlen(message) == 0)
        return;

    DynamicJsonDocument root(BUFFER_BIG);
	// Backward compatibility
	root["chat_id"] = msg.sender.id != 0 ? msg.sender.id : msg.chatId;
    root["text"] = message;

    if(msg.isMarkdownEnabled)
        root["parse_mode"] = "MarkdownV2";

    if(msg.isHTMLenabled)
        root["parse_mode"] = "HTML";

    if(msg.disable_notification)
        root["disable_notification"] = true;

    if (keyboard.length() != 0) {
        DynamicJsonDocument doc(512);
        deserializeJson(doc, keyboard);
        JsonObject myKeyb = doc.as<JsonObject>();
        root["reply_markup"] = myKeyb;
        if(msg.force_reply) {
            root["reply_markup"]["selective"] = true,
            root["reply_markup"]["force_reply"] = true;
        }
    }

    String param;
    serializeJson(root, param);
    sendCommand("sendMessage", param.c_str());
    debugJson(root, Serial);
}


void AsyncTelegram::sendTo(const int32_t userid, String &message, String keyboard) {
    TBMessage msg;
    msg.chatId = userid;
    return sendMessage(msg, message.c_str(), "");
}


void AsyncTelegram::sendPhotoByUrl(const uint32_t& chat_id,  const String& url, const String& caption)
{
    if (url.length() == 0)
        return;
	smallDoc.clear();
    smallDoc["chat_id"] = chat_id;
    smallDoc["photo"] = url;
    smallDoc["caption"] = caption;

    char param[256];
    serializeJson(smallDoc, param, 256);
    sendCommand("sendPhoto", param);
    debugJson(smallDoc, Serial);
}


void AsyncTelegram::sendToChannel(const char* &channel, String &message, bool silent) {
    if (message.length() == 0)
        return;
    DynamicJsonDocument root(BUFFER_MEDIUM);
    root["chat_id"] = channel;
    root["text"] = message;
    if(silent)
        root["silent"] = true;

    String param;
    serializeJson(root, param);
    sendCommand("sendMessage", param.c_str());
    debugJson(root, Serial);
}


void AsyncTelegram::endQuery(const TBMessage &msg, const char* message, bool alertMode)
{
    if (strlen(msg.callbackQueryID) == 0)
        return;
	smallDoc.clear();
    smallDoc["callback_query_id"] =  msg.callbackQueryID;
    if (strlen(message) != 0) {
        smallDoc["text"] = message;
        if (alertMode)
            smallDoc["show_alert"] = true;
        else
            smallDoc["show_alert"] = false;
    }
    char param[BUFFER_SMALL];
    serializeJson(smallDoc, param, BUFFER_SMALL);
    sendCommand("answerCallbackQuery", param);
}


void AsyncTelegram::removeReplyKeyboard(const TBMessage &msg, const char* message, bool selective)
{
	smallDoc.clear();
    smallDoc["remove_keyboard"] = true;
    if (selective) {
        smallDoc["selective"] = true;
    }
    char command[128];
    serializeJson(smallDoc, command, 128);
    sendMessage(msg, message, command);
}

void AsyncTelegram::editMessageReplyMarkup(TBMessage &msg, String keyboard) // keyboard value defaulted to ""
{
    if (sizeof(msg) == 0)
        return;


    DynamicJsonDocument root(BUFFER_SMALL);

    root["chat_id"] = msg.chatId;
    root["message_id"] = msg.messageID;

    if (msg.isMarkdownEnabled)
        root["parse_mode"] = "Markdown";

    if (keyboard.length() != 0) {
        DynamicJsonDocument doc(512);
        deserializeJson(doc, keyboard);
        JsonObject myKeyb = doc.as<JsonObject>();
        root["reply_markup"] = myKeyb;
    }

    String buffer;
    serializeJson(root, buffer);
    sendCommand("editMessageReplyMarkup", buffer.c_str());
    debugJson(root, Serial);
}

void AsyncTelegram::editMessageReplyMarkup(TBMessage &msg, InlineKeyboard &keyboard)
{
    m_inlineKeyboard = keyboard;
    return editMessageReplyMarkup(msg, keyboard.getJSON());
}


bool AsyncTelegram::serverReply(const char* const& replyMsg)
{
	smallDoc.clear();
    deserializeJson(smallDoc, replyMsg);
    bool ok = smallDoc["ok"];
    if (!ok) {
        errorJson(replyMsg);
        return false;
    }
    debugJson(smallDoc, Serial);
    return true;
}


bool AsyncTelegram::checkConnection()
{
    if(WiFi.status() != WL_CONNECTED )
        return false;

    // Start connection with Telegramn server (if necessary)
    if(! telegramClient->connected() ){
        // try to connect
        if (!telegramClient->connect(telegramServerIP, TELEGRAM_PORT)) {            // no way, try to connect with hostname
            if (!telegramClient->connect(TELEGRAM_HOST, TELEGRAM_PORT))
                Serial.printf("Unable to connect to Telegram server\n");
            else {
                log_debug("\nConnected using Telegram hostname\n");
			}
        }
        else log_debug("\nConnected using Telegram ip address\n");
    }
    return telegramClient->connected();
}

// bool AsyncTelegram::checkConnection(){
//     // Start connection with Telegramn server if necessary)
//     if(! telegramClient->connected()){
//         // check for using symbolic URLs
//         BearSSL::X509List cert(digicert);
//         telegramClient->setTrustAnchors(&cert);
//         if (m_useDNS) {
//             // try to connect with URL
//             if (!telegramClient->connect(TELEGRAM_HOST, TELEGRAM_PORT)) {
//                 if (!telegramClient->connect(telegramServerIP, TELEGRAM_PORT)) {
//                     log_debug("\nUnable to connect to Telegram server\n");
//                 }
//                 else {
//                     log_debug("\nConnected using fixed IP\n");
//                     telegramClient->setTimeout(SERVER_TIMEOUT);
//                     useDNS(false);
//                 }
//             }
//             else {
//                 log_debug("\nConnected using DNS\n");
//                 telegramClient->setTimeout(SERVER_TIMEOUT);
//             }
//         }
//         else {
//             // try to connect with fixed IP
//             IPAddress telegramServerIP; // (149, 154, 167, 198);
//             telegramServerIP.fromString(TELEGRAM_IP);
//             if (!telegramClient->connect(telegramServerIP, TELEGRAM_PORT)) {
//                 log_debug("\nUnable to connect to Telegram server\n");
//             }
//             else {
//                 log_debug("\nConnected using fixed IP\n");
//                 telegramClient->setTimeout(SERVER_TIMEOUT);
//             }
//         }
//     }
//     return telegramClient->connected();
// }

#if USE_FINGERPRINT
bool AsyncTelegram::updateFingerPrint(void){
    WiFiClientSecure client;
    HTTPClient http;
    String request((char *)0);

    uint8_t new_fingerprint[20];

    request = "https://www.grc.com/fingerprints.htm?chain=";
    request += TELEGRAM_HOST;

#if defined(ESP8266)
    client.setInsecure();
#endif

    log_debug("\n[HTTP] begin...");
    if(!WiFi.isConnected())
        return false;

    if (http.begin(client, request)) {
        log_debug("\n[HTTP] GET...");
        int httpCode = http.GET();
        if (httpCode > 0) {
            // HTTP header has been send and Server response header has been handled
            if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
                char _fingerPrintStr[59];   // Example "F2:AD:29:9C:34:48:DD:8D:F4:CF:52:32:F6:57:33:68:2E:81:C1:90"
                char * pch;

                // get lenght of document (is -1 when Server sends no Content-Length header)
                int len = http.getSize();
                WiFiClient * stream = http.getStreamPtr();

                while(http.connected() && (len > 0 || len == -1)) {
                    // Find table cell with our fingerprint label string (skip all unnecessary data from stream)
                    if(stream->find("<td class=\"ledge\">api.telegram.org</td>")){
                        // Find next table cell where updated string is placed
                        if(stream->find("<td>") ){
                            stream->readBytes(_fingerPrintStr, 59);
                            http.end();
                            break;
                        }
                        delay(1);
                    }
                }

                // Split char _fingerPrintStr[] in uint8_t new_fingerprint[20]
                uint8_t i = 0;
                for (pch = strtok(_fingerPrintStr,":"); pch != NULL; pch = strtok(NULL,":"), i++) {
                    if(pch != NULL)
                        new_fingerprint[i] = (uint8_t)strtol(pch, NULL, 16);
                }
                #if DEBUG_MODE > 0
                    Serial.printf("\nFingerprint updated:\n");
                    Serial.printf("%02X", new_fingerprint[0]);
                    for(uint8_t i=1; i<sizeof(new_fingerprint); i++)
                        Serial.printf(":%02X", new_fingerprint[i]);
                    Serial.println();
                #endif
            }
        }
        else {
            log_error("GET... failed");
            return false;
        }
        http.end();
    }
    else {
        log_error("\nUnable to connect to host \"https://www.grc.com\"");
        return false;
    }

    setFingerprint(new_fingerprint);
    return true;
}
#endif


bool AsyncTelegram::sendPhotoByFile(const uint32_t& chat_id, const String& fileName, fs::FS& filesystem)
{
    return sendMultipartFormData("sendPhoto", chat_id, fileName, "image/jpeg", "photo", filesystem );
}

bool AsyncTelegram::sendMultipartFormData( const String& command,  const uint32_t& chat_id, const String& fileName,
                                           const char* contentType, const char* binaryPropertyName, fs::FS& fs )
{

    #define BOUNDARY            "----WebKitFormBoundary7MA4YWxkTrZu0gW"
    #define END_BOUNDARY        "\r\n--" BOUNDARY "--\r\n"

    File myFile = fs.open("/" + fileName, "r");
    if (!myFile) {
        Serial.printf("Failed to open file %s\n", fileName.c_str());
        return false;
    }

    if (telegramClient->connected()) {
        String formData;
        formData += "--" BOUNDARY;
        formData += "\r\nContent-disposition: form-data; name=\"chat_id\"\r\n\r\n";
        formData += String(chat_id);
        formData += "\r\n--" BOUNDARY;
        formData += "\r\nContent-disposition: form-data; name=\"";
        formData += binaryPropertyName;
        formData += "\"; filename=\"";
        formData += fileName;
        formData += "\"\r\nContent-Type: ";
        formData += contentType;
        formData += "\r\n\r\n";

        String uri = "POST /bot";
        uri += m_token;
        uri += "/";
        uri += command;
        uri += " HTTP/1.1";
        // Send POST request to host
        telegramClient->println(uri);
        // Headers
        telegramClient->println("Host: " TELEGRAM_HOST);
        telegramClient->print("Content-Length: ");
        int contentLength = myFile.size() + formData.length() + String(END_BOUNDARY).length();
        telegramClient->println(String(contentLength));
        telegramClient->print("Content-Type: multipart/form-data; boundary=");
        telegramClient->println(BOUNDARY);
        telegramClient->println();
        // Body of request
        telegramClient->print(formData);

        uint8_t buff[BLOCK_SIZE];
        uint16_t count = 0;
        while (myFile.available()) {
            yield();
            buff[count++] = myFile.read();
            if (count == BLOCK_SIZE ) {
                Serial.println(F("Sending binary photo full buffer"));
                telegramClient->write((const uint8_t *)buff, BLOCK_SIZE);
                count = 0;
            }
        }
        if (count > 0) {
            Serial.println(F("Sending binary photo remaining buffer"));
            telegramClient->write((const uint8_t *)buff, count);
        }

        telegramClient->print(END_BOUNDARY);
        myFile.close();
    }
    else {
        Serial.println("\nError: client not connected");
        return false;
    }
    return true;
}
