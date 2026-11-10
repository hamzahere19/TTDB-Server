#pragma once


#include <string>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>









constexpr int32_t MAX_VARS_PER_FRAME = 16;
constexpr int32_t MAX_STACK_DEPTH = 64;
constexpr int IO_BUFFER_SIZE = 64 * 1024;

constexpr uint8_t STATUS_OK = 0;
constexpr uint8_t STATUS_ERROR = 1;
constexpr size_t TRAILER_SIZE = 4;






struct Variable
{
    std::string name;
    int32_t value;
};

struct Frame
{
    std::string func_name;
    int32_t argc;
    Variable argv[MAX_VARS_PER_FRAME];
    int32_t returnLine;
    Variable locals[MAX_VARS_PER_FRAME];
    int32_t localCount;
};

struct Snapshot
{
    Frame callStack[MAX_STACK_DEPTH];
    int32_t stackDepth;
};

struct TTDBHeader
{
    char magic[4];
    int32_t version;
    int32_t stepCount;
    int64_t indexOffset;
};
constexpr int64_t TTDB_HEADER_BYTES = 20;





// Helper.h should contain the structs, functions, constants, etc. that are used more than one time in the project. For example, the structs that are used in both server and client, the constants that are used in all three files. 

struct FuncEntry
{
    std::string funcName;
    int64_t byteOffsetInResolveBin;
};
struct PendingPatch
{
    int64_t byteOffsetOfOffsetField;
    std::string targetFuncName;
};





class Crc32
{
    uint32_t state;

    struct Table
    {
        uint32_t t[256];
        Table()
        {
            for (uint32_t i = 0; i < 256; i++)
            {
                uint32_t c = i;
                for (int k = 0; k < 8; k++)
                {
                    if (c & 1)
                    {
                        c = 0xEDB88320u ^ (c >> 1);
                    }
                    else
                    {
                        c = c >> 1;
                    }
                }
                t[i] = c;
            }
        }
    };

    static const Table &table()
    {
        static const Table tbl;
        return tbl;
    }

public:
    Crc32() : state(0xFFFFFFFFu) {}

    void update(const void *data, size_t n)
    {
        const uint8_t *p = static_cast<const uint8_t *>(data);
        const Table &tb = table();
        for (size_t i = 0; i < n; i++)
        {
            state = tb.t[(state ^ p[i]) & 0xFF] ^ (state >> 8);
        }
    }

    uint32_t value() const
    {
        return state ^ 0xFFFFFFFFu;
    }
};






inline void crcToBytes(uint32_t crc, char out[4])
{
    out[0] = (char)((crc >> 24) & 0xFF);
    out[1] = (char)((crc >> 16) & 0xFF);
    out[2] = (char)((crc >> 8) & 0xFF);
    out[3] = (char)(crc & 0xFF);
}

inline uint32_t crcFromBytes(const char in[4])
{
    return ((uint32_t)(uint8_t)in[0] << 24) |
           ((uint32_t)(uint8_t)in[1] << 16) |
           ((uint32_t)(uint8_t)in[2] << 8) |
           (uint32_t)(uint8_t)in[3];
}








class TrailerChecker
{
    Crc32 crc;
    char tail[TRAILER_SIZE];
    size_t tailLen;

public:
    TrailerChecker() : tailLen(0) {}

    void feed(const char *buf, size_t n, std::string &safe)
    {
        std::string tmp(tail, tailLen);
        tmp.append(buf, n);
        if (tmp.size() <= TRAILER_SIZE)
        {
            memcpy(tail, tmp.data(), tmp.size());
            tailLen = tmp.size();
            return;
        }
        size_t releasable = tmp.size() - TRAILER_SIZE;
        crc.update(tmp.data(), releasable);
        safe.append(tmp.data(), releasable);
        memcpy(tail, tmp.data() + releasable, TRAILER_SIZE);
        tailLen = TRAILER_SIZE;
    }

    bool verify() const
    {
        if (tailLen != TRAILER_SIZE)
        {
            return false;
        }
        return crcFromBytes(tail) == crc.value();
    }

    const char *heldTrailer() const
    {
        return tail;
    }
};





inline bool sendAll(int sock, const char *buf, size_t n)
{
    size_t sent = 0;
    while (sent < n)
    {
        ssize_t r = send(sock, buf + sent, n - sent, MSG_NOSIGNAL);
        if (r <= 0)
        {
            return false;
        }
        sent += (size_t)r;
    }
    return true;
}

inline bool recvAll(int sock, char *buf, size_t n)
{
    size_t got = 0;
    while (got < n)
    {
        ssize_t r = recv(sock, buf + got, n - got, 0);
        if (r <= 0)
        {
            return false;
        }
        got += (size_t)r;
    }
    return true;
}






inline bool sendMessage(int sock, uint8_t status, const std::string &body)
{
    Crc32 c;
    c.update(&status, 1);
    c.update(body.data(), body.size());
    char t[4];
    crcToBytes(c.value(), t);

    if (!sendAll(sock, (const char *)&status, 1))
    {
        return false;
    }
    if (!sendAll(sock, body.data(), body.size()))
    {
        return false;
    }
    return sendAll(sock, t, 4);
}