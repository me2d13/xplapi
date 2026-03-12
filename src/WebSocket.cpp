#include "WebSocket.h"
#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "advapi32.lib")

#include <algorithm>
#include <cstring>

namespace WebSocket {

static const char MAGIC[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static std::string base64Encode(const unsigned char* data, size_t len)
{
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        unsigned n = (unsigned)data[i] << 16;
        if (i + 1 < len) n |= (unsigned)data[i + 1] << 8;
        if (i + 2 < len) n |= (unsigned)data[i + 2];
        out += B64[(n >> 18) & 63];
        out += B64[(n >> 12) & 63];
        out += (i + 1 < len) ? B64[(n >> 6) & 63] : '=';
        out += (i + 2 < len) ? B64[n & 63] : '=';
    }
    return out;
}

std::string computeAcceptKey(const std::string& clientKey)
{
    std::string toHash = clientKey + MAGIC;

    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    if (!CryptAcquireContext(&prov, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        return {};
    if (!CryptCreateHash(prov, CALG_SHA1, 0, 0, &hash)) {
        CryptReleaseContext(prov, 0);
        return {};
    }
    if (!CryptHashData(hash, (const BYTE*)toHash.data(), (DWORD)toHash.size(), 0)) {
        CryptDestroyHash(hash);
        CryptReleaseContext(prov, 0);
        return {};
    }
    BYTE sha1[20];
    DWORD sha1Len = 20;
    if (!CryptGetHashParam(hash, HP_HASHVAL, sha1, &sha1Len, 0)) {
        CryptDestroyHash(hash);
        CryptReleaseContext(prov, 0);
        return {};
    }
    CryptDestroyHash(hash);
    CryptReleaseContext(prov, 0);

    return base64Encode(sha1, 20);
}

std::string parseFrame(const char* buf, int len, int* bytesConsumed)
{
    *bytesConsumed = 0;
    if (len < 2) return {};

    unsigned char b0 = (unsigned char)buf[0];
    unsigned char b1 = (unsigned char)buf[1];
    bool masked = (b1 & 0x80) != 0;
    uint64_t payloadLen = b1 & 0x7F;

    int headerLen = 2;
    if (payloadLen == 126) {
        if (len < 4) return {};
        payloadLen = ((unsigned char)buf[2] << 8) | (unsigned char)buf[3];
        headerLen = 4;
    } else if (payloadLen == 127) {
        if (len < 10) return {};
        payloadLen = 0;
        for (int i = 0; i < 8; i++) payloadLen = (payloadLen << 8) | (unsigned char)buf[2 + i];
        headerLen = 10;
    }

    int maskOffset = 0;
    if (masked) {
        maskOffset = headerLen;
        headerLen += 4;
    }

    if (len < headerLen + (int)payloadLen) return {};
    *bytesConsumed = headerLen + (int)payloadLen;

    int opcode = b0 & 0x0F;
    if (opcode == 0x8) return {};  // close
    if (opcode == 0x9 || opcode == 0xA) return {};  // ping/pong - ignore for now

    std::string payload;
    payload.reserve((size_t)payloadLen);
    for (uint64_t i = 0; i < payloadLen; i++) {
        char c = buf[headerLen + (int)i];
        if (masked) c ^= buf[maskOffset + (i % 4)];
        payload += c;
    }
    return payload;
}

std::string createTextFrame(const std::string& payload)
{
    size_t len = payload.size();
    std::string frame;
    frame.reserve(2 + (len < 126 ? 0 : (len < 65536 ? 2 : 8)) + len);

    frame += (char)0x81;  // FIN + text opcode
    if (len < 126) {
        frame += (char)(len & 0xFF);
    } else if (len < 65536) {
        frame += (char)126;
        frame += (char)((len >> 8) & 0xFF);
        frame += (char)(len & 0xFF);
    } else {
        frame += (char)127;
        for (int i = 7; i >= 0; i--)
            frame += (char)((len >> (i * 8)) & 0xFF);
    }
    frame += payload;
    return frame;
}

}
