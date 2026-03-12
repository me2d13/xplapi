#pragma once
#include "TcpListener.h"
#include "DataRefRegistry.h"
#include "StaticFileServer.h"
#include "WebSocketManager.h"
#include <string>
#include <thread>

// ---------------------------------------------------------------------------
// WebServer — HTTP layer on top of TcpListener
//
// Routes:
//   GET  /                        → status HTML page
//   GET  /api/dataref?name=...    → read single dataref (JSON)
//   POST /api/dataref/get         → body: {"name":"..."} → JSON
//   POST /api/dataref/getMultiple → body: ["name1","name2"] → JSON
// ---------------------------------------------------------------------------
class WebServer : public TcpListener {
public:
    WebServer() = default;

    void setRegistry(DataRefRegistry* reg) { m_registry = reg; m_wsManager.setRegistry(reg); }
    void setPluginDir(const std::string& dir) { m_staticServer.setPluginDir(dir); }

protected:
    bool onMessageReceived(int clientSocket, const char* msg, int length) override;
    void onClientConnected(int clientSocket) override    {}
    void onClientDisconnected(int clientSocket) override;
    void onLoopTick() override;

private:
    DataRefRegistry* m_registry { nullptr };
    StaticFileServer m_staticServer;
    WebSocketManager m_wsManager;

    void processRequest(int clientSocket, const std::string& msgStr);

    // Route handlers – return response body; set contentType and statusCode
    std::string handleStatusPage();
    std::string handleGetOne(const std::string& name, int& statusCode);
    std::string handleGetOneBody(const std::string& body, int& statusCode);
    std::string handleGetMultiple(const std::string& body, int& statusCode);
    std::string handleSet(const std::string& body, int& statusCode);
    std::string handleSetMultiple(const std::string& body, int& statusCode);
    std::string handleCommand(const std::string& path, const std::string& body, int& statusCode);

    // Helpers
    void        sendHttp(int sock, int status, const std::string& contentType,
                         const std::string& body);
    void        sendRedirect(int sock, const std::string& location);

    static std::string urlDecode(const std::string& s);
    static std::string queryParam(const std::string& path, const std::string& key);
};
