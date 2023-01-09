#ifndef _TCPCONNECTION_ACCEPTOR_H
#define _TCPCONNECTION_ACCEPTOR_H

#include <winsock2.h>
#include <windows.h>
#include "imports/wepoll/wepoll.h"
#include <vector>

class ConnectionPool;
class Client;
class Packet;


using functionPtr_t = void(*)(Client *, Packet *);
// Global handle function
extern functionPtr_t handle_function;

class TcpConnectionAcceptor{
public:
    TcpConnectionAcceptor(functionPtr_t *handle_function, const char *ip, int port, int connection_pool_size);
    ~TcpConnectionAcceptor();
    void shutdown() {this->running = false;}
    void serveForever();
    // Returns time in MS since server start
    int getTimeMS();
    

    /*  Define abstract function to be overridden ( = 0)
        Anyone who inherits this class must implement this function. */
    virtual void handleNewConnection(SOCKET newSocket, struct sockaddr *newSockAddr);
    // Expected override for login acceptor only
    virtual int update(){return 0;} 


protected:
    void run_threadpools();

    /* Returns the pool thread with least connections in its pool, given size of the pool */
    ConnectionPool *getConnectionPool();

    SOCKET acceptSocket, new_socket;
    struct sockaddr_in server, client;

    // Time when server started running
    unsigned long long server_starttime;


    bool running = true;
    int port;
    const char *ip;
    int connection_pool_size = 20;
    unsigned long long connectionCount = 0;
    // Max number of new sockets accepted per second. Set <= 0 for unlimited accept rate
    int accept_rate = -1;

    HANDLE epoll_handle = nullptr;
    static const int num_epoll_events = 20; // Config::maxConcurrentAcceptions
    struct epoll_event event, epoll_events[num_epoll_events];

    // List of server thread pools running
    std::vector<ConnectionPool *> thread_connectionpool;
    // List of all accepted connections
    std::vector<Client *> connections;
};


/* World */
/*
class WorldConnectionAcceptor: public TcpConnectionAcceptor{
public:
    WorldConnectionAcceptor(const char *ip, int port);
    ~WorldConnectionAcceptor();
    void handleNewConnection(SOCKET newSocket, struct sockaddr *newSockAddr) override;

private:
    // Returns the pool thread with least connections in its pool
    WorldConnectionPool *getConnectionPool();
};
*/











#endif
