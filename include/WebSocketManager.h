#pragma once
#include "DataRefRegistry.h"
#include "WebSocket.h"
#include <string>
#include <vector>
#include <map>
#include <chrono>

// ---------------------------------------------------------------------------
// WebSocketManager — tracks /api/dataref/watch connections, sends dataref updates
// ---------------------------------------------------------------------------

class WebSocketManager {
public:
    void setRegistry(DataRefRegistry* reg) { m_registry = reg; }

    // Try to handle WebSocket upgrade. Returns true if handled (handshake sent).
    // On success, connection is tracked for subsequent data/sends.
    bool tryUpgrade(int clientSocket, const char* request, int len);

    // Returns true if socket is a known WebSocket connection
    bool hasConnection(int clientSocket) const;

    // Handle incoming WebSocket frame (first message = register datarefs).
    // Returns true if connection was closed (caller should FD_CLR).
    bool handleData(int clientSocket, const char* buf, int len);

    // Remove connection (on close/error)
    void removeConnection(int clientSocket);

    // Send updates to all connections. Returns list of sockets that failed (caller should remove/close them).
    std::vector<int> sendUpdates();

private:
    struct Connection {
        std::vector<std::string> datarefNames;
        int intervalMs;
        bool alwaysUpdate;
        std::chrono::steady_clock::time_point lastSend;
        std::string lastJson;  // for change detection when !alwaysUpdate
    };

    DataRefRegistry* m_registry { nullptr };
    std::map<int, Connection> m_connections;

    static bool parseUpgradeRequest(const char* req, int len,
        std::string* outKey, std::string* outPath);
    static int parseIntervalFromPath(const std::string& path);
    static bool parseAlwaysUpdateFromPath(const std::string& path);
    std::string buildJsonForNames(const std::vector<std::string>& names) const;
};
