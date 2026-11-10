// ======================= TIME-TRAVEL DEBUGGER - LOAD BALANCER TEMPLATE =======================

// What load balancer does:
//   1. Many clients can connect at the same time. Each one sends one string (the source code). 
//   2. Clients wait in a queue (first come, first served).
//   3. For each client, in order: make sure the server is running (start it with system() if not),
//      pass the string to the server, pass the server's reply back to the client.
//
// It does not read or understand the messages. It only moves bytes.
// The only messages it writes itself are errors ("server busy", "server failed to start").
//
// Two threads:
//   acceptor thread (acceptLoop) -> takes new clients and puts them in the queue. It never waits for the server.
//   main thread (dispatcherLoop) -> takes ONE client out of the queue and serves it completely, then the next.
//
// How the server is started:
//   system("./server 5001 &");
//   The "&" is required. Without it, system() waits until the server exits and the load balancer freezes.
//
//
// What the server program must do (the load balancer depends on this):
//   - it takes its port as the first argument, for example: ./server 5001
//   - it reads the string until the connection's write side is closed (recv returns 0)
//   - it sends its reply as [status: 1 byte][payload], then closes the connection 


#include <iostream>
#include <string>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include "helper.h"
using namespace std;


// ---- Constants ----
const char *SERVER_PATH = "./server";      // the program we start when the server is down
   
const int LISTEN_BACKLOG = 16;             // connections the OS holds until accept() takes them
const int MAX_PENDING_CLIENTS = 8;         // clients allowed to wait in our queue. More than this get "server busy".
const int STARTUP_MAX_ATTEMPTS = 50;       // after starting the server, how many times we try to connect
const int STARTUP_POLL_INTERVAL_MS = 100;  // wait between those tries (50 x 100 ms = 5 seconds in total)
int lbPort = 5000;
string serverHost = "127.0.0.1";
int serverPort = 5001;
const int CLIENT_TIMEOUT_SEC = 30;



// ---- Queue: here it holds client sockets (int). ----
// Implement it yourself, do not use std::queue.
template <typename T>
class Queue
{
    struct Node
    {
        T data;
        Node *next;
    };
    Node *front, *rear;
    int count;


public:
    // Implement these functions:
    Queue()
    {
        front = nullptr;
        rear = nullptr;
        count = 0;

    }
    ~Queue()
    {
        while (front != nullptr)
        {
            Node *temp = front;
            front = front->next;
            delete temp;
        }


    }
    void enqueue(const T &val)
    {
         Node *n = new Node;
        n->data = val;
        n->next = nullptr;

        if (rear == nullptr)
        {
            front = n;
            rear = n;
        }
        else
        {
            rear->next = n;
            rear = n;
        }
        count++;


    }
    T dequeue()
    {

        if (front == nullptr)
        {
            return T();
        }

        Node *temp = front;
        T val = temp->data;
        front = front->next;
        if (front == nullptr)
        {
            rear = nullptr;
        }
        delete temp;
        count--;
        return val;
    }
    bool isEmpty()
    {
        return count ==0;
        
    }
    int size()
    {

        return count;

    }
};

struct ClientInfo
{
    int sock;           // the connected socket
    string address;     // the client's IP as text, e.g. "127.0.0.1"
    int port;           // the client's port
    time_t arrivedAt;   // when accept() returned
};

// The queue is used by two threads, so every use of it must be inside queueMutex.
Queue<ClientInfo> pendingClients;
mutex queueMutex;
volatile sig_atomic_t flag = 1;



bool sendError(int sock, const string &message)
{
    Crc32 crc;
    uint8_t status = STATUS_ERROR;
    crc.update(&status, 1);
    crc.update(message.data(), message.size());



    if (!sendAll(sock, (const char *)&status, 1))
    {
        return false;
    }
    if (!sendAll(sock, message.data(), message.size()))
    {
        return false;
    }



    char trailer[4];
    crcToBytes(crc.value(), trailer);
    return sendAll(sock, trailer, 4);



    // Reply format: [status: 1 byte][payload until close][checksum: 4 bytes].
    // For an error: send one byte with value 1, then the message text. 
}
int createListenSocket(int port)
{ 


    int sock = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (sock < 0)
    {
        perror("socket");
        exit(1);
    }



    int yes = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
    {
        perror("setsockopt");
        close(sock);
        exit(1);
    }


    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);


    if (bind(sock, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        close(sock);
        exit(1);
    }



    if (listen(sock, LISTEN_BACKLOG) < 0)
    {
        perror("listen");
        close(sock);
        exit(1);
    }

    return sock;









    // Create a TCP socket with SOCK_CLOEXEC(flag).
    // set SO_REUSEADDR so we can restart the load balancer right away.
    // bind() to the port on any address, then listen(LISTEN_BACKLOG).
    // If bind or listen fails, print the error and exit, because without this socket we cannot work.
}
int connectToServer(const char *host, int port)
{

     int sock = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (sock < 0)
    {
        return -1;
    }

    
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;

    addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1)
    {
        close(sock);
        return -1;
    }

    
    if (connect(sock, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(sock);
        return -1;
    }

    return sock;


    // Create a TCP socket (SOCK_CLOEXEC) and connect it to host:port.
    // Return the socket, or -1 if the connection fails 
}

// =============================================================================================
// PART 1: WAKING UP THE SERVER IF IT IS DOWN
// =============================================================================================


bool startServer(int port)
{
    string cmd = string(SERVER_PATH) + " " + to_string(port) + " &";
    int rc = system(cmd.c_str());
    return rc == 0;


    // Build the command text: SERVER_PATH + " " + port as text + "&"
    //   Example: "./server 5001 &"
    // Call system() with it.
    // Return true if system() returned 0, otherwise false.
}
int connectToServerWithWakeup()
{

    int sock = connectToServer(serverHost.c_str(), serverPort);
    if (sock >= 0)
    {
        return sock;
    }

    if (!startServer(serverPort))
    {
        return -1;
    }


    for (int attempt = 0; attempt < STARTUP_MAX_ATTEMPTS; attempt++)
    {
        usleep(STARTUP_POLL_INTERVAL_MS * 1000);

        sock = connectToServer(serverHost.c_str(), serverPort);
        if (sock >= 0)
        {
            return sock;
        }
    }

  
    return -1;
    // 1. Try connectToServer(). If it works, return that socket. The server is alive.
    // 2. Otherwise call startServer(). If it fails, return -1.
    // 3. Try connectToServer() up to STARTUP_MAX_ATTEMPTS times 
    // 4. Return the first working socket, or -1 if the server never came up.
}


// =============================================================================================
// PART 2: PASSING THE STRING AND THE REPLY (one client, called by the main thread only)
// =============================================================================================


void relayOneClient(ClientInfo &c)
{

    int clientSock = c.sock;


    int serverSock = connectToServerWithWakeup();
    if (serverSock < 0)
    {
        sendError(clientSock, "backend server failed to start");
        close(clientSock);
        return;
    }


    TrailerChecker upCheck;
    string safe;
    char buf[IO_BUFFER_SIZE];
    bool netOk = true;

    while (true)
    {
        ssize_t n = recv(clientSock, buf, sizeof(buf), 0);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            netOk = false;
            break;
        }
        if (n == 0)
        {
            break;
        }

        safe.clear();
        upCheck.feed(buf, (size_t)n, safe);
        if (!safe.empty() && !sendAll(serverSock, safe.data(), safe.size()))
        {
            netOk = false;
            break;
        }
    }

    if (!netOk)
    {
        close(serverSock);
        close(clientSock);
        return;
    }

    if (!upCheck.verify())
    {
        close(serverSock);
        sendError(clientSock, "upload checksum mismatch, request corrupted");
        close(clientSock);
        return;
    }

    if (!sendAll(serverSock, upCheck.heldTrailer(), TRAILER_SIZE))
    {
        close(serverSock);
        close(clientSock);
        return;
    }
    shutdown(serverSock, SHUT_WR);

  
    TrailerChecker downCheck;
    size_t totalFromServer = 0;
    bool clientAlive = true;
    netOk = true;

    while (true)
    {
        ssize_t n = recv(serverSock, buf, sizeof(buf), 0);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            netOk = false;
            break;
        }
        if (n == 0)
        {
            break;
        }

        totalFromServer += (size_t)n;
        safe.clear();
        downCheck.feed(buf, (size_t)n, safe);
        if (!safe.empty() && !sendAll(clientSock, safe.data(), safe.size()))
        {
            clientAlive = false;
            break;
        }
    }

    if (netOk && clientAlive)
    {
        if (totalFromServer == 0)
        {
            sendError(clientSock, "server closed the connection without a reply");
        }
        else if (downCheck.verify())
        {
            sendAll(clientSock, downCheck.heldTrailer(), TRAILER_SIZE);
        }

    }

   
    close(serverSock);
    close(clientSock);

    // 1. serverSock = connectToServerWithWakeup().
    //    If it is -1: sendError "backend server failed to start", close clientSock, return.
    // 3. Client -> server: repeat recv() from clientSock into a buffer of IO_BUFFER_SIZE and sendAll() it to serverSock.
    //    Stop when recv() returns 0 (the client finished sending) or fails.
    // 4. Call shutdown(serverSock, SHUT_WR), so the server knows the string is complete.
    // 5. Server -> client: repeat recv() from serverSock and sendAll() to clientSock.
    //    Stop when recv() returns 0 (the server sent its whole reply and closed) or fails.
    // 6. Close both sockets.
}


// =============================================================================================
// PART 3: THE TWO THREADS
// =============================================================================================


void acceptLoop(int listenSock)
{
    while (flag)
    {
        int clientSock = accept(listenSock, nullptr, nullptr);
        if (clientSock < 0)
        {
            continue;
        }

        ClientInfo info;
        info.sock = clientSock;
        info.address = "";
        info.port = 0;
        info.arrivedAt = time(nullptr);

        queueMutex.lock();
        bool full = (pendingClients.size() >= MAX_PENDING_CLIENTS);
        if (!full)
        {
            pendingClients.enqueue(info);
        }
        queueMutex.unlock();

        if (full)
        {
            sendError(clientSock, "server busy, try again later");
            close(clientSock);
        }
    }









    // Repeat: accept() a new client. 
    //  If pendingClients.size() is already MAX_PENDING_CLIENTS: unlock, sendError "server busy, try again later",
    // else
    // Lock queueMutex.
    //   close the client, and go on.
    //   Otherwise enqueue the client socket.
    // Never wait for the server here, so new clients are always accepted quickly.
    // If accept() fails, print the error and keep going (do not end the thread).
}
void dispatcherLoop()
{

    while (flag)
   {
        bool got = false;
        ClientInfo c;

        queueMutex.lock();
        if (!pendingClients.isEmpty())
        {
            c = pendingClients.dequeue();
            got = true;
        }
        queueMutex.unlock();

    
        if (!got)
        {
            usleep(50000);
            continue;
        }


        if (time(nullptr) - c.arrivedAt > CLIENT_TIMEOUT_SEC)
        {
            close(c.sock);
            continue;
        }

      
        relayOneClient(c);
    }


    queueMutex.lock();
    while (!pendingClients.isEmpty())
    {
        ClientInfo left = pendingClients.dequeue();
        close(left.sock);
    }
    queueMutex.unlock();



    // This runs in the main thread and never ends.
    // Repeat:
    //   Lock queueMutex, take ONE client out of the queue if there is one, unlock.
    //   If we got a client: call relayOneClient(). The next client is taken out of the queue only after this one is finished.
}

void handler(int sg) {
    (void)sg;
    flag = 0;
}
int main(int argc, char *argv[])
{



    if (argc != 3)
    {
        cerr << "usage: " << argv[0] << " <server_ip> <server_port>" << endl;
        return 1;
    }
    serverHost = argv[1];

    char *end = nullptr;
    long p = strtol(argv[2], &end, 10);
    if (*argv[2] == '\0' || *end != '\0' || p < 1 || p > 65535)
    {
        cerr << "invalid port: " << argv[2] << endl;
        return 1;
    }
    serverPort = (int)p;

    signal(SIGINT, handler);
    signal(SIGPIPE, SIG_IGN);

  
    int listenSock = createListenSocket(lbPort);
    cout << "load balancer listening on port " << lbPort
         << ", server at " << serverHost << ":" << serverPort << endl;


    thread acceptor(acceptLoop, listenSock);
    acceptor.detach();

  
    dispatcherLoop();


    close(listenSock);
    return 0;

    // 2. listenSock = createListenSocket(LB_PORT).
    // 3. Start a thread that runs acceptLoop(listenSock) and detach it.
    // 4. Call dispatcherLoop().

}