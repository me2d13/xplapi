#include "WebSocketManager.h"
#include "json.hpp"
#include "XPLMDataAccess.h"
#include <WinSock2.h>
#include <sstream>
#include <algorithm>
#include <cstring>

using json = nlohmann::json;

bool WebSocketManager::parseUpgradeRequest(const char* req, int len,
    std::string* outKey, std::string* outPath)
{
    std::string text(req, len);
    auto lineEnd = text.find("\r\n");
    if (lineEnd == std::string::npos) return false;

    std::istringstream rl(text.substr(0, lineEnd));
    std::string method, path, version;
    rl >> method >> path >> version;
    if (method != "GET") return false;

    auto qpos = path.find('?');
    *outPath = (qpos != std::string::npos) ? path : path;

    const char* keyHeader = "Sec-WebSocket-Key:";
    auto keyPos = text.find(keyHeader);
    if (keyPos == std::string::npos)
        keyPos = text.find("sec-websocket-key:");
    if (keyPos == std::string::npos) return false;

    size_t keyStart = keyPos + strlen(keyHeader);
    while (keyStart < text.size() && text[keyStart] == ' ') keyStart++;
    size_t keyEnd = keyStart;
    while (keyEnd < text.size() && text[keyEnd] != '\r' && text[keyEnd] != '\n') keyEnd++;
    *outKey = text.substr(keyStart, keyEnd - keyStart);
    return !outKey->empty();
}

int WebSocketManager::parseIntervalFromPath(const std::string& path)
{
    auto pos = path.find("interval=");
    if (pos == std::string::npos) return 1000;  // default 1 sec
    pos += 9;
    int val = 0;
    while (pos < (int)path.size() && path[pos] >= '0' && path[pos] <= '9') {
        val = val * 10 + (path[pos] - '0');
        pos++;
    }
    if (val <= 0) val = 1;
    if (val > 60) val = 60;
    return val * 1000;  // convert to ms
}

bool WebSocketManager::parseAlwaysUpdateFromPath(const std::string& path)
{
    auto pos = path.find("alwaysUpdate=true");
    if (pos != std::string::npos) return true;
    pos = path.find("alwaysupdate=true");
    return pos != std::string::npos;
}

bool WebSocketManager::tryUpgrade(int clientSocket, const char* request, int len)
{
    std::string path = "/api/dataref/watch";
    if (path.size() > (size_t)len) return false;
    if (memcmp(request, "GET ", 4) != 0) return false;
    auto pathStart = (const char*)memchr(request, ' ', len);
    if (!pathStart || pathStart - request + path.size() + 1 > (size_t)len) return false;
    if (memcmp(pathStart + 1, path.c_str(), path.size()) != 0) return false;
    char c = pathStart[1 + path.size()];
    if (c != ' ' && c != '?' && c != '\r') return false;

    size_t pathLen = (size_t)(request + len - pathStart - 1);
    if (pathLen > 256) pathLen = 256;
    std::string fullPath(pathStart + 1, pathStart + 1 + pathLen);
    size_t end = fullPath.find('\r');
    if (end != std::string::npos) fullPath = fullPath.substr(0, end);

    std::string clientKey, outPath;
    if (!parseUpgradeRequest(request, len, &clientKey, &outPath)) return false;

    std::string accept = WebSocket::computeAcceptKey(clientKey);
    if (accept.empty()) return false;

    std::ostringstream resp;
    resp << "HTTP/1.1 101 Switching Protocols\r\n"
         << "Upgrade: websocket\r\n"
         << "Connection: Upgrade\r\n"
         << "Sec-WebSocket-Accept: " << accept << "\r\n"
         << "\r\n";

    std::string out = resp.str();
    send((SOCKET)clientSocket, out.c_str(), (int)out.size(), 0);

    Connection conn;
    conn.intervalMs = parseIntervalFromPath(fullPath);
    conn.alwaysUpdate = parseAlwaysUpdateFromPath(fullPath);
    conn.lastSend = std::chrono::steady_clock::now();
    m_connections[clientSocket] = conn;
    return true;
}

bool WebSocketManager::hasConnection(int clientSocket) const
{
    return m_connections.find(clientSocket) != m_connections.end();
}

bool WebSocketManager::handleData(int clientSocket, const char* buf, int len)
{
    auto it = m_connections.find(clientSocket);
    if (it == m_connections.end()) return false;

    int consumed = 0;
    std::string payload = WebSocket::parseFrame(buf, len, &consumed);
    if (payload.empty() && consumed > 0) {
        removeConnection(clientSocket);
        closesocket((SOCKET)clientSocket);
        return true;  // connection closed
    }
    if (payload.empty()) return false;

    try {
        auto j = json::parse(payload);
        std::vector<std::string> names;
        if (j.is_array()) {
            for (const auto& v : j) {
                if (v.is_string()) names.push_back(v.get<std::string>());
            }
        } else if (j.is_object() && j.contains("names")) {
            for (const auto& v : j["names"]) {
                if (v.is_string()) names.push_back(v.get<std::string>());
            }
        }
        for (const auto& n : names)
            if (m_registry) m_registry->ensureTracked(n);
        it->second.datarefNames = std::move(names);
    } catch (...) {}
    return false;
}

void WebSocketManager::removeConnection(int clientSocket)
{
    m_connections.erase(clientSocket);
}

std::string WebSocketManager::buildJsonForNames(const std::vector<std::string>& names) const
{
    if (!m_registry) return "{}";
    json obj;
    for (const auto& name : names) {
        DataRefEntry entry;
        if (m_registry->readValue(name, entry) && entry.found) {
            if (entry.type & xplmType_FloatArray) {
                json arr = json::array();
                for (int i = 0; i < (entry.count < 64 ? entry.count : 64); i++)
                    arr.push_back(entry.value.fArrayValue[i]);
                obj[name] = arr;
            } else if (entry.type & xplmType_IntArray) {
                json arr = json::array();
                for (int i = 0; i < (entry.count < 64 ? entry.count : 64); i++)
                    arr.push_back(entry.value.iArrayValue[i]);
                obj[name] = arr;
            } else if (entry.type & xplmType_Data) {
                obj[name] = std::string(entry.value.cArrayValue);
            } else if (entry.type & xplmType_Float) {
                obj[name] = entry.value.fValue;
            } else if (entry.type & xplmType_Int) {
                obj[name] = entry.value.iValue;
            } else if (entry.type & xplmType_Double) {
                obj[name] = (double)entry.value.fValue;
            } else {
                obj[name] = nullptr;
            }
        } else {
            obj[name] = nullptr;
        }
    }
    return obj.dump();
}

std::vector<int> WebSocketManager::sendUpdates()
{
    std::vector<int> closed;
    if (!m_registry) return closed;
    auto now = std::chrono::steady_clock::now();

    for (auto it = m_connections.begin(); it != m_connections.end(); ) {
        int sock = it->first;
        Connection& conn = it->second;

        if (conn.datarefNames.empty()) {
            ++it;
            continue;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - conn.lastSend).count();
        if (elapsed < conn.intervalMs) {
            ++it;
            continue;
        }

        std::string json = buildJsonForNames(conn.datarefNames);
        if (!conn.alwaysUpdate && json == conn.lastJson) {
            conn.lastSend = now;
            ++it;
            continue;
        }
        conn.lastJson = json;
        conn.lastSend = now;

        std::string frame = WebSocket::createTextFrame(json);
        int sent = send((SOCKET)sock, frame.c_str(), (int)frame.size(), 0);
        if (sent <= 0 || sent != (int)frame.size()) {
            it = m_connections.erase(it);
            closed.push_back(sock);
        } else {
            ++it;
        }
    }
    return closed;
}
