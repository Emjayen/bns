/*
 * websocket.h
 *
 */
#ifndef WEBSOCKET_H
#define WEBSOCKET_H
#include <pce\pce.h>


/* 
      0                   1                   2                   3
      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     +-+-+-+-+-------+-+-------------+-------------------------------+
     |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
     |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
     |N|V|V|V|       |S|             |   (if payload len==126/127)   |
     | |1|2|3|       |K|             |                               |
     +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
     |     Extended payload length continued, if payload len == 127  |
     + - - - - - - - - - - - - - - - +-------------------------------+
     |                               |Masking-key, if MASK set to 1  |
     +-------------------------------+-------------------------------+
     | Masking-key (continued)       |          Payload Data         |
     +-------------------------------- - - - - - - - - - - - - - - - +
     :                     Payload Data continued ...                :
     + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
     |                     Payload Data continued ...                |
     +---------------------------------------------------------------+

     https://tools.ietf.org/html/rfc6455#section-5.3
*/

#define WS_CTLF_FIN  (1<<7)
#define WS_CTLF_RSV1 (1<<6)
#define WS_CTLF_RSV2 (1<<5)
#define WS_CTLF_RSV3 (1<<4)

#define WS_OP_CONTINUE  0x00
#define WS_OP_TEXT      0x01
#define WS_OP_BIN       0x02
#define WS_OP_CLOSE     0x08
#define WS_OP_PING      0x09
#define WS_OP_PONG      0x0A

#define WS_FLAG_MASKED  (1<<7)

#define WS_CTRLF_MASK   (0xF0)
#define WS_OP_MASK      (0x0F)
#define WS_LENGTH_MASK  (0x7F)

#define WS_MIN_HDR_SZ  2 /* 4:ctlf 4:opcode 1:mask 7:payload len */


#endif