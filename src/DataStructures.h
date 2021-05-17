
#ifndef DATA_STRUCTURES
#define DATA_STRUCTURES

#include <Arduino.h>

#define BUFFER_BIG       	2048 		// json parser buffer size (ArduinoJson v6)
#define BUFFER_MEDIUM     	1028 		// json parser buffer size (ArduinoJson v6)
#define BUFFER_SMALL      	512 		// json parser buffer size (ArduinoJson v6)

enum MessageType {
	MessageNoData   = 0,
	MessageText     = 1,
	MessageQuery    = 2,
	MessageLocation = 3,
	MessageContact  = 4,
	MessageDocument = 5,
	MessageReply 	= 6
};


// Here we store the stuff related to the Telegram server reply
struct HttpServerReply {
    bool        waitingReply = false;
    uint32_t    timestamp;
    String      payload;

    // Task sharing variables
    // Here we can share data with task for handling the request to server
    String      command;
    String      param;
} ;



struct TBUser {
	int32_t  id = 0;
	bool     isBot;
	const char*   firstName;
	const char*   lastName;
	const char*   username;
	const char*   languageCode;
};

struct TBGroup {
	int64_t id;
	const char*  title;
};

struct TBLocation{
	float longitude;
	float latitude;
};

struct TBContact {
	const char*  phoneNumber;
	const char*  firstName;
	const char*  lastName;
	int32_t 	 id;
	const char*  vCard;
};

struct TBDocument {
	const char*  file_id;
	const char*  file_name;
	bool         file_exists;
	int32_t      file_size;
	char		 file_path[128];
};

struct TBMessage {
	MessageType 	 messageType;
	bool		 isHTMLenabled = false;
	bool             isMarkdownEnabled = false;
	bool 	         disable_notification = false;
	bool		 force_reply = false;
	int64_t          chatId;
	int32_t          messageID;
	int32_t          date;
	int32_t          chatInstance;
	TBUser           sender;
	TBGroup          group;
	TBLocation       location;
	TBContact        contact;
	TBDocument       document;
	const char*      callbackQueryData;
	const char*   	 callbackQueryID;
	String      	 text;
};

#endif

