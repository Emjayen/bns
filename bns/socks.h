/*
 * socks.h
 *    SOCKS protocol definitions
 *
 */
#ifndef SOCKS_H
#define SOCKS_H
#include <pce\pce.h>



#pragma pack(1)

struct s4p_connect
{
	uib version;
	uib command;
	uiw dst_port;
	uid dst_addr;
	char ident[];
};

struct s4p_status
{
	uib unused;
	uib status;
	uib reserved[6];
};

struct s5p_connect
{
	uib version;
	uib count;
	uib methods[];
};

struct s5p_method
{
	uib version;
	uib method;
};


struct s5p_auth_request
{
	uib version;
	byte ect[]; /* (PSTRING) username (PSTRING) password */
};

struct s5p_auth_response
{
	uib version;
	uib status;
};

#pragma pack()



#endif