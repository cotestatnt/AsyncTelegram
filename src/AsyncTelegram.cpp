

#include "AsyncTelegram.h"


#if DEBUG_ENABLE
#define debugJson(X, Y) { log_debug(); serializeJsonPretty(X, Y); Serial.println();}
#define errorJson(X, Y) { log_error(); serializeJsonPretty(X, Y); Serial.println();}
#else
#define debugJson(X, Y) 
#define errorJson(X, Y)
#endif


// get fingerprints from https://www.grc.com/fingerprints.htm
uint8_t fingerprint[20] = { 0xF2, 0xAD, 0x29, 0x9C, 0x34, 0x48, 0xDD, 0x8D, 0xF4, 0xCF, 0x52, 0x32, 0xF6, 0x57, 0x33, 0x68, 0x2E, 0x81, 0xC1, 0x90 };

static const char digicert[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIEADCCAuigAwIBAgIBADANBgkqhkiG9w0BAQUFADBjMQswCQYDVQQGEwJVUzEh
MB8GA1UEChMYVGhlIEdvIERhZGR5IEdyb3VwLCBJbmMuMTEwLwYDVQQLEyhHbyBE
YWRkeSBDbGFzcyAyIENlcnRpZmljYXRpb24gQXV0aG9yaXR5MB4XDTA0MDYyOTE3
MDYyMFoXDTM0MDYyOTE3MDYyMFowYzELMAkGA1UEBhMCVVMxITAfBgNVBAoTGFRo
ZSBHbyBEYWRkeSBHcm91cCwgSW5jLjExMC8GA1UECxMoR28gRGFkZHkgQ2xhc3Mg
MiBDZXJ0aWZpY2F0aW9uIEF1dGhvcml0eTCCASAwDQYJKoZIhvcNAQEBBQADggEN
ADCCAQgCggEBAN6d1+pXGEmhW+vXX0iG6r7d/+TvZxz0ZWizV3GgXne77ZtJ6XCA
PVYYYwhv2vLM0D9/AlQiVBDYsoHUwHU9S3/Hd8M+eKsaA7Ugay9qK7HFiH7Eux6w
wdhFJ2+qN1j3hybX2C32qRe3H3I2TqYXP2WYktsqbl2i/ojgC95/5Y0V4evLOtXi
EqITLdiOr18SPaAIBQi2XKVlOARFmR6jYGB0xUGlcmIbYsUfb18aQr4CUWWoriMY
avx4A6lNf4DD+qta/KFApMoZFv6yyO9ecw3ud72a9nmYvLEHZ6IVDd2gWMZEewo+
YihfukEHU1jPEX44dMX4/7VpkI+EdOqXG68CAQOjgcAwgb0wHQYDVR0OBBYEFNLE
sNKR1EwRcbNhyz2h/t2oatTjMIGNBgNVHSMEgYUwgYKAFNLEsNKR1EwRcbNhyz2h
/t2oatTjoWekZTBjMQswCQYDVQQGEwJVUzEhMB8GA1UEChMYVGhlIEdvIERhZGR5
IEdyb3VwLCBJbmMuMTEwLwYDVQQLEyhHbyBEYWRkeSBDbGFzcyAyIENlcnRpZmlj
YXRpb24gQXV0aG9yaXR5ggEAMAwGA1UdEwQFMAMBAf8wDQYJKoZIhvcNAQEFBQAD
ggEBADJL87LKPpH8EsahB4yOd6AzBhRckB4Y9wimPQoZ+YeAEW5p5JYXMP80kWNy
OO7MHAGjHZQopDH2esRU1/blMVgDoszOYtuURXO1v0XJJLXVggKtI3lpjbi2Tc7P
TMozI+gciKqdi0FuFskg5YmezTvacPd+mSYgFFQlq25zheabIZ0KbIIOqPjCDPoQ
HmyW74cNxA9hi63ugyuV+I6ShHI56yDqg+2DzZduCLzrTia2cyvk0/ZM/iZx4mER
dEr/VxqHD3VILs9RaRegAhJhldXRQLIQTO7ErBBDpqWeCtWVYpoNz4iCxTIM5Cuf
ReYNnyicsbkqWletNw+vHX/bvZ8=
-----END CERTIFICATE-----
)EOF";

AsyncTelegram::AsyncTelegram() {
    httpData.payload.reserve(512);
    httpData.param.reserve(512);
    httpData.command.reserve(32);
    
#if USE_FINGERPRINT
    setFingerprint(fingerprint);   // set the default fingerprint   
    telegramClient.setFingerprint(m_fingerprint);  
#endif
	
#if defined(ESP8266)    
	#if USE_UNSECURE
		telegramClient.setInsecure();     
	#else	
		telegramClient.setCACert_P(digicert, sizeof(digicert));
		telegramClient.setSession(&m_session);
		telegramClient.setBufferSizes(TCP_MSS, 2*TCP_MSS);
	#endif
#elif defined(ESP32) 
	telegramClient.setCACert(digicert);
#endif 

}


bool AsyncTelegram::begin(){

#if defined(ESP32)
    //Start Task with input parameter set to "this" class
    xTaskCreatePinnedToCore(
        this->postCommandTask,  //Function to implement the task
        "httpTask",             //Name of the task
        8192,                   //Stack size in words
        this,                   //Task input parameter
        10,                     //Priority of the task
        &taskHandler,           //Task handle.
        0                       //Core where the task should run
    );    
#endif   
    telegramClient.connect(TELEGRAM_HOST, TELEGRAM_PORT);   
    return getMe(m_user);
}


bool AsyncTelegram::reset(void){     
    telegramClient.stop();   
    telegramClient.flush();
    
#if defined(ESP32)
    httpData.command.clear();
#endif

    httpData.timestamp = millis();
    return checkConnection();
}


// helper function used to select the properly working mode with ESP8266/ESP32
void AsyncTelegram::sendCommand(const char* const&  command, const char* const& param)
{
#if defined(ESP32)  
    if(httpData.waitingReply == false && telegramClient.connected() ){
        httpData.waitingReply = true;
        httpData.command = command;
        httpData.param = param;
    }
#else
    postCommand(command, param, false);
#endif
}


// Blocking https POST to server
String AsyncTelegram::postCommand(const char* const& command, const char* const& param, bool blocking)
{   
    //bool connected = checkConnection();
    if(telegramClient.connected()){      
        telegramClient.print("POST https://" TELEGRAM_HOST "/bot");
        telegramClient.print(m_token);
        telegramClient.print("/");
        telegramClient.print(command);
        telegramClient.print(" HTTP/1.1" "\nHost: api.telegram.org" "\nConnection: keep-alive" "\nContent-Type: application/json");
        telegramClient.print("\nContent-Length: ");
        telegramClient.print(strlen(param));
        telegramClient.print("\n\n");
        telegramClient.print(param);
       
        httpData.payload.clear();
        httpData.waitingReply = true;
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
            httpData.waitingReply = false;
            return httpData.payload ;        
        }
    }
    return (char *)0;   
}


// Https POST command runned in a separate task/separate core (ESP32 only)
#if defined(ESP32) 
void AsyncTelegram::postCommandTask(void *args){
   
    log_debug("Start http request task on core %d\n", xPortGetCoreID());
    AsyncTelegram *_this = (AsyncTelegram *) args;  

    for(;;) {         
        // send a command to telegram server
        if (_this->httpData.command.length() && _this->telegramClient.connected() ) {    
            uint32_t t1 = millis();

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
            _this->httpData.payload.clear();
            _this->httpData.waitingReply = true;

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
            while (_this->telegramClient.available()) {
                yield();
                _this->httpData.payload  += (char) _this->telegramClient.read();
            }
            _this->httpData.waitingReply = false;     

            UBaseType_t uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );                                        
            log_debug("Time: %lu, Heap: %u, Stack: %u\n", millis() - t1, heap_caps_get_free_size(0), uxHighWaterMark);  
        }  
        vTaskDelay(100);
    }
    vTaskDelete(NULL);             // delete task from memory    
}
#endif



bool AsyncTelegram::getUpdates(){   

    // Send message to Telegram server only if enough time has passed since last
    if(millis() - m_lastUpdateTime > m_minUpdateTime){
        m_lastUpdateTime = millis();

        // If previuos reply from server was received, we are free to send another one
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

    // No response from Telegram server for a long time 
    if(millis() - httpData.timestamp > 10*m_minUpdateTime) {
        Serial.println("Reset connection");
        reset();
    }

// No task manager for esp8266 :(
#if defined(ESP8266)    
    // If there are incoming bytes available from the server, read and save
    httpData.payload.clear();

    // Skip headers
    while (telegramClient.connected()) {
        String line = telegramClient.readStringUntil('\n');
        // End of headers
        if (line == "\r") break;
        // Server has closed the connection from remote, force restart connection.
        if (line.indexOf("Connection: close") > -1) {
            log_debug("Connection closed from server side\n");   
            httpData.timestamp = 0; // force restart connection from next getUpdates()
        }
    }
    // Get payload 
    while (telegramClient.available()) {
        httpData.payload  += (char) telegramClient.read();
    }
    httpData.waitingReply = false;     
#endif

    return ! httpData.waitingReply;
}



// Parse message received from Telegram server
MessageType AsyncTelegram::getNewMessage(TBMessage &message ) 
{
    message.messageType = MessageNoData;    

    // Check incoming messages from server (if enough time has elapsed since last)
    getUpdates() ;
    
    if( ! httpData.waitingReply && httpData.payload.length() ) {       
        // We have a message, parse data received
        DynamicJsonDocument root(BUFFER_BIG);
        deserializeJson(root, httpData.payload);
        httpData.payload.clear();
        httpData.timestamp = millis();
        httpData.waitingReply = false;

        bool ok = root["ok"];
        if (!ok) {
            errorJson(root, Serial);
            return MessageNoData;
        }
        
        debugJson(root, Serial);    
        uint32_t updateID = root["result"][0]["update_id"];
        if (updateID == 0){
            return MessageNoData;
        }
        m_lastUpdate = updateID + 1;
        
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
    String response((char *)0);
    response.reserve(100);

    // getFile has to be blocking (wait server reply)
    String cmd = "getFile?file_id=" + String(doc.file_id);
    response = postCommand(cmd.c_str(), "", true);
    if (response.length() == 0)
        return false;

    DynamicJsonDocument root(BUFFER_SMALL);
    deserializeJson(root, response);
    httpData.payload.clear();

    bool ok = root["ok"];
    if (!ok) {
        errorJson(root, Serial);
        return false;
    }

    debugJson(root, Serial);
    doc.file_path  = "https://api.telegram.org/file/bot";
    doc.file_path += m_token;
    doc.file_path += "/";
    doc.file_path += root["result"]["file_path"].as<String>();
    doc.file_size  = root["result"]["file_size"].as<long>();
    return true;
}


// Blocking getMe function (we wait for a reply from Telegram server)
bool AsyncTelegram::getMe(TBUser &user) {
    String response((char *)0);
    response.reserve(100);
    
    // getMe has top be blocking (wait server reply)
    response = postCommand("getMe", "", true); 
    if (response.length() == 0)
        return false;

    DynamicJsonDocument root(BUFFER_SMALL);
    deserializeJson(root, response);
    httpData.payload.clear();

    bool ok = root["ok"];
    if (!ok) {
        errorJson(root, Serial);
        return false;
    }
    debugJson(root, Serial);

    user.id           = root["result"]["id"];
    user.isBot        = root["result"]["is_bot"];
    user.firstName    = root["result"]["first_name"];
    user.username     = root["result"]["username"];
    user.lastName     = root["result"]["last_name"];
    user.languageCode = root["result"]["language_code"];
    m_userName        = user.username ;
    return true;
}



void AsyncTelegram::sendMessage(const TBMessage &msg, const char* message, String keyboard)
{
    if (sizeof(message) == 0)
        return;
    String param((char *)0);
    param.reserve(512);
    DynamicJsonDocument root(BUFFER_BIG);   

    root["chat_id"] = msg.sender.id;
    root["text"] = message;
    if (msg.isMarkdownEnabled)
        root["parse_mode"] = "Markdown";
    
    if (keyboard.length() != 0) {
        DynamicJsonDocument doc(512);
        deserializeJson(doc, keyboard);
        JsonObject myKeyb = doc.as<JsonObject>();
        root["reply_markup"] = myKeyb;
    }
    
    serializeJson(root, param);
    sendCommand("sendMessage", param.c_str());
    debugJson(root, Serial);
}


void AsyncTelegram::sendToUser(const int32_t userid, String &message, String keyboard) {
    TBMessage msg;
    msg.sender.id = userid;
    return sendMessage(msg, message.c_str(), "");
}

void AsyncTelegram::sendPhotoByUrl(const uint32_t& chat_id,  const String& url, const String& caption){ 
    if (sizeof(url) == 0)
        return;
    String param((char *)0);
    param.reserve(512);
    DynamicJsonDocument root(BUFFER_BIG);   
    root["chat_id"] = chat_id;
    root["photo"] = url;
    root["caption"] = caption;  
    serializeJson(root, param);
    sendCommand("sendPhoto", param.c_str());
    debugJson(root, Serial);
}

void AsyncTelegram::sendToChannel(const char* &channel, String &message, bool silent) {
    if (sizeof(message) == 0)
        return;
    String param((char *)0);
    param.reserve(512);
    DynamicJsonDocument root(BUFFER_BIG);   

    root["chat_id"] = channel;
    root["text"] = message;
    if(silent)
        root["silent"] = true;
    
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
    String command;
    root["remove_keyboard"] = true;
    if (selective) {
        root["selective"] = true;
    }
    serializeJson(root, command);
    sendMessage(msg, message, command);
}

bool AsyncTelegram::serverReply(const char* const& replyMsg) {  
    DynamicJsonDocument root(BUFFER_SMALL);
    deserializeJson(root, replyMsg);
    bool ok = root["ok"];
    if (!ok) {
        errorJson(root, Serial);
        return false;
    }
    debugJson(root, Serial);
    return true;
}

bool AsyncTelegram::checkConnection(){
    // Start connection with Telegramn server if necessary)
    if(! telegramClient.connected()){
          // try to connect
        if (!telegramClient.connect(TELEGRAM_HOST, TELEGRAM_PORT)) {
            // no way, try to connect with fixed IP
            IPAddress telegramServerIP;
            telegramServerIP.fromString(TELEGRAM_IP);
            if (!telegramClient.connect(telegramServerIP, TELEGRAM_PORT)) {
                Serial.printf("Unable to connect to Telegram server\n");                  
            }
            else {
                Serial.printf("Connected using Telegram fixed IP\n");                
                telegramClient.setTimeout(SERVER_TIMEOUT);
            }
        }
        else {
            Serial.printf("Connected using Telegram hostname\n"); 
            telegramClient.setTimeout(SERVER_TIMEOUT);
        }   
    }
    return telegramClient.connected();
}



bool AsyncTelegram::sendPhotoByFile(const uint32_t& chat_id, const String& fileName, fs::FS& filesystem) {

    m_filesystem = &filesystem;
#if defined(ESP8266)
    return sendMultipartFormData("sendPhoto", chat_id, fileName, "image/jpeg", "photo" );
#endif
    m_fileInfo.chat_id =  chat_id;
    m_fileInfo.fileName = fileName;
    m_fileInfo.fileType = "photo";
    xTaskCreatePinnedToCore(this->sendMultipartFormDataTask, "sendFileTask", 8192, this, 1, NULL, 0);    
    return true;
}

bool AsyncTelegram::sendPhotoByFile(const TBMessage &msg, const String& fileName, fs::FS& filesystem) {    
    return sendPhotoByFile(msg.sender.id, fileName, filesystem );
}


void AsyncTelegram::sendMultipartFormDataTask(void *args)
{
    log_debug();
    AsyncTelegram* _this = (AsyncTelegram*) args;  

    uint32_t chat_id = _this->m_fileInfo.chat_id;
    const char* fileName = _this->m_fileInfo.fileName.c_str();
    const char* fileType =  _this->m_fileInfo.fileType.c_str();
    const char* contentType =  _this->m_fileInfo.contentType.c_str();

    _this->sendMultipartFormData("sendPhoto", chat_id, fileName, contentType, fileType ); 
    vTaskDelay(200);
    vTaskDelete( NULL );
}

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

#if defined(ESP32)
        vTaskSuspend(taskHandler);
#endif         
        httpData.waitingReply = false;
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
        
        // Send POST request to host        
        telegramClient.print("POST /bot");     
        telegramClient.print(m_token);  
        telegramClient.print("/");  
        telegramClient.print(command);  
        telegramClient.println(" HTTP/1.1");      
        // Headers
        telegramClient.println("Host: " TELEGRAM_HOST);    
        telegramClient.print("Content-Length: ");
        int contentLength = myFile.size() + formData.length() + String(END_BOUNDARY).length();
        telegramClient.println(String(contentLength));
        telegramClient.print("Content-Type: multipart/form-data; boundary=");
        telegramClient.println(BOUNDARY);
        telegramClient.println();    
        // Body of request
        telegramClient.print(formData);

        uint8_t buff[BLOCK_SIZE];
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
    }
    else {
        Serial.println("\nError: client not connected");
        return false;
    }
    
#if defined(ESP32)
    vTaskResume(taskHandler);
#endif 
    httpData.waitingReply = false;
    return true;
}

