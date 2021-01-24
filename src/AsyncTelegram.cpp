#include "AsyncTelegram.h"

#if DEBUG_ENABLE
#define debugJson(X, Y)  { log_debug(); Serial.println(); serializeJsonPretty(X, Y); Serial.println();}
#define errorJson(E)  { log_error(); Serial.println(); Serial.println(E);}
#else
#define debugJson(X, Y) 
#define errorJson(E)
#endif

// get fingerprints from https://www.grc.com/fingerprints.htm
uint8_t defaulFingerprint[20] = { 0xF2, 0xAD, 0x29, 0x9C, 0x34, 0x48, 0xDD, 0x8D, 0xF4, 0xCF, 0x52, 0x32, 0xF6, 0x57, 0x33, 0x68, 0x2E, 0x81, 0xC1, 0x90 };

AsyncTelegram::AsyncTelegram() {

    httpData.payload.reserve(512);
    httpData.param.reserve(512);
    httpData.command.reserve(32);
    m_minUpdateTime = MIN_UPDATE_TIME;

#if USE_FINGERPRINT
    setFingerprint(defaulFingerprint);   // set the default fingerprint   
    telegramClient.setFingerprint(m_fingerprint);  
    telegramClient.setBufferSizes(TCP_MSS, TCP_MSS);
#else     
    #if defined(ESP8266)
        // Set up time to allow for certificate validation
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        telegramClient.setSession(&m_session);
        telegramClient.setBufferSizes(TCP_MSS, TCP_MSS);
    #elif defined(ESP32) 
	    telegramClient.setCACert(digicert);
    #endif
#endif 

}


bool AsyncTelegram::begin(){
    
#if defined(ESP8266) && USE_FINGERPRINT == 0  
    // Waiting for NTP time sync:
    log_debug("\nWaiting for NTP time sync: ");
    time_t now = time(nullptr);
    while (now < 8 * 3600 * 2) {        
        now = time(nullptr);
        delay(100);
    }
#endif
    telegramClient.connect(TELEGRAM_HOST, TELEGRAM_PORT);   
    bool isMe = getMe(m_user);
#if defined(ESP32)
    //Start Task with input parameter set to "this" class
    xTaskCreatePinnedToCore(
        this->postCommandTask,  //Function to implement the task
        "httpTask",             //Name of the task
        8192,                   //Stack size in words
        this,                   //Task input parameter
        1,                      //Priority of the task
        &taskHandler,           //Task handle.
        0                       //Core where the task should run
    );    
#endif   
    return isMe;  
}


void AsyncTelegram::reset(void){     
    log_debug("\nReset connection");
    telegramClient.stop();   
    telegramClient.flush();   
    httpData.busy = false; 
    
#if defined(ESP32)
    httpData.command.clear();
    if(taskHandler == nullptr)
        xTaskCreatePinnedToCore(this->postCommandTask, "httpTask", 8192, this, 1, &taskHandler, 0);  
#endif

    httpData.timestamp = millis();
    checkConnection();
}


// helper function used to select the properly working mode with ESP8266/ESP32
void AsyncTelegram::sendCommand(const char* const&  command, const char* const& param)
{
#if defined(ESP32)  
    // Check if http task is busy before set new command
    checkConnection();
    if( !httpData.busy && telegramClient.connected() ){
        httpData.command = command;
        httpData.param = param;
    }
#else
    postCommand(command, param, false);
#endif
}



String AsyncTelegram::postCommand(const char* const& command, const char* const& param, bool blocking)
{   
    bool connected = checkConnection();
    if(connected){      
        String request((char *)0);
        request.reserve(512);
        request = "POST https://" TELEGRAM_HOST "/bot";
        request += m_token;
        request += "/";
        request += command;
        request += " HTTP/1.1" "\nHost: api.telegram.org" "\nConnection: keep-alive" "\nContent-Type: application/json";
        request += "\nContent-Length: ";
        request += String(strlen(param));
        request += "\n\n";
        request += param;
        telegramClient.print(request);
        httpData.busy = true;

         // Blocking mode
        if (blocking) {        
            while (telegramClient.connected()) {
                String line = telegramClient.readStringUntil('\n');
                if (line == "\r") break;
            }
            // If there are incoming bytes available from the server, read them and print them:
            while (telegramClient.available()) {
                httpData.payload  += (char) telegramClient.read();
            }
            httpData.busy = false;
            return httpData.payload ;        
        }
    }
    return (char *)0;   
}


// Https POST command runned in a separate task/separate core (ESP32 only)
#if defined(ESP32) 
void AsyncTelegram::postCommandTask(void *args){
    log_debug("\nStarted http request task on core %d\n", xPortGetCoreID());
    AsyncTelegram *_this = (AsyncTelegram *) args;  
    vTaskDelay(MIN_UPDATE_TIME);     
    for(;;) {  
        vTaskDelay(50);      
        // send a command to telegram server
        if (_this->httpData.command.length() && _this->telegramClient.connected()) {    
            uint32_t t1 = millis();
            _this->httpData.busy = true;
            _this->telegramClient.print("POST https://" TELEGRAM_HOST "/bot");
            _this->telegramClient.print(_this->m_token);
            _this->telegramClient.print("/");
            _this->telegramClient.print(_this->httpData.command);
            _this->telegramClient.print(" HTTP/1.1" "\nHost: api.telegram.org" "\nConnection: keep-alive" "\nContent-Type: application/json");
            _this->telegramClient.print("\nContent-Length: ");
            _this->telegramClient.print(_this->httpData.param.length());
            _this->telegramClient.print("\n\n");
            _this->telegramClient.print(_this->httpData.param);

            _this->httpData.command.clear();
            _this->httpData.param.clear();   

            // Skip headers
            while (_this->telegramClient.connected()) {
                yield();
                String line = _this->telegramClient.readStringUntil('\n');
                // End of headers
                if (line == "\r")  break;
                // Server has closed the connection from remote, force restart connection.
                if (line.indexOf("Connection: close") > -1) {
                    log_debug("Connection closed from server side\n");   
                    _this->httpData.timestamp = 0; // force restart connection from next getUpdates()
                }
            }
            // Save the payload data
            _this->httpData.payload.clear();
            while (_this->telegramClient.available()) {
                yield();
                _this->httpData.payload  += (char) _this->telegramClient.read();
            }

            UBaseType_t uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );                                        
            log_debug("Time: %lu, Heap: %u, MaxFree: %u, Task stack: %u\n", millis() - t1, 
                    heap_caps_get_free_size(0), heap_caps_get_largest_free_block(0), uxHighWaterMark);  

            // Something wrong, maybe memory leak?
            if (uxHighWaterMark < 6000 || _this->httpData.timestamp == 0) {
                log_error("\nError: Memory leak in http object - restart task");    
                break;                                         
            }
            
            _this->httpData.busy = false;
        }  
    }
    vTaskDelete(NULL);       
}
#endif



bool AsyncTelegram::getUpdates(){   
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
            String param;
            DynamicJsonDocument root(BUFFER_SMALL);
            root["limit"] = 1;
            // polling timeout: add &timeout=<seconds. zero for short polling.
            root["timeout"] = 3;
            root["allowed_updates"] = "message,callback_query";     
            if (m_lastUpdateId != 0) {
                root["offset"] = m_lastUpdateId;
            }           
            serializeJson(root, param);             
            sendCommand("getUpdates", param.c_str());    
        }
    }

// No task manager for esp8266 :(
#if defined(ESP8266)    
    if(httpData.busy ) {

        // If there are incoming bytes available from the server, read them and store:
        httpData.payload.clear();
        while (telegramClient.available() ){
            httpData.payload += (char) telegramClient.read();       
        }

        // We have a message, parse data received
        if(httpData.payload.length()) {        
            httpData.payload = httpData.payload.substring(httpData.payload.indexOf("{\"ok\":"), httpData.payload.length());     
            httpData.busy = false;
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
    
    if( httpData.payload.length() && !httpData.busy) {       
        // We have a message, parse data received
        DynamicJsonDocument root(BUFFER_BIG);
        deserializeJson(root, httpData.payload);
        
        bool ok = root["ok"];
        if (!ok) {
            errorJson(httpData.payload);
            return MessageNoData;
        }
        debugJson(root, Serial);    
        httpData.payload.clear();
        httpData.command.clear();
        httpData.timestamp = millis();

        int32_t updateID = root["result"][0]["update_id"];
        if (updateID == 0){
            return MessageNoData;
        }

        if(updateID == m_lastUpdateId -1) {
            // Message already parsed
            return MessageNoData;
        }
        m_lastUpdateId = updateID + 1;
        
        if(root["result"][0]["callback_query"]["id"]){
            // this is a callback query
            message.callbackQueryID   = root["result"][0]["callback_query"]["id"];
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
            message.sender.id        = root["result"][0]["message"]["from"]["id"];
            message.sender.username  = root["result"][0]["message"]["from"]["username"];
            message.sender.firstName = root["result"][0]["message"]["from"]["first_name"];
            message.sender.lastName  = root["result"][0]["message"]["from"]["last_name"];
            message.group.id         = root["result"][0]["message"]["chat"]["id"];
            message.group.title      = root["result"][0]["message"]["chat"]["title"];
            message.date             = root["result"][0]["message"]["date"];

            if(root["result"][0]["message"]["reply_to_message"]){
                // this is a reply to message
                message.text        = root["result"][0]["message"]["text"].as<String>();        
                message.messageType = MessageReply;   
            }                      
            else if(root["result"][0]["message"]["location"]){
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
                Serial.println("Call getFile()");
                message.document.file_exists  = getFile(message.document);
                message.messageType           = MessageDocument;
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


bool AsyncTelegram::getFile(TBDocument &doc)
{
    String response;
    response.reserve(100);

    // getFile has to be blocking (wait server reply)
    String cmd = "getFile?file_id=" + String(doc.file_id);
    response = postCommand(cmd.c_str(), "", true);
    if (response.length() == 0)
        return false;

    DynamicJsonDocument root(BUFFER_SMALL);
    deserializeJson(root, response);
    
    bool ok = root["ok"];
    if (!ok) {
        errorJson(httpData.payload);
        return MessageNoData;
    }
    debugJson(root, Serial);    
    httpData.payload.clear();
    httpData.timestamp = millis();

    doc.file_path  = "https://api.telegram.org/file/bot";
    doc.file_path += m_token;
    doc.file_path += "/";
    doc.file_path += root["result"]["file_path"].as<String>();
    doc.file_size  = root["result"]["file_size"].as<long>();
    return true;
}



// Blocking getMe function (we wait for a reply from Telegram server)
bool AsyncTelegram::getMe(TBUser &user) 
{
    // getMe has top be blocking (wait server reply)
    String response = postCommand("getMe", "", true); 
    if (response.length() == 0)
        return false;

    DynamicJsonDocument root(BUFFER_SMALL);
    deserializeJson(root, response);

    bool ok = root["ok"];
    if (!ok) {
        errorJson(httpData.payload);
        return MessageNoData;
    }
    debugJson(root, Serial);    
    httpData.payload.clear();
    httpData.timestamp = millis();

    user.id           = root["result"]["id"];
    user.isBot        = root["result"]["is_bot"];
    user.firstName    = root["result"]["first_name"];
    user.username     = root["result"]["username"];
    user.lastName     = root["result"]["last_name"];
    user.languageCode = root["result"]["language_code"];
    userName          = user.username ;
    return true;
}



void AsyncTelegram::sendMessage(const TBMessage &msg, const char* message, String keyboard, bool forceReply)
{
    if (sizeof(message) == 0)
        return;
    
    DynamicJsonDocument root(BUFFER_BIG);   

    root["chat_id"] = msg.sender.id;
    root["text"] = message;
    if (msg.isMarkdownEnabled)
        root["parse_mode"] = "Markdown";
    
    if (keyboard.length() != 0 || forceReply) {
        DynamicJsonDocument doc(BUFFER_MEDIUM);
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


void AsyncTelegram::sendToUser(const int32_t userid, const char* message, String keyboard) {
    TBMessage msg;
    msg.sender.id = userid;
    return sendMessage(msg, message, "");
}

void AsyncTelegram::sendPhotoByUrl(const uint32_t& chat_id,  const String& url, const String& caption){ 
    if (sizeof(url) == 0)
        return;
    DynamicJsonDocument root(BUFFER_MEDIUM);   
    root["chat_id"] = chat_id;
    root["photo"] = url;
    root["caption"] = caption;  

    String param;
    serializeJson(root, param);
    sendCommand("sendPhoto", param.c_str());
    debugJson(root, Serial);
}

void AsyncTelegram::sendToChannel(const char* &channel, String &message, bool silent) {
    if (sizeof(message) == 0)
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

void AsyncTelegram::endQuery(const TBMessage &msg, const char* message, bool alertMode) {
    if (sizeof(msg.callbackQueryID) == 0)
        return;

    DynamicJsonDocument root(BUFFER_SMALL);
    root["callback_query_id"] =  msg.callbackQueryID;
    if (sizeof(message) != 0) {
        root["text"] = message;
        if (alertMode) 
            root["show_alert"] = true;
        else
            root["show_alert"] = false;
    }
    String param;
    serializeJson(root, param);
    sendCommand("answerCallbackQuery", param.c_str());
}

void AsyncTelegram::removeReplyKeyboard(const TBMessage &msg, const char* message, bool selective) {
    DynamicJsonDocument root(BUFFER_SMALL);
    
    root["remove_keyboard"] = true;
    if (selective) {
        root["selective"] = true;
    }

    String command;
    serializeJson(root, command);
    sendMessage(msg, message, command);
}

bool AsyncTelegram::serverReply(const char* const& replyMsg) {  
    DynamicJsonDocument root(BUFFER_SMALL);
    deserializeJson(root, replyMsg);
    bool ok = root["ok"];
    if (!ok) {
        errorJson(replyMsg);
        return false;
    }
    debugJson(root, Serial);
    return true;
}

bool AsyncTelegram::checkConnection(){
    // Start connection with Telegramn server if necessary)
    if(! telegramClient.connected()){
        #if defined(ESP8266)
        BearSSL::X509List cert(digicert);
        telegramClient.setTrustAnchors(&cert);
        #endif
        // try to connect
        if (!telegramClient.connect(TELEGRAM_HOST, TELEGRAM_PORT)) {
            // no way, try to connect with fixed IP
            IPAddress telegramServerIP;
            telegramServerIP.fromString(TELEGRAM_IP);
            if (!telegramClient.connect(telegramServerIP, TELEGRAM_PORT)) {
                Serial.printf("\n\nUnable to connect to Telegram server\n");                  
            }
            else {
                log_debug("\nConnected using Telegram ip address\n");                
                telegramClient.setTimeout(SERVER_TIMEOUT);
            }
        }
        else {
            log_debug("\nConnected using Telegram hostname\n"); 
            telegramClient.setTimeout(SERVER_TIMEOUT);
        }   
    }
    return telegramClient.connected();
}



bool AsyncTelegram::sendPhotoByFile(const uint32_t& chat_id, const String& fileName, fs::FS& filesystem) {

    m_filesystem = &filesystem;
#if defined(ESP8266)
    return sendMultipartFormData("sendPhoto", chat_id, fileName, "image/jpeg", "photo" );
#else

    if(!httpData.busy) {
        // Suspend main task
        vTaskSuspend(taskHandler);
        m_fileInfo.chat_id =  chat_id;
        m_fileInfo.fileName = fileName;
        m_fileInfo.fileType = "photo";
        xTaskCreatePinnedToCore(this->sendMultipartFormDataTask, "sendFileTask", 8192, this, 10, NULL, 0);    
    }
    return true;
#endif
}

bool AsyncTelegram::sendPhotoByFile(const TBMessage &msg, const String& fileName, fs::FS& filesystem) {    
    return sendPhotoByFile(msg.sender.id, fileName, filesystem );
}


#if defined(ESP32)
void AsyncTelegram::sendMultipartFormDataTask(void *args)
{
    log_debug();
    AsyncTelegram* _this =      (AsyncTelegram*) args;  
    uint32_t chat_id     =      _this->m_fileInfo.chat_id;
    const char* fileName =      _this->m_fileInfo.fileName.c_str();
    const char* fileType =      _this->m_fileInfo.fileType.c_str();
    const char* contentType =   _this->m_fileInfo.contentType.c_str();

    _this->sendMultipartFormData("sendPhoto", chat_id, fileName, contentType, fileType ); 
    vTaskDelete( NULL );
}
#endif

bool AsyncTelegram::sendMultipartFormData( const String& command,  const uint32_t& chat_id, const String& fileName, 
                                           const char* contentType, const char* binaryPropertyName)
{
    #define BOUNDARY            "----WebKitFormBoundary7MA4YWxkTrZu0gW"
    #define END_BOUNDARY        "\r\n--" BOUNDARY "--\r\n"

    File myFile = m_filesystem->open("/" + fileName, "r");
    if (!myFile) {
        Serial.printf("Failed to open file %s\n", fileName.c_str());
        return false;
    }

    if (telegramClient.connected()) {
        httpData.busy = true;      
        String formData;
        formData.reserve(300);
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
        
        // Send POST request to host        
        telegramClient.print("POST /bot");     
        telegramClient.print(m_token);  
        telegramClient.print("/");  
        telegramClient.print(command);  
        telegramClient.println(" HTTP/1.1");      
        // Headers
        telegramClient.println("Host: " TELEGRAM_HOST);    
        telegramClient.print("Content-Length: ");
        int contentLength = myFile.size() + formData.length() + strlen(END_BOUNDARY);
        telegramClient.println(String(contentLength));
        telegramClient.print("Content-Type: multipart/form-data; boundary=");
        telegramClient.println(BOUNDARY);
        telegramClient.println();    
        // Body of request
        telegramClient.print(formData);

        uint8_t buff[BLOCK_SIZE];
        //telegramClient.write(myFile);

        while (myFile.available()) {    
#if defined(ESP32)
            vTaskDelay(1);
#endif 
            if(myFile.available() > BLOCK_SIZE ){
                Serial.println(F("Sending binary photo full buffer")); 
                myFile.read(buff, BLOCK_SIZE );                            
                telegramClient.write(buff, BLOCK_SIZE);
            }
            else {
                Serial.println(F("Sending binary photo remaining buffer")); 
                int b_size = myFile.available() ;
                myFile.read(buff, b_size);
                telegramClient.write(buff, b_size); 
            }   
        }       
        telegramClient.print(END_BOUNDARY);
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

