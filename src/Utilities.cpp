#include "Utilities.h"

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

String int64ToAscii(int64_t value) {
	String buffer = "";
	int64_t temp;
	uint8_t rest;
	char ascii;
	if (value < 0)
		temp = -value;
	else
		temp = value;

	while (temp != 0) {
		rest = temp % 10;
		temp = (temp - rest) / 10;
		ascii = 0x30 + rest;
		buffer = ascii + buffer;
	}
	if (value < 0)
		buffer = '-' + buffer;
	return buffer;
}