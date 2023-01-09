# c++ tcp server.

Work in progress.
Currently only compatible on windows.

Server to efficiently accept new tcp connections and handle communication between server and client. Designed with focus on scalability and performance.

 This server also uses lock free structures to prevent any blocking on the acceptor and communication threads.


 This server utilizes wepoll (from: https://github.com/piscisaureus/wepoll) which is an efficient API for receiving socket state notifications.

 As well as (1 producer 1 consumer) lock free queue (from: https://github.com/cameron314/readerwriterqueue)
