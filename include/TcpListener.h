#pragma once
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

// ---------------------------------------------------------------------------
// TcpListener — select()-based multi-client TCP server
//   Subclass and override onMessageReceived / onClientConnected /
//   onClientDisconnected to handle events.
//
//   Call init() once, then run() in a dedicated thread; call stop() to exit.
// ---------------------------------------------------------------------------
class TcpListener {
public:
    TcpListener() : m_ipAddress("0.0.0.0"), m_port(8012), m_running(false), m_socket(INVALID_SOCKET) {}
    virtual ~TcpListener() = default;

    int  init();
    int  run();
    void stop();

    void setPort(int port)               { m_port = port; }
    void setIpAddress(const char* addr)  { m_ipAddress = addr; }

protected:
    virtual void onClientConnected(int clientSocket);
    virtual void onClientDisconnected(int clientSocket);
    // Return true to close connection (FD_CLR), false to keep open (WebSocket)
    virtual bool onMessageReceived(int clientSocket, const char* msg, int length);
    virtual void onLoopTick() {}

    void sendToClient(int clientSocket, const char* msg, int length);
    void broadcastToClients(int sendingClient, const char* msg, int length);
    void removeClientSocket(int clientSocket);

private:
    const char* m_ipAddress;
    int         m_port;
    SOCKET      m_socket;
    fd_set      m_master;
    bool        m_running;
};
