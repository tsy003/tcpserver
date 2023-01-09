#include <mutex>
#include "TcpConnectionPool.h"
#include "client.h"
//#include "packet.h"
#include "TcpConnectionAcceptor.h"



ConnectionPool::ConnectionPool(int id, const char *serverName){
	this->running = true;
    this->id = id;
    this->serverName = serverName;

    // Thread safe lock free queue with fixed size 100
    // Only thread safe with 2 threads (1 enqueue 1 dequeue)
    this->newConnectionsQueue = new moodycamel::ReaderWriterQueue<Client *>(100);

    // Init epoll
    // parameter given doesn't matter (deprecated) but must be greater than 0.
    this->epoll_handle = epoll_create(1);
    if (this->epoll_handle == nullptr){
        // Error
        printf("[%s] Couldn't create epoll handle in ConnectionPool::ConnectionPool()\n", this->serverName);
        throw;
    }
    // Add socket type event for epoll
    this->event.events = EPOLLIN;
}
ConnectionPool::~ConnectionPool(){
    this->shutdown();
    delete this->newConnectionsQueue;
    
}

void ConnectionPool::addNewConnection(Client *client){
    /* Adds new connection to this pool. Uses thread safe queue to pass client along. */

    client->referenceCount++; // Pool thread will have a reference to this client (atomic variable)

    if (!this->newConnectionsQueue->try_enqueue(client)){
        printf("[Error] Connection queue is full for %s\n", this->serverName);
    }
}

void ConnectionPool::checkNewConnections(){
    /* Adds new connection to this pool. Uses thread safe queue to ensure thread safety. 
        Note: this queue is only safe with 1 enqueue thread and 1 dequeue thread. */

    Client *client;
    // try_dequeue will return false when queue is empty
    while (this->newConnectionsQueue->try_dequeue(client)){
        // Does not need to add referenceCount to this new client. It has already been 
        // added for this thread by another as the client was transitioned into 'newConnectionsQueue'.
        this->clients.push_back(client);
        // Add connection on this socket for this pool
        this->event.data.sock = client->client_socket;
        if (epoll_ctl(this->epoll_handle, EPOLL_CTL_ADD, client->client_socket, &this->event) == -1){
            printf("[%s] Error could not add new connection in ConnectionPool::checkNewConnections()\n", this->serverName);
            client->close();
        }
        else {
            printf("[%s] Added new connection: socket %d\n", this->serverName, (int)client->client_socket);
            // TODO: create function
            // this->onConnectionInit();

            // Increase size atomically, cause it might be read by acceptor thread
            // such that it can be able to determine which thread has the lowest workload.
            this->size++;
        }
    }
}


int ConnectionPool::shutdown(){
    /* Shuts down all connections and stops running. Returns number of clients shut down */
	this->running = false;
    int count = 0;
    for (Client *c : this->clients){
        count += this->closeConnection(c);
    }

    if (this->epoll_handle != nullptr) epoll_close(this->epoll_handle);
    return count;
}

int ConnectionPool::closeConnection(Client *c){
    /* Closes connection with given client, returns 1 if closed successfully, 0 otherwise. */
    //if (c->isClosed()) return 0;
    for (int i = 0; i < this->clients.size(); i++) {
        if (c == this->clients[i]) {
            // Remove from list
            this->clients.erase(this->clients.begin()+i);
            // Remove client from the epoll set explictly
            epoll_ctl(this->epoll_handle, EPOLL_CTL_DEL, c->client_socket, nullptr);
            

            // Reduce reference count to this client as we no longer store a reference to it.
            c->referenceCount--;
            // Reduce current pool size
            this->size--;
            
            printf("[%s] Closed client connection\n", this->serverName);
            return 1;
        }
    }
    return 0;
}
void ConnectionPool::removeFromList(Client *c){
    /* Removes client from pool of clients. Does not close the connection with the client (useful when migrating servers). */
    //if (c->isClosed()) return;
    for (int i = 0; i < this->clients.size(); i++) {
        if (c == this->clients[i]) {
            // Remove from list
            this->clients.erase(this->clients.begin()+i);

            // Reduce reference count to this client as we no longer store a reference to it.
            c->referenceCount--;
            // Reduce current pool size
            this->size--;
            return;
        }
    }
    return;
}
void ConnectionPool::addToList(Client *c){
    /* Adds client to list */
    //if (c->isClosed()) return;
    this->clients.push_back(c);
    // Add counts
    c->referenceCount++;
    this->size++;
}


void ConnectionPool::serveForever(){
    // TODO: ensure this is enough bytes for all types of packets
    int buffer_size = 4096*10;
    char *recv_buffer = new char[buffer_size]();
    int num_bytes = 0;
    int timeout_ms = 500;

    while (this->running){

        int eventCount = epoll_wait(this->epoll_handle, this->epoll_events, this->num_epoll_events, timeout_ms);

        // Timed out
        if (eventCount == 0);

        else if (eventCount > 0){
            // Received events up to max of 'num_epoll_events'
            for (int i = 0; i < eventCount; i++){

                SOCKET client_socket = this->epoll_events[i].data.sock;
                Client *client = this->getClientFromSocket(client_socket);
                if (client == nullptr){
                    // Should never happen
                    printf("[%s] getClientFromSocket() returned nullptr with socket: %d in ConnectionPool::serveForever()\n", this->serverName, (int)client_socket);
                    continue;
                }

                if (this->epoll_events[i].events != EPOLLIN){
                    // Socket closed, hang-up, socket error
                    client->close();
                    continue;
                }
                else {
                    // Incoming recv data available
                    num_bytes = recv(client_socket, (char *)recv_buffer, buffer_size, 0);
                    if (num_bytes <= -1){
                        // Error in recv
                        int error_code;
                        int error_code_size = sizeof(error_code);
                        // Reset socket
                        getsockopt(client_socket, SOL_SOCKET, SO_ERROR, (char *)&error_code, &error_code_size);
                        printf("[%s] Error when receiving data. Socket error code %d, WSAGetLastError = %d, client socket: %d\n", this->serverName, error_code, (int)WSAGetLastError(), (int)client_socket);
                        client->close();
                        continue;
                    }

                    if (num_bytes >= buffer_size){
                        // Packet is too big for our allocated buffer
                        printf("[%s] Error: Packet size %d is more than allocated buffer size %d in ConnectionPool::serveForever()\n", this->serverName, num_bytes, buffer_size);
                        client->close();
                        break;
                    }

                    // Update number of requests this client has received
                    client->request_count++;

                    //if (print)
                    //printf("[%s] Received %d/%d bytes for client %d\n", this->serverName, packetLength+buffer_offset, num_bytes, (int)client->client_id);

                    Packet *p = nullptr;
                    try{
                        // -4 to skip packet header
                        p = new Packet((char *)recv_buffer, num_bytes);
                        // TODO: remove this try catch
                    } catch (...){
                        printf("[%s] Error allocating packet with size: %d\n", this->serverName, num_bytes);
                        client->close();
                        break;
                    }
                    // Handle packet request
                    try{
                        handle_function(client, p);
                    } catch (...){
                        printf("[%s] Could not handle packet, closed connection with %d\n", this->serverName, (int)client->client_id);
                        client->close();
                        delete p;
                        break;
                    }

                    delete p;
                }
            }
            
        }
        else if (eventCount <= -1){
            // Error occurred
            printf("[%s] Error occurred in epoll_wait()\n", this->serverName);
        }
        // Update any events
        this->update();
    }
    delete[] recv_buffer;
}


Client *ConnectionPool::getClientFromSocket(SOCKET s){
    for (Client *c : this->clients){
        if (c->client_socket == s) return c;
    }
    return nullptr;
}













// end