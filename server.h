#ifndef _SERVER
#define _SERVER

#include "protocol.h"


typedef struct {
    int       fd;
    char      name[MAX_NAME + 1];
    Card      hand[DECK_SIZE];
    int       hand_size;
    int       connected;
    int       said_uno;          /* declared UNO while at <=2 cards   */
    int       uno_vulnerable;    /* hand==1 and hasn't said UNO       */
    int       has_drawn;         /* drew a card this turn already     */
    uint8_t   buf[NET_BUF_SIZE];
    int       buf_len;
} Player;




#endif