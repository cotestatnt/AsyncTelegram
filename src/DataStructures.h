
#ifndef DATA_STRUCTURES
#define DATA_STRUCTURES

#include <Arduino.h>
#include <FS.h>

#define BUFFER_BIG       	2048 		// json parser buffer size (ArduinoJson v6)
#define BUFFER_MEDIUM      	1024 		// json parser buffer size (ArduinoJson v6)
#define BUFFER_SMALL      	512 		// json parser buffer size (ArduinoJson v6)

enum MessageType {
	MessageNoData   = 0,
	MessageText     = 1,
	MessageQuery    = 2,
	MessageLocation = 3,
	MessageContact  = 4,
	MessageDocument = 5,
	MessageReply = 6
};

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
	int32_t id;
	const char*  vCard;
};

struct TBDocument {
	const char*  file_id;
	const char*  file_name;
	String       file_path;
	int32_t      file_size;
	bool         file_exists;
};

struct TBMessage {
	int32_t          messageID;
	TBUser           sender;
	TBGroup          group;
	int32_t          date;
	String      	 text;
	int32_t          chatInstance;
	bool             isMarkdownEnabled = false;
	const char*      callbackQueryData;
	const char*   	 callbackQueryID;
	TBLocation       location;
	TBContact        contact;
	TBDocument       document;
	MessageType 	 messageType;
};



struct TBFileInfo {
	int32_t chat_id;
	String fileName;
	String fileType;
	String contentType;
};

// Here we store the stuff related to the Telegram server reply
struct TBServerReply{
	bool		busy;
    uint32_t    timestamp;
    String      payload;

    // Task sharing variables
    // Here we can share data with task for handling the request to server
    String      command;
    String      param;
} ;


#endif

