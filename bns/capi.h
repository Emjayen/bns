/*
 * capi.h
 *
 */
#ifndef CAPI_H
#define CAPI_H
#include <pce\pce.h>



// CAPI message
struct capi_msg
{
    union
    {
        uid _int;
        char* _pstr;
    } f[0x10];
};


#define CAPI_FIELD_CHANNEL    0x1 
#define CAPI_FIELD_COMMAND    0x2 
#define CAPI_FIELD_CODE       0x3 
#define CAPI_FIELD_USER_ID    0x4 
#define CAPI_FIELD_TOON_NAME  0x7 
#define CAPI_FIELD_FLAGS      0x8 
#define CAPI_FIELD_AREA       0x9 
#define CAPI_FIELD_MESSAGE    0xA 
#define CAPI_FIELD_PAYLOAD    0xB 
#define CAPI_FIELD_TYPE       0xC 
#define CAPI_FIELD_REQUEST_ID 0xD 
#define CAPI_FIELD_STATUS     0xE 
#define CAPI_FIELD_ATTRIBUTES 0xF 

#define CAPIS_USER_UPDATE   0x1
#define CAPIS_KICK 0x2
#define CAPIS_CHAT 0x3
#define CAPIS_AUTH 0x5
#define CAPIS_CHAT_EVENT 0x6
#define CAPIS_EMOTE 0x7
#define CAPIS_UNBAN 0x8
#define CAPIS_USER_LEAVE 0x9
#define CAPIS_BAN 0xA
#define CAPIS_DISCONNECT 0xB
#define CAPIS_ASSIGN_MODERATOR 0xC
#define CAPIS_ENTER_CHANNEL 0xD
#define CAPIS_WHISPER 0xE
#define CAPIS_CONNECT 0xF

/*
 * CAPIParse
 *
 */
void CAPIParse(char* pJSON, capi_msg* pMsg);


extern const char* CAPI_TXT_VALUE[];
extern const char* CAPI_TXT_FIELD[];

#endif