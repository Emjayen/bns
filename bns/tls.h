/*
 * tls.h
 *
 */
#ifndef TLS_H
#define TLS_H
#include <pce\pce.h>
#define SECURITY_WIN32
#include <Windows.h>
#include <wintrust.h>
#include <schannel.h>
#include <sspi.h>
#include <security.h>




#define TLS_CTX_FLAG_INITIALIZED  (1<<0)
#define TLS_CTX_FLAG_HANDSHAKED   (1<<1)

struct tls_ctx
{
    CtxtHandle hCtx;
    uid header_sz;
    uid trailer_sz;
    uib flags;
};



/*
 * tls_startup
 *
 */
uib tls_startup();


/*
 * tls_handshake
 *
 */
uib tls_handshake(tls_ctx* pTlsCtx, byte* pBuffer, uid* pBytes, byte* pOutBuffer, uid* pOutBytes);


/*
 * tls_decrypt
 *
 */
uib tls_decrypt(tls_ctx* pTlsCtx, byte** ppBuffer, uid* pBytes, uid* pDecryptedLength);


/*
 * tls_encrypt
 *
 */
uib tls_encrypt(tls_ctx* pTlsCtx, byte* pBuffer, uid* pBytes);


/*
 * tls_cleanup
 *
 */
void tls_cleanup(tls_ctx* pTlsCtx);



#endif