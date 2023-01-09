#ifndef _CONNECTION_POOL_H
#define _CONNECTION_POOL_H

#include <mutex>
#include <vector>
#include "imports/wepoll/wepoll.h"
//#include "packet.h"
#include <atomic>
#include "imports/lockfreequeue/readerwriterqueue.h"
#include <string>

class Client;
class Packet;

using functionPtr_t = void(*)(Client *, Packet *);


class ConnectionPool{

public:
	ConnectionPool(int id, const char *serverName);
	~ConnectionPool();
	void addNewConnection(Client *client);
	virtual int closeConnection(Client *c);
	virtual void checkNewConnections();
	virtual void update();
	void serveForever();
	void removeFromList(Client *c);
	void addToList(Client *c);
	int shutdown();

	int id = 0;

	// Number of connected clients on this thread
	std::atomic<int> size = 0;

	// Queue of new connections inserted by TcpConnectionAcceptor
	// Thread safe lock free queue
	// Only thread safe with 2 threads (1 enqueue 1 dequeue concurrently)
	moodycamel::ReaderWriterQueue<Client *> *newConnectionsQueue;

	int running = 1;
protected:
	Client *getClientFromSocket(SOCKET s);
	std::vector<Client *> clients;
	HANDLE epoll_handle = nullptr;
	static const int num_epoll_events = 20; // Config::maxConcurrentRequests
	struct epoll_event event, epoll_events[num_epoll_events];
	const char *serverName;
};







#endif
