#pragma once
#include <WinSock2.h>
#include <atomic>

class ConnectionPool;

class Client{
public:
	Client(SOCKET socket, struct sockaddr *sockAddr, int client_id);
	void close();

	SOCKET client_socket;
	ConnectionPool *connection_pool;

	// Number of requests this client has received
	int request_count = 0;
	// Unique id for this client
	int client_id = 0;
	std::atomic<int> referenceCount = 0;
};

class Packet{
public:
	Packet(char *buffer, int num_bytes);

};
