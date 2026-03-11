#include "TcpListener.h"
#include <iostream>

int TcpListener::init()
{
    WSADATA wsData;
    WORD ver = MAKEWORD(2, 2);
    int wsOk = WSAStartup(ver, &wsData);
    if (wsOk != 0) return wsOk;

    m_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_socket == INVALID_SOCKET) return WSAGetLastError();

    // Allow rapid restart without "address already in use"
    BOOL yes = TRUE;
    setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));

    sockaddr_in hint{};
    hint.sin_family = AF_INET;
    hint.sin_port   = htons(m_port);
    inet_pton(AF_INET, m_ipAddress, &hint.sin_addr);

    if (bind(m_socket, (sockaddr*)&hint, sizeof(hint)) == SOCKET_ERROR)
        return WSAGetLastError();

    if (listen(m_socket, SOMAXCONN) == SOCKET_ERROR)
        return WSAGetLastError();

    FD_ZERO(&m_master);
    FD_SET(m_socket, &m_master);
    return 0;
}

int TcpListener::run()
{
    m_running = true;
    struct timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 500000; // 500 ms — allows stop() to be noticed quickly

    while (m_running) {
        fd_set copy = m_master;
        int n = select(0, &copy, nullptr, nullptr, &timeout);
        for (int i = 0; i < n; i++) {
            SOCKET sock = copy.fd_array[i];
            if (sock == m_socket) {
                SOCKET client = accept(m_socket, nullptr, nullptr);
                if (client != INVALID_SOCKET) {
                    FD_SET(client, &m_master);
                    onClientConnected((int)client);
                }
            } else {
                char buf[8192]{};
                int bytesIn = recv(sock, buf, sizeof(buf) - 1, 0);
                if (bytesIn <= 0) {
                    onClientDisconnected((int)sock);
                    closesocket(sock);
                    FD_CLR(sock, &m_master);
                } else {
                    onMessageReceived((int)sock, buf, bytesIn);
                }
            }
        }
    }

    FD_CLR(m_socket, &m_master);
    closesocket(m_socket);
    while (m_master.fd_count > 0) {
        closesocket(m_master.fd_array[0]);
        FD_CLR(m_master.fd_array[0], &m_master);
    }
    WSACleanup();
    return 0;
}

void TcpListener::stop()                                           { m_running = false; }
void TcpListener::sendToClient(int s, const char* msg, int len)   { send((SOCKET)s, msg, len, 0); }
void TcpListener::onClientConnected(int)    {}
void TcpListener::onClientDisconnected(int) {}
void TcpListener::onMessageReceived(int, const char*, int) {}

void TcpListener::broadcastToClients(int sendingClient, const char* msg, int len)
{
    for (u_int i = 0; i < m_master.fd_count; i++) {
        SOCKET s = m_master.fd_array[i];
        if (s != m_socket && (int)s != sendingClient)
            sendToClient((int)s, msg, len);
    }
}
