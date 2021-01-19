// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "AsyncTelegram.h"

#ifndef __LOG_H__
#define __LOG_H__

#ifdef __cplusplus
extern "C"
{
#endif


#ifndef CONFIG_LOG_COLORS
#define CONFIG_LOG_COLORS 0
#endif

#if CONFIG_LOG_COLORS
#define LOG_COLOR_BLACK   "30"
#define LOG_COLOR_RED     "31" //ERROR
#define LOG_COLOR_GREEN   "32" //INFO
#define LOG_COLOR_YELLOW  "33" //WARNING
#define LOG_COLOR_BLUE    "34"
#define LOG_COLOR_MAGENTA "35"
#define LOG_COLOR_CYAN    "36" //DEBUG
#define LOG_COLOR_GRAY    "37" //VERBOSE
#define LOG_COLOR_WHITE   "38"

#define LOG_COLOR(COLOR)  "\033[0;" COLOR "m"
#define LOG_BOLD(COLOR)   "\033[1;" COLOR "m"
#define LOG_RESET_COLOR   "\033[0m"

#define LOG_COLOR_E       LOG_COLOR(LOG_COLOR_RED)
#define LOG_COLOR_W       LOG_COLOR(LOG_COLOR_YELLOW)
#define LOG_COLOR_I       LOG_COLOR(LOG_COLOR_GREEN)
#define LOG_COLOR_D       LOG_COLOR(LOG_COLOR_CYAN)
#define LOG_COLOR_V       LOG_COLOR(LOG_COLOR_GRAY)
#else
#define LOG_COLOR_E
#define LOG_COLOR_W
#define LOG_COLOR_I
#define LOG_COLOR_D
#define LOG_COLOR_V
#define LOG_RESET_COLOR
#endif

const char * pathToFileName(const char * path);
//int log_printf(const char *fmt, ...);

#define _LOG_FORMAT(letter, format)  LOG_COLOR_ ## letter "[" #letter "][%s:%u] %s():\t" format LOG_RESET_COLOR, pathToFileName(__FILE__), __LINE__, __FUNCTION__


#if DEBUG_ENABLE
#define log_debug(format, ...) Serial.printf(_LOG_FORMAT(D, format), ##__VA_ARGS__)
#define log_error(format, ...) { Serial.println(); Serial.printf(_LOG_FORMAT(E, format), ##__VA_ARGS__); }
#define log_info(format, ...) Serial.printf(_LOG_FORMAT(I, format), ##__VA_ARGS__)
#else
#define log_debug(format, ...) 
#define log_error(format, ...) 
#define log_info(format, ...) 
#endif



#ifdef __cplusplus
}
#endif

#endif /* __ESP_LOGGING_H__ */
