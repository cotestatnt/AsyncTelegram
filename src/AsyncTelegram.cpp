#include "AsyncTelegram.h"
#ifdef ESP32
#include "esp_task_wdt.h"
#endif

#if DEBUG_ENABLE
#define debugJson(X, Y)  { log_debug(); Serial.println(); serializeJsonPretty(X, Y); Serial.println();}
#define errorJson(E)  { log_error(); Serial.println(); Serial.println(E);}
#else
#define debugJson(X, Y)
#define errorJson(E)
#endif

// get fingerprints from https://www.grc.com/fingerprints.htm
uint8_t defaulFingerprint[20] = { 0xF2, 0xAD, 0x29, 0x9C, 0x34, 0x48, 0xDD, 0x8D, 0xF4, 0xCF, 0x52, 0x32, 0xF6, 0x57, 0x33, 0x68, 0x2E, 0x81, 0xC1, 0x90 };

AsyncTelegram::AsyncTelegram()
{
    telegramServerIP.fromString(TELEGRAM_IP);
    httpData.payload.reserve(BUFFER_BIG);
    httpData.param.reserve(512);
    httpData.command.reserve(32);
    m_minUpdateTime = MIN_UPDATE_TIME;
}

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


bool AsyncTelegram::begin()
{
    telegramClient = new WiFiClientSecure;
#if USE_FINGERPRINT
    setFingerprint(defaulFingerprint);   // set the default fingerprint
    telegramClient->setFingerprint(m_fingerprint);
    telegramClient->setBufferSizes(TCP_MSS, TCP_MSS);
#else
    #if defined(ESP8266)
        telegramClient->setBufferSizes(TCP_MSS, TCP_MSS);
        telegramClient->setSession(&m_session);
    #elif defined(ESP32)
	    telegramClient->setCACert(digicert);
        //telegramClient->setInsecure();
    #endif
#endif
    telegramClient->connect(TELEGRAM_HOST, TELEGRAM_PORT);
    //checkConnection();
    bool isMe = getMe(m_user);

#if defined(ESP32)
    disableCore0WDT();
    //Start Task with input parameter set to "this" class
    xTaskCreatePinnedToCore(
        this->postCommandTask,  //Function to implement the task
        "httpTask",             //Name of the task
        5000,                   //Stack size in words
        this,                   //Task input parameter
        10,                      //Priority of the task
        &taskHandler,           //Task handle.
        0                       //Core where the task should run
    );
#endif
    return isMe;
}


void AsyncTelegram::reset(void)
{
    if(WiFi.status() != WL_CONNECTED ){
        Serial.println("No connection available.");
        return;
    }
    log_debug("Reset connection\n");
    telegramClient->stop();

    httpData.busy = false;
    httpData.payload.clear();
    httpData.command.clear();
    httpData.timestamp = millis();

    if(checkConnection()) {
#if defined(ESP32)
    log_debug("Task state: %d\n", eTaskGetState( taskHandler ));
    xTaskCreatePinnedToCore(this->postCommandTask, "httpTask", 5000, this, 10, &taskHandler, 0);
#endif
    }

}



bool AsyncTelegram::checkConnection()
{
    if(WiFi.status() != WL_CONNECTED )
        return false;

    // Start connection with Telegramn server (if necessary)
    if(! telegramClient->connected() ){
#if defined(ESP8266)
        BearSSL::X509List cert(digicert);
        telegramClient->setTrustAnchors(&cert);
#elif defined(ESP32)
        telegramClient->setCACert(digicert);
        //telegramClient->verify((const char*) m_fingerprint, TELEGRAM_HOST );
#endif
        // try to connect
        if (!telegramClient->connect(telegramServerIP, TELEGRAM_PORT)) {            // no way, try to connect with hostname
            if (!telegramClient->connect(TELEGRAM_HOST, TELEGRAM_PORT))
                Serial.printf("\n\nUnable to connect to Telegram server\n");
            else
                log_debug("\nConnected using Telegram hostname\n");
        }
        else log_debug("\nConnected using Telegram ip address\n");
    }
    return telegramClient->connected();
}



// helper function used to select the properly working mode with ESP8266/ESP32
void AsyncTelegram::sendCommand(const char* const&  command, const char* const& param)
{
#if defined(ESP32)
    // Check if http task is busy before set new command
    if( !httpData.busy && telegramClient->connected()) {
        httpData.command = command;
        httpData.param = param;
    }
#else
    postCommand(command, param, false);
#endif
}



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
        httpData.busy = true;

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
            httpData.busy = false;
            //return httpData.payload ;
            DeserializationError error = deserializeJson(smallDoc, httpData.payload);
            return !error;
        }
    }
    return false;
}

// Https POST command runned in a separate task/separate core (ESP32 only)
#if defined(ESP32)
void AsyncTelegram::postCommandTask(void *args){
    log_debug("\nStarted http request task on core %d\n", xPortGetCoreID());
    AsyncTelegram *_this = (AsyncTelegram *) args;
    bool resetTask = false;
    UBaseType_t uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
    vTaskDelay(100);

    for(;;) {
        vTaskDelay(10);
        // send a command to telegram server
        //if(WiFi.status() == WL_CONNECTED ) {
            if (_this->httpData.command.length() && _this->telegramClient->connected()) {
                uint32_t t1 = millis();
                _this->httpData.busy = true;
                String txBuffer;
                int plen = _this->httpData.param.length();
                txBuffer.reserve(512 + plen );
                txBuffer = "POST https://" TELEGRAM_HOST "/bot";
                txBuffer += _this->m_token;
                txBuffer += "/";
                txBuffer += _this->httpData.command;
                txBuffer += " HTTP/1.1\nHost: api.telegram.org" "\nConnection: keep-alive" "\nContent-Type: application/json";
                txBuffer += "\nContent-Length: ";
                txBuffer += String(plen);
                txBuffer += "\n\n";
                txBuffer += _this->httpData.param;
                _this->telegramClient->print(txBuffer);

                _this->httpData.command.clear();
                _this->httpData.param.clear();

                // Skip headers
                while (_this->telegramClient->available()) {
                    vTaskDelay(1);
                    String line = _this->telegramClient->readStringUntil('\n');
                    // End of headers
                    if (line == "\r")  break;
                    // Server has closed the connection from remote, force restart connection.
                    if (line.indexOf("Connection: close") > -1) {
                        log_debug("Connection closed from server side\n");
                        resetTask = true;
                    }
                }
                // Save the payload data
                _this->httpData.payload.clear();
                while (_this->telegramClient->available()) {
                    esp_task_wdt_reset();
                    _this->httpData.payload  += (char) _this->telegramClient->read();
                }

                uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
                Serial.printf("Time: % 4lu, Heap: %u, MaxFree: %u, Task stack: %d\n", millis() - t1,
                            heap_caps_get_free_size(0), heap_caps_get_largest_free_block(0), uxHighWaterMark);
                _this->httpData.busy = false;
            }

            // Something wrong, maybe memory leak or connection closed from server
            if (uxHighWaterMark < 2800 || resetTask) {
                log_debug("\nGoing to delete this task and restart");
                //Serial.println("\nGoing to delete this task and restart");
                break;
            }
        //}
    }

    _this->httpData.timestamp = 0;  // Force reset on next call
    vTaskDelete(NULL);
}
#endif

bool AsyncTelegram::getUpdates()
{
     // No response from Telegram server for a long time
    if(millis() - httpData.timestamp > 10*m_minUpdateTime) {
        Serial.println("Reset connection");
        reset();
    }

    // Send message to Telegram server only if enough time has passed since last
    static uint32_t lastUpdateTime;
    if(millis() - lastUpdateTime > m_minUpdateTime){
        lastUpdateTime = millis();

        // If previuos reply from server was received, we are free to send another one
        if( ! httpData.busy) {
            char param[128];
            DynamicJsonDocument root(BUFFER_SMALL);
            root["limit"] = 1;
            // polling timeout: add &timeout=<seconds. zero for short polling.
            root["timeout"] = 0;
            root["allowed_updates"] = "callback_query";
            if (m_nextUpdateId != 0) {
                root["offset"] = m_nextUpdateId;
            }
            serializeJson(root, param, 128);
            sendCommand("getUpdates", param);
        }
    }

// No task manager for esp8266 :(
#if defined(ESP8266)
    if(httpData.busy ) {
        // If there are incoming bytes available from the server, read them and store:
        static uint8_t brOpen = 0;
        static uint8_t brClose = 0;
        //uint16_t bytesToRead = telegramClient->available();
        //for(uint16_t i=0; i<bytesToRead; i++){
        while (telegramClient->available() ){
            ESP.wdtFeed();
            char ch = (char) telegramClient->read();
            if(ch == '{') brOpen++;
            if(ch == '}') brClose++;
            if(brOpen >= 1)
                httpData.payload += ch;

            if(brClose == brOpen) {
                httpData.busy = false;
                brOpen = 0;
                brClose = 0;
            }
        }
    }
#endif

    return !httpData.busy;
}



// Parse message received from Telegram server
MessageType AsyncTelegram::getNewMessage(TBMessage &message )
{
    message.messageType = MessageNoData;

    // Check incoming messages from server (if enough time has elapsed since last)
    getUpdates() ;

    // Check if server reply is ready to be parsed
    if(!httpData.busy && httpData.payload.length() > 20) {

        // We have a message, parse data received
        DynamicJsonDocument root(BUFFER_BIG);
        DeserializationError error = deserializeJson(root, httpData.payload);

        httpData.payload.clear();
        httpData.command.clear();
        httpData.timestamp = millis();

        if (error ) {
            errorJson(httpData.payload);
            return MessageNoData;
        }
        debugJson(root, Serial);

        JsonArray array = root["result"].as<JsonArray>();
        for(JsonVariant result : array) {
            int32_t updateID  = result["update_id"];
            if(updateID < m_nextUpdateId) {
                // Message already parsed
                return MessageNoData;
            }
            m_nextUpdateId = updateID + 1;

            if(result["callback_query"]["id"]){
                // this is a callback query
                message.callbackQueryID   = result["callback_query"]["id"];
                message.sender.id         = result["callback_query"]["from"]["id"];
                message.sender.username   = result["callback_query"]["from"]["username"];
                message.sender.firstName  = result["callback_query"]["from"]["first_name"];
                message.sender.lastName   = result["callback_query"]["from"]["last_name"];
                message.messageID         = result["callback_query"]["message"]["message_id"];
                message.text              = result["callback_query"]["message"]["text"];
                message.date              = result["callback_query"]["message"]["date"];
                message.chatInstance      = result["callback_query"]["chat_instance"];
                message.callbackQueryData = result["callback_query"]["data"];
                message.messageType       = MessageQuery;
                m_inlineKeyboard.checkCallback(message);
            }

            else if(result["message"]["message_id"]){
                // this is a message
                message.messageID        = result["message"]["message_id"];
                message.sender.id        = result["message"]["from"]["id"];
                message.sender.username  = result["message"]["from"]["username"];
                message.sender.firstName = result["message"]["from"]["first_name"];
                message.sender.lastName  = result["message"]["from"]["last_name"];
                message.group.id         = result["message"]["chat"]["id"];
                message.group.title      = result["message"]["chat"]["title"];
                message.date             = result["message"]["date"];

                if(result["message"]["reply_to_message"]){
                    // this is a reply to message
                    message.text        = result["message"]["text"];
                    message.messageType = MessageReply;
                }
                else if(result["message"]["location"]){
                    // this is a location message
                    message.location.longitude = result["message"]["location"]["longitude"];
                    message.location.latitude = result["message"]["location"]["latitude"];
                    message.messageType = MessageLocation;
                }
                else if(result["message"]["contact"]){
                    // this is a contact message
                    message.contact.id          = result["message"]["contact"]["user_id"];
                    message.contact.firstName   = result["message"]["contact"]["first_name"];
                    message.contact.lastName    = result["message"]["contact"]["last_name"];
                    message.contact.phoneNumber = result["message"]["contact"]["phone_number"];
                    message.contact.vCard       = result["message"]["contact"]["vcard"];
                    message.messageType = MessageContact;
                }
                else if(result["message"]["document"]){
                    // this is a document message
                    message.document.file_id      = result["message"]["document"]["file_id"];
                    message.document.file_name    = result["message"]["document"]["file_name"];
                    message.text                  = result["message"]["caption"];
                    Serial.println("Call getFile()");
                    message.document.file_exists  = getFile(message.document);
                    message.messageType           = MessageDocument;
                }
                else if (result["message"]["text"]) {
                    // this is a text message
                    message.text        = result["message"]["text"];
                    message.messageType = MessageText;
                }
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

    m_botName = (char *) realloc(m_botName, strlen(user.username)+1);
    strcpy(m_botName, user.username );
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

void AsyncTelegram::sendMessage(const TBMessage &msg, const char* message, String keyboard, bool forceReply)
{
    if (strlen(message) == 0)
        return;

    DynamicJsonDocument root(BUFFER_BIG);
    root["chat_id"] = msg.sender.id;
    root["text"] = message;
    if (msg.isMarkdownEnabled)
        root["parse_mode"] = "Markdown";

    if(msg.disable_notification)
        root["disable_notification"] = true;

    if (keyboard.length() != 0 || forceReply) {
        DynamicJsonDocument doc(BUFFER_SMALL + keyboard.length());
        deserializeJson(doc, keyboard);
        JsonObject myKeyb = doc.as<JsonObject>();
        root["reply_markup"] = myKeyb;
        if(forceReply) {
            root["reply_markup"]["selective"] = true,
            root["reply_markup"]["force_reply"] = true;
        }
    }

    String param;
    serializeJson(root, param);
    sendCommand("sendMessage", param.c_str());
    debugJson(root, Serial);
}

void AsyncTelegram::sendToChannel(const char* &channel, String &message, bool silent)
{
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

    //DynamicJsonDocument root(BUFFER_SMALL);
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
    //DynamicJsonDocument root(BUFFER_SMALL);
    smallDoc["remove_keyboard"] = true;
    if (selective) {
        smallDoc["selective"] = true;
    }
    char command[128];
    serializeJson(smallDoc, command, 128);
    sendMessage(msg, message, command);
}

bool AsyncTelegram::serverReply(const char* const& replyMsg)
{
    //DynamicJsonDocument root(BUFFER_SMALL);
    deserializeJson(smallDoc, replyMsg);
    bool ok = smallDoc["ok"];
    if (!ok) {
        errorJson(replyMsg);
        return false;
    }
    debugJson(smallDoc, Serial);
    return true;
}

void AsyncTelegram::sendPhotoByUrl(const uint32_t& chat_id,  const String& url, const String& caption)
{
    if (url.length() == 0)
        return;
    //DynamicJsonDocument root(BUFFER_MEDIUM);
    smallDoc["chat_id"] = chat_id;
    smallDoc["photo"] = url;
    smallDoc["caption"] = caption;

    char param[256];
    serializeJson(smallDoc, param, 256);
    sendCommand("sendPhoto", param);
    debugJson(smallDoc, Serial);
}

bool AsyncTelegram::sendPhotoByFile(const uint32_t& chat_id, const String& fileName, fs::FS& filesystem)
{
    m_filesystem = &filesystem;
#if defined(ESP8266)
    return sendMultipartFormData("sendPhoto", chat_id, fileName, "image/jpeg", "photo" );
#else

    if(!httpData.busy) {
        // Suspend main task
        vTaskSuspend(taskHandler);
        TaskHandle_t handleUpload;

        // Prepare data for task
        m_uploadFile.chat_id =  chat_id;
        m_uploadFile.fileName = fileName;
        m_uploadFile.fileType = "photo";
        m_uploadFile.contentType = "image/jpeg";

        xTaskCreatePinnedToCore(this->sendMultipartFormDataTask, "sendFileTask", 8192, this, 11, &handleUpload, 0);
        //xTaskCreate(this->sendMultipartFormDataTask, "sendFileTask", 8192, this, 11, &handleUpload);
    }
    return true;

#endif
}

#if defined(ESP32)
void AsyncTelegram::sendMultipartFormDataTask(void *args)
{
    log_debug();
    AsyncTelegram* _this =      (AsyncTelegram*) args;

    _this->sendMultipartFormData("sendPhoto", _this->m_uploadFile.chat_id, _this->m_uploadFile.fileName,
                            _this->m_uploadFile.contentType, _this->m_uploadFile.fileType );
    vTaskDelete( NULL );
}
#endif

bool AsyncTelegram::sendMultipartFormData( const char* command,  const uint32_t &chat_id, const String &fileName,
                                           const char* contentType, const char* binaryPropertyName)
{
    #define BOUNDARY            "----WebKitFormBoundary7MA4YWxkTrZu0gW"
    #define END_BOUNDARY        "\r\n--" BOUNDARY "--\r\n"

    File myFile = m_filesystem->open("/" + fileName, "r");
    if (!myFile) {
        Serial.printf("Failed to open file %s\n", fileName.c_str());
        return false;
    }

    if (telegramClient->connected()) {
        httpData.busy = true;
        httpData.timestamp = millis();
        String formData;
        formData.reserve(300);
        formData += "--" BOUNDARY;
        formData += "\r\nContent-disposition: form-data; name=\"chat_id\"\r\n\r\n";
        formData += chat_id;
        formData += "\r\n--" BOUNDARY;
        formData += "\r\nContent-disposition: form-data; name=\"";
        formData += binaryPropertyName;
        formData += "\"; filename=\"";
        formData += fileName;
        formData += "\"\r\nContent-Type: ";
        formData += contentType;
        formData += "\r\n\r\n";

        // Send POST request to host
        telegramClient->print("POST /bot");
        telegramClient->print(m_token);
        telegramClient->print("/");
        telegramClient->print(command);
        telegramClient->println(" HTTP/1.1");
        // Headers
        telegramClient->println("Host: " TELEGRAM_HOST);
        telegramClient->print("Content-Length: ");
        int contentLength = myFile.size() + formData.length() + strlen(END_BOUNDARY);
        telegramClient->println(String(contentLength));
        telegramClient->print("Content-Type: multipart/form-data; boundary=");
        telegramClient->println(BOUNDARY);
        telegramClient->println();
        // Body of request
        telegramClient->print(formData);

        uint8_t buff[BLOCK_SIZE];
        while (myFile.available()) {
            #if defined(ESP32)
                esp_task_wdt_reset();
            #else
                yield();
            #endif
            if(myFile.available() > BLOCK_SIZE ){
                myFile.read(buff, BLOCK_SIZE );
                telegramClient->write(buff, BLOCK_SIZE);
                Serial.print(".");
            }
            else {
                int b_size = myFile.available() ;
                myFile.read(buff, b_size);
                telegramClient->write(buff, b_size);
                Serial.println(" ;");
            }
        }

        telegramClient->print(END_BOUNDARY);
        myFile.close();
        httpData.busy = false;

        #if defined(ESP32)
        // Resume main task
        vTaskResume(taskHandler);
        #endif
    }
    else {
        Serial.println("\nError: client not connected");
        return false;
    }
    return true;
}

