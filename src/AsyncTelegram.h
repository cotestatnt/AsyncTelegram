#ifndef ASYNCTELEGRAM
#define ASYNCTELEGRAM

#define DEBUG_ENABLE  true              // enable debugmode -> print debug data on the Serial

// use Telegram fingerprint server validation or SSL digital certificate ca.cert format if 0
#define USE_FINGERPRINT     0           

#define SERVER_TIMEOUT      5000
#define MIN_UPDATE_TIME     2000

#define TELEGRAM_HOST  "api.telegram.org"
#define TELEGRAM_IP    "149.154.167.220" 
#define TELEGRAM_PORT   443

#include <Arduino.h>
#include <FS.h>
// for using int_64 data
#define ARDUINOJSON_USE_LONG_LONG   1 
#define ARDUINOJSON_DECODE_UNICODE  1
#include <ArduinoJson.h>

#include "serial_log.h"
#include "Utilities.h"
#include "DataStructures.h"
#include "InlineKeyboard.h"
#include "ReplyKeyboard.h"
#include "ca_cert.h"

#if defined(ESP32)     
    #include <WiFiClientSecure.h>
    #include <HTTPClient.h>
    #define BLOCK_SIZE          4096        // More memory, increase block size to speed-up a little upload
#elif defined(ESP8266)  
    #define BLOCK_SIZE          2048
    #include <ESP8266WiFi.h>
    #include <ESP8266HTTPClient.h>
    #include <WiFiClientSecure.h>   
#else
    #error "This library work only with ESP8266 or ESP32"
#endif

class AsyncTelegram
{

public:
    // default constructor
    AsyncTelegram();
    // default destructor
    inline ~AsyncTelegram(){;}

    // set the telegram token
    // params
    //   token: the telegram token
    inline void setTelegramToken(const char* token)  
    { 
        m_token = (char*) token; 
    }

    // set the new Telegram API server fingerprint overwriting the default one.
    // It can be obtained by this service: https://www.grc.com/fingerprints.htm
    // params:
    //    newFingerprint: the array of 20 bytes that contains the new fingerprint
    inline void setFingerprint(const uint8_t *newFingerprint)
    {
        for (int i = 0; i < 20; i++)
            m_fingerprint[i] = newFingerprint[i];
    }

    inline void updateFingerprint()
    {
        getFingerPrint(m_fingerprint);
    }


    // set the interval in milliseconds for polling in order to Avoid query Telegram server to much often (ms)
    // params:
    //    pollingTime: interval time in milliseconds
    inline void setUpdateTime(uint32_t pollingTime)
    { 
        if(pollingTime > MIN_UPDATE_TIME)
            m_minUpdateTime = pollingTime;
    }


    void sendPhotoByUrl(const uint32_t& chat_id,  const String& url, const String& caption);
    
    bool sendPhotoByFile(const uint32_t& chat_id,  const String& fileName, fs::FS& filesystem);
    bool sendPhotoByFile(const TBMessage &msg,  const String& fileName, fs::FS& filesystem);

    // Get file link and size by unique document ID
    // params
    //   doc   : document structure
    // returns
    //   true if no error
    bool getFile(TBDocument &doc);


    // test the connection between ESP8266 and the telegram server
    // returns
    //    true if no error occurred
    bool begin(void);

    // reset the connection with telegram server (ex. when connection was lost)
    void reset(void);

    // get the first unread message from the queue (text and query from inline keyboard). 
    // This is a destructive operation: once read, the message will be marked as read
    // so a new getMessage will read the next message (if any).
    // params
    //   message: the data structure that will contains the data retrieved
    // returns
    //   MessageNoData: an error has occurred
    //   MessageText  : the received message is a text
    //   MessageQuery : the received message is a query (from inline keyboards)
    MessageType getNewMessage(TBMessage &message);

    // send a message to the specified telegram user ID
    // params
    //   msg      : the TBMessage telegram recipient with user ID 
    //   message : the message to send
    //   keyboard: the inline/reply keyboard (optional)
    //             (in json format or using the inlineKeyboard/ReplyKeyboard class helper)
    
    void sendMessage(const TBMessage &msg, const char* message, String keyboard = "", bool forceReply = false);

    // sendMessage function overloads
    inline void sendMessage(const TBMessage &msg, String &message, String keyboard = "") 
    {
        return sendMessage(msg, message.c_str(), keyboard);
    }

    inline void sendMessage(const TBMessage &msg, const char* message, InlineKeyboard &keyboard) 
    {
        m_inlineKeyboard = keyboard;
        return sendMessage(msg, message, keyboard.getJSON());
    }

    inline void sendMessage(const TBMessage &msg, const char* message, ReplyKeyboard &keyboard) {
        return sendMessage(msg, message, keyboard.getJSON());
    }

    // Send message to a channel. The bot must be in the admin group
    void sendToChannel(const char*  &channel, String &message, bool silent) ;

    // Send message to a specific user. In order to work properly two conditions is needed:
    //  - You have to find the userid (for example using the bot @JsonBumpBot  https://t.me/JsonDumpBot)
    //  - User has to start your bot in it's own client. For example send a message with @<your bot name>
    void sendToUser(const int32_t userid, String &message, String keyboard = "") ;

    // terminate a query started by pressing an inlineKeyboard button. The steps are:
    // 1) send a message with an inline keyboard
    // 2) wait for a <message> (getNewMessage) of type MessageQuery
    // 3) handle the query and then call endQuery with <message>.callbackQueryID 
    // params
    //   msg  : the TBMessage telegram recipient with unique query ID (retrieved with getNewMessage method)
    //   message  : an optional message
    //   alertMode: false -> a simply popup message
    //              true --> an alert message with ok button
    void endQuery(const TBMessage &msg, const char* message, bool alertMode = false);

    // remove an active reply keyboard for a selected user, sending a message
    // params:
    //   msg      : the TBMessage telegram recipient with the telegram user ID 
    //   message  : the message to be show to the selected user ID
    //   selective: enable selective mode (hide the keyboard for specific users only)
    //              Targets: 1) users that are @mentioned in the text of the Message object; 
    //                       2) if the bot's message is a reply (has reply_to_message_id), sender of the original message
    // return:
    //   true if no error occurred
    void removeReplyKeyboard(const TBMessage &msg, const char* message, bool selective = false);

    // Moved to public section in order to get OTA firmware update working properly
    bool getUpdates();

    // Return the Telegram username
    inline const char* getTelegramUser() 
    {
        return userName;
    }
    const char*     userName ;

private:
    
    const char*     m_token;
    const char*     m_botName;
    int32_t         m_lastUpdateId = 0;
    
    uint8_t         m_fingerprint[20];
    TBUser          m_user; 
    InlineKeyboard  m_inlineKeyboard;   // last inline keyboard showed in bot    
    TBServerReply   httpData;           // Struct for store telegram server reply and infos about it
    uint32_t        m_minUpdateTime;    // Timer for avoiding query Server too much
    fs::FS*         m_filesystem ;   

#if defined(ESP32) 
    WiFiClientSecure telegramClient;
    TaskHandle_t taskHandler;
#elif defined(ESP8266) 
    BearSSL::WiFiClientSecure telegramClient;   
    BearSSL::Session m_session;       
    
#endif  

    // send commands to the telegram server. For info about commands, check the telegram api https://core.telegram.org/bots/api
    // params
    //   command   : the command to send, i.e. getMe
    //   parameters: optional parameters
    // returns
    //   an empty string if error
    //   a string containing the Telegram JSON response
    String postCommand(const char* const& command, const char* const& param, bool blocking = false);

#if defined(ESP32) 
    /*  postCommand() and sendMultipartFormData() are blocking functions because send an http request to server and wait for reply.
        With ESP32 we can move this job in a separate task on the other core for a full async http handling.
    */
    static void postCommandTask(void *args);
    static void sendMultipartFormDataTask(void *args);
    TBFileInfo m_fileInfo;        
#endif

    // helper function used to select the properly working mode with ESP8266/ESP32
    void sendCommand(const char* const&  command, const char* const& param);
    
    // upload documents to Telegram server https://core.telegram.org/bots/api#sending-files
    // params
    //   command   : the command to send, i.e. getMe
    //   chat_id   : the char to upload
    //   filename  : the name of document uploaded
    //   contentType  : the content type of document uploaded
    //   binaryPropertyName: the type of data
    // returns
    //   true if no error
    bool sendMultipartFormData( const String& command,  const uint32_t& chat_id,
                                const String& fileName, const char* contentType,
                                const char* binaryPropertyName );

    // get some information about the bot
    // params
    //   user: the data structure that will contains the data retreived
    // returns
    //   true if no error occurred
    bool getMe(TBUser &user);

    bool checkConnection();

    bool serverReply(const char* const&  replyMsg);


};

#endif
