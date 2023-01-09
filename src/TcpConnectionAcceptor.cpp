#include <winsock2.h>
#include <windows.h>

#include <iostream>
#include <thread>
#include <cstdio>
#include <signal.h>
#include <errno.h>

#include <vector>
#include <mutex>
#include <string>

#include "TcpConnectionAcceptor.h"
#include "TcpConnectionPool.h"

/* For wepoll documentation refer to: https://github.com/piscisaureus/wepoll */
#include "imports/wepoll/wepoll.h"
#include "client.h"

// Global handle function for all connections made
functionPtr_t *handle_function = nullptr;

TcpConnectionAcceptor::TcpConnectionAcceptor(functionPtr_t *_handle_function, const char *ip, int port, int connection_pool_size){
    handle_function = _handle_function;
    acceptSocket = new_socket = 0;
    this->connection_pool_size = connection_pool_size;

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(ip); // INADDR_ANY;
    server.sin_port = htons(port);
    this->ip = ip;
    this->port = port;


    //this->acceptSocket = socket(AF_INET, SOCK_STREAM, 0);
    this->acceptSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (this->acceptSocket == INVALID_SOCKET){
        printf("Could not create socket: error %d\n", WSAGetLastError());
        throw;
    }


    if (bind(this->acceptSocket, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR){
        printf("Error binding accept-socket to server ip and port WSA-errcode:%d\n",  WSAGetLastError());
        throw;
    }

    if (listen(this->acceptSocket, 5) == SOCKET_ERROR){
        printf("Error listening to connections WSA-errcode:%d\n",  WSAGetLastError());
        throw;
    }
    
    // TODO: remove if supporting tcp packets optimization.
    int val = 1;
    int result = setsockopt(this->acceptSocket, IPPROTO_TCP, TCP_NODELAY, (char *)&val, sizeof(int));
    

    // Init epoll
    // parameter given doesn't matter (deprecated) but must be greater than 0.
    this->epoll_handle = epoll_create(1);
    if (this->epoll_handle == nullptr){
        // Error
        printf("Couldn't create epoll handle in TcpConnectionAcceptor::TcpConnectionAcceptor()\n");
        throw;
    }
    

    // Add socket event to epoll port
    this->event.events = EPOLLIN;
    this->event.data.sock = this->acceptSocket;
    if (epoll_ctl(this->epoll_handle, EPOLL_CTL_ADD, this->acceptSocket, &this->event) == -1){
        // error
        printf("Error adding epoll event to handle incoming requests\n");
        epoll_close(this->epoll_handle);
        throw;
    }

    // Set server start time
    this->server_starttime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    
    this->run_threadpools();
}

static void startConnectionPool(ConnectionPool *p){
    printf("Running server thread %d\n", p->id);
    p->serveForever();
    printf("Shutdown server on thread %d\n", p->id);
}

void TcpConnectionAcceptor::run_threadpools(){
    // Initialize thread connection pools (each pool runs on a different thread)
    for (int i = 0; i < this->connection_pool_size; i++){

        ConnectionPool *p = new ConnectionPool(i, "Login server");
        this->thread_connectionpool.push_back(p);
        std::thread t(startConnectionPool, std::ref(p));
        t.detach();
        // Sleep a few MS in order to let thread fully initialize 
        // otherwise it might lose the connectionPool reference given to it.
        // TODO: fix better
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    printf("Server online (%s:%d) with %d thread(s)\n", ip, port, connection_pool_size);
}

TcpConnectionAcceptor::~TcpConnectionAcceptor(){
    epoll_close(this->epoll_handle);
}

int TcpConnectionAcceptor::getTimeMS(){
    /* returns time in milliseconds since server start */
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - this->server_starttime;
}


void TcpConnectionAcceptor::serveForever(){

   
    int c = sizeof(struct sockaddr_in);

    int socketError = false;
    int timeout_ms = 10000;

    while (this->running) {
        // Update global server state here
        this->update();


        /*  Timeout values for epoll_wait:
            <0  block indefinitely.
            0   report any events that are already waiting, but don't block.
            >=1 block for at most N milliseconds

            Return values:
            -1  error occurred
            0   timed out without any events to report
            >=1 number of events stored in the epoll_evnt buffer
        */
        int eventCount = epoll_wait(this->epoll_handle, this->epoll_events, this->num_epoll_events, timeout_ms);

        // Timed out
        if (eventCount == 0) continue;

        else if (eventCount > 0){
            for (int i = 0; i < eventCount; i++){

                if (this->epoll_events[i].events != EPOLLIN){
                    // Socket closed, hang-up, socket error
                    socketError = true;
                    break;
                }
                else {
                    
                    // Incoming data available, or incoming connection ready to be accepted
                    new_socket = accept(this->acceptSocket, (struct sockaddr *)&client, &c);
                    if (new_socket == INVALID_SOCKET)
                    {
                        printf("accept failed with error code : %d\n" , WSAGetLastError());
                        continue;
                    }

                    this->handleNewConnection(new_socket, ((struct sockaddr *)&client));
                }
            }
            if (socketError){
                // TODO: handle error
            }
        }
        else if (eventCount <= -1){
            // Error occurred
            printf("error occurred in epoll_wait()\n");
        }
    }
    
}


void TcpConnectionAcceptor::handleNewConnection(SOCKET newSocket, struct sockaddr *newSockAddr) {
    /*  Handles new connections made with given socket.
        Creates a new Client and determines which connection-pool-thread that should handle its connection.
        Note:
            Sleeps if over set accept rate.
    */
    int startms = getTimeMS();
    Client *client = new Client(newSocket, newSockAddr, this->connectionCount);

    // New client has been connected, so we add it to unauthorized client list
    connections.push_back(client);

    // Get thread with least connections
    ConnectionPool *cp = this->getConnectionPool();
    client->connection_pool = cp; 
    cp->addNewConnection(client);
    this->connectionCount++;

    if (accept_rate > 0){
        // Calculate time to sleep based on accept rate. 
        // Subtract time it takes to add new connection.
        int ms_sleep = (int)max((1000 / accept_rate) - (getTimeMS() - startms), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(ms_sleep));
    }
}

TcpConnectionAcceptor::~TcpConnectionAcceptor(){
    int sumClosed = 0;
    // First set running to false and let all threads gracefully exit
    // TODO: fix better
    for (auto cp : this->thread_connectionpool){
        cp->running = false;
    }
    // Sleep for 2 seconds
    std::this_thread::sleep_for(std::chrono::milliseconds(1000*2));

    // Loop again and shut down completely
    for (auto cp : this->thread_connectionpool){
        sumClosed += cp->shutdown();
        delete cp;
    }
    int sumDeleted = 0, total = this->connections.size();
    for (auto c : this->connections){
        if (c->referenceCount <= 0){
            sumDeleted++;
            delete c;
        }
    }

    if (sumClosed > 0)
        printf("Successfully shutdown %d clients\n", sumClosed);
    if (sumDeleted > 0){
        printf("Successfully deleted %d/%d clients\n", sumDeleted, total);
    }
}



ConnectionPool *TcpConnectionAcceptor::getConnectionPool(){
    // Get thread with least connections in its pool
    int idx = 0;
    int maxc = INT32_MAX;
    for (int i = 0; i < this->connection_pool_size; i++){
        if (this->thread_connectionpool[i]->size < maxc){
            maxc = this->thread_connectionpool[i]->size;
            idx = i;
        }
    }
    return this->thread_connectionpool[idx];
}




















// end