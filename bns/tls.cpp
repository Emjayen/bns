/*
 * tls.cpp
 *
 */
#include "tls.h"



#include "bns.h"


static CredHandle hCredHandle;
static SCHANNEL_CRED SChannelCred;
static TimeStamp CredExpireTime;



/*
 * tls_startup
 *
 */
uib tls_startup()
{
    DWORD Result;


    SChannelCred.dwVersion = SCHANNEL_CRED_VERSION;
    SChannelCred.grbitEnabledProtocols = SP_PROT_TLS1_2;
    SChannelCred.dwFlags = SCH_CRED_NO_DEFAULT_CREDS;

    if((Result = AcquireCredentialsHandle(NULL, (LPSTR) UNISP_NAME, SECPKG_CRED_OUTBOUND, NULL, &SChannelCred, NULL, NULL, &hCredHandle, &CredExpireTime)) != SEC_E_OK)
        return FALSE;

    return TRUE;
}


/*
 * tls_handshake
 *
 */
uib tls_handshake(tls_ctx* pTlsCtx, byte* pBuffer, uid* pBytes, byte* pOutBuffer, uid* pOutBytes)
{
    CtxtHandle* phCtx;
    SecBufferDesc* psbdIn;
    SecBufferDesc sbdOut;
    SecBuffer sbOut;
    SecBufferDesc sbdIn;
    SecBuffer sbIn[2];
    DWORD CtxAttrFlags;
    SECURITY_STATUS Status;
    
    
    sbOut.cbBuffer = *pOutBytes;
    sbOut.BufferType = SECBUFFER_TOKEN;
    sbOut.pvBuffer = pOutBuffer;

    sbdOut.ulVersion = SECBUFFER_VERSION;
    sbdOut.cBuffers = 1;
    sbdOut.pBuffers = &sbOut;

    if(pTlsCtx->flags & TLS_CTX_FLAG_INITIALIZED)
    {
        phCtx = &pTlsCtx->hCtx;
        psbdIn = &sbdIn;

        sbIn[0].cbBuffer = *pBytes;
        sbIn[0].BufferType = SECBUFFER_TOKEN;
        sbIn[0].pvBuffer = pBuffer;
        sbIn[1].cbBuffer = 0;
        sbIn[1].BufferType = SECBUFFER_EMPTY;
        sbIn[1].pvBuffer = NULL;

        sbdIn.ulVersion = SECBUFFER_VERSION;
        sbdIn.cBuffers = 2;
        sbdIn.pBuffers = sbIn;
    }

    else
    {
        phCtx = NULL;
        psbdIn = NULL;
    }

    Status = InitializeSecurityContext(
                              &hCredHandle,
                              phCtx,
                              (SEC_CHAR*) NULL,
                              ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_STREAM | ISC_REQ_MANUAL_CRED_VALIDATION,
                              NULL,
                              SECURITY_NATIVE_DREP,
                              psbdIn,
                              NULL,
                              &pTlsCtx->hCtx,
                              &sbdOut,
                              &CtxAttrFlags,
                              &CredExpireTime);

    LDBG("InitializeSecurityContext status: 0x%X", Status);

    pTlsCtx->flags |= TLS_CTX_FLAG_INITIALIZED;

    if(Status != SEC_E_OK && Status != SEC_I_CONTINUE_NEEDED && Status != SEC_E_INCOMPLETE_MESSAGE)
    {
        SetLastError(Status);
        return FALSE;
    }

    *pOutBytes = 0;

    if(Status == SEC_E_OK)
    {
        SecPkgContext_StreamSizes Sizes;

        if(QueryContextAttributes(phCtx, SECPKG_ATTR_STREAM_SIZES, &Sizes) != SEC_E_OK)
            return FALSE;

        pTlsCtx->header_sz = Sizes.cbHeader;
        pTlsCtx->trailer_sz = Sizes.cbTrailer;

        pTlsCtx->flags |= TLS_CTX_FLAG_HANDSHAKED;
    }

    else if(Status == SEC_I_CONTINUE_NEEDED)
    {
        *pOutBytes = sbOut.cbBuffer;
    }

    else if(Status == SEC_E_INCOMPLETE_MESSAGE)
    {
        *pBytes = 0;
    }

    else
    {
        return FALSE;
    }

    if(sbIn[1].BufferType == SECBUFFER_EXTRA)
    {
        *pBytes -= sbIn[1].cbBuffer;
    }

    return TRUE;
}



/*
 * tls_decrypt
 *
 */
uib tls_decrypt(tls_ctx* pTlsCtx, byte** ppBuffer, uid* pBytes, uid* pDecryptedLength)
{
    SecBufferDesc sbdBuffers;
    SecBuffer sbBuffers[4];
    SECURITY_STATUS Status;

    
    *pDecryptedLength = 0;

    sbdBuffers.ulVersion = SECBUFFER_VERSION;
    sbdBuffers.cBuffers = 4;
    sbdBuffers.pBuffers = sbBuffers;

    sbBuffers[0].cbBuffer = *pBytes;
    sbBuffers[0].BufferType = SECBUFFER_DATA;
    sbBuffers[0].pvBuffer = *ppBuffer;
    sbBuffers[1].BufferType = SECBUFFER_EMPTY;
    sbBuffers[2].BufferType = SECBUFFER_EMPTY;
    sbBuffers[3].BufferType = SECBUFFER_EMPTY;

    Status = DecryptMessage(&pTlsCtx->hCtx, &sbdBuffers, NULL, NULL);

    LDBG("DecryptMessage() status: 0x%X", Status);

    if(Status == SEC_E_INCOMPLETE_MESSAGE)
    {
        *pBytes = 0;
    }

    else if(Status == SEC_E_OK)
    {
        SecBuffer* pbData = NULL;
        SecBuffer* pbExtra = NULL;

        for(uib i = 0; i < ARRAYSIZE(sbBuffers); i++)
        {
            if(sbBuffers[i].BufferType == SECBUFFER_DATA) pbData = &sbBuffers[i];
            if(sbBuffers[i].BufferType == SECBUFFER_EXTRA) pbExtra = &sbBuffers[i];
        }

        if(pbData)
        {
            *ppBuffer = (byte*) pbData->pvBuffer;
            *pDecryptedLength = pbData->cbBuffer;
        }

        if(pbExtra)
        {
            *pBytes -= pbExtra->cbBuffer;
        }
    }

    else
    {
        SetLastError(Status);
        return FALSE;
    }


    return TRUE;
}


/*
 * tls_encrypt
 *
 */
uib tls_encrypt(tls_ctx* pTlsCtx, byte* pBuffer, uid* pBytes)
{
    SecBufferDesc sbdBuffers;
    SecBuffer sbBuffers[4];
    SECURITY_STATUS Status;


    sbdBuffers.ulVersion = SECBUFFER_VERSION;
    sbdBuffers.cBuffers = 4;
    sbdBuffers.pBuffers = sbBuffers;

    sbBuffers[0].cbBuffer = pTlsCtx->header_sz;
    sbBuffers[0].BufferType = SECBUFFER_STREAM_HEADER;
    sbBuffers[0].pvBuffer = pBuffer;

    sbBuffers[1].cbBuffer = *pBytes;
    sbBuffers[1].BufferType = SECBUFFER_DATA;
    sbBuffers[1].pvBuffer = ((byte*) sbBuffers[0].pvBuffer) + sbBuffers[0].cbBuffer;

    sbBuffers[2].cbBuffer = pTlsCtx->trailer_sz;
    sbBuffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
    sbBuffers[2].pvBuffer = ((byte*) sbBuffers[1].pvBuffer) + sbBuffers[1].cbBuffer;

    sbBuffers[3].cbBuffer = 0;
    sbBuffers[3].BufferType = SECBUFFER_EMPTY;
    sbBuffers[3].pvBuffer = NULL;


    if((Status = EncryptMessage(&pTlsCtx->hCtx, NULL, &sbdBuffers, NULL)) != SEC_E_OK)
    {
        SetLastError(Status);
        return FALSE;
    }

    *pBytes = sbBuffers[0].cbBuffer + sbBuffers[1].cbBuffer + sbBuffers[2].cbBuffer;

    return TRUE;
}




/*
 * tls_free
 *
 */
void tls_cleanup(tls_ctx* pTlsCtx)
{
    if(pTlsCtx->flags & TLS_CTX_FLAG_INITIALIZED)
        DeleteSecurityContext(&pTlsCtx->hCtx);

    pTlsCtx->flags = NULL;
}