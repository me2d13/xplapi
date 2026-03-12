#pragma once
#include <string>

// ---------------------------------------------------------------------------
// WebSocket protocol helpers (RFC 6455)
// ---------------------------------------------------------------------------

namespace WebSocket {

// Compute Sec-WebSocket-Accept from client's Sec-WebSocket-Key
std::string computeAcceptKey(const std::string& clientKey);

// Parse WebSocket frame from buffer. Returns payload as string, or empty on error/close/ping/pong.
// On return, *bytesConsumed is set to the number of bytes consumed from the buffer.
std::string parseFrame(const char* buf, int len, int* bytesConsumed);

// Create a text frame (opcode 0x1) to send to client. Server must not mask.
std::string createTextFrame(const std::string& payload);

}
