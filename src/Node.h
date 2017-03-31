#ifndef NODE_UWS_H
#define NODE_UWS_H

#include "Socket.h"
#include <vector>
#include <mutex>

namespace uS {

enum ListenOptions : int {
    REUSE_PORT = 1,
    ONLY_IPV4 = 2
};

/**
 *
 * 
 *
 */

class WIN32_EXPORT Node {
protected:
    Loop *loop;
    NodeData *nodeData;
    std::mutex asyncMutex;

public:
    Node(int recvLength = 1024, int prePadding = 0, int postPadding = 0, bool useDefaultLoop = false);
    ~Node();
    void run();

    Loop *getLoop() {
        return loop;
    }

    template <void C(Socket p, bool error)>
    static void connect_cb(Poll *p, int status, int events) {
        C(p, status < 0);
    }

    template <void C(Socket p, bool error)>
    uS::Socket connect(const char *hostname, int port, bool secure, uS::SocketData *socketData) {
        Poll *p = new Poll;
        p->setData(socketData);

        addrinfo hints, *result;
        memset(&hints, 0, sizeof(addrinfo));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(hostname, std::to_string(port).c_str(), &hints, &result) != 0) {
            C(p, true);
            delete p;
            return nullptr;
        }

        uv_os_sock_t fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (fd == -1) {
            C(p, true);
            delete p;
            return nullptr;
        }

#ifdef __APPLE__
        int noSigpipe = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &noSigpipe, sizeof(int));
#endif

        ::connect(fd, result->ai_addr, result->ai_addrlen);
        freeaddrinfo(result);

        NodeData *nodeData = socketData->nodeData;
        if (secure) {
            socketData->ssl = SSL_new(nodeData->clientContext);
            SSL_set_fd(socketData->ssl, fd);
            SSL_set_connect_state(socketData->ssl);
            SSL_set_mode(socketData->ssl, SSL_MODE_RELEASE_BUFFERS);
            SSL_set_tlsext_host_name(socketData->ssl, hostname);
        } else {
            socketData->ssl = nullptr;
        }

        socketData->poll = UV_READABLE;
        p->init(loop, fd);
        p->setCb(connect_cb<C>);
        p->start(UV_WRITABLE);
        return p;
    }

    template <void A(Socket s)>
    static void accept_poll_cb(Poll *p, int status, int events) { // su: status == -1 if operation aborted
        ListenData *listenData = (ListenData *) p->getData();
        accept_cb<A, false>(listenData); // su: 
    }

    template <void A(Socket s)>
    static void accept_timer_cb(Timer *p) {
        ListenData *listenData = (ListenData *) p->getData();
        accept_cb<A, true>(listenData);
    }

    template <void A(Socket s), bool TIMER>
    static void accept_cb(ListenData *listenData) {
        uv_os_sock_t serverFd = listenData->sock;
        uv_os_sock_t clientFd = accept(serverFd, nullptr, nullptr);
        if (clientFd == INVALID_SOCKET /* -1 */) {
            /*
            * If accept is failing, the pending connection won't be removed and the
            * polling will cause the server to spin, using 100% cpu. Switch to a timer
            * event instead to avoid this.
            */
            if (!TIMER && errno != EAGAIN && errno != EWOULDBLOCK) {
                listenData->listenPoll->stop();
                listenData->listenPoll->close();
                listenData->listenPoll = nullptr;

                listenData->listenTimer = new Timer(listenData->nodeData->loop);
                listenData->listenTimer->setData(listenData);
                listenData->listenTimer->start(accept_timer_cb<A>, 1000, 1000);
            }
            return;
        } else if (TIMER) {
            listenData->listenTimer->stop();
            listenData->listenTimer->close();
            listenData->listenTimer = nullptr;
            listenData->listenPoll = new Poll(listenData->nodeData->loop, serverFd);
            listenData->listenPoll->setData(listenData);
            listenData->listenPoll->setCb(accept_poll_cb<A>);
            listenData->listenPoll->start(UV_READABLE);
        }

        // su: tryto listen for more incoming sockets connections
        do {
    #ifdef __APPLE__
        int noSigpipe = 1;
        setsockopt(clientFd, SOL_SOCKET, SO_NOSIGPIPE, &noSigpipe, sizeof(int));
    #endif

        SSL *ssl = nullptr;
        if (listenData->sslContext) {
            ssl = SSL_new(listenData->sslContext.getNativeContext());
            SSL_set_fd(ssl, clientFd);
            SSL_set_accept_state(ssl);
            SSL_set_mode(ssl, SSL_MODE_RELEASE_BUFFERS);
        }

        // su: new SocketData fot client
        SocketData *socketData = new SocketData(listenData->nodeData);
        socketData->ssl = ssl;

        // create a client socket Poll
        Poll *clientPoll = new Poll(listenData->listenPoll->getLoop(), clientFd);
        clientPoll->setData(socketData); // su: set Client socket data

        socketData->poll = UV_READABLE;
        A(clientPoll); // su: call onServerAccept()
        } while ((clientFd = accept(serverFd, nullptr, nullptr)) != INVALID_SOCKET);
    }

    // ***
    // todo: hostname, backlog
    // su: create, bind, listen, accept
    template <void A(Socket s)>
    bool listen(const char *host, int port, uS::TLS::Context sslContext, int options, uS::NodeData *nodeData, void *user) {
        addrinfo hints, *result;
        memset(&hints, 0, sizeof(addrinfo));

        // su: http://man7.org/linux/man-pages/man3/getaddrinfo.3.html
        hints.ai_flags = AI_PASSIVE;
        hints.ai_family = AF_UNSPEC; // IPv4/IPv6 socket address
        hints.ai_socktype = SOCK_STREAM; //for TCP use SOCK_STREAM

        if (getaddrinfo(host, std::to_string(port).c_str(), &hints, &result)) {
            return true;
        }

        uv_os_sock_t listenFd = SOCKET_ERROR /* -1 */;
        addrinfo *listenAddr;
        if ((options & uS::ONLY_IPV4) == 0) { // not ONLY_IPV4
            for (addrinfo *a = result; a && listenFd == SOCKET_ERROR; a = a->ai_next) { // until gets valid listenFd (!= SICKET_ERROR)
                if (a->ai_family == AF_INET6) { // IPv6
                    listenFd = socket(a->ai_family, a->ai_socktype, a->ai_protocol); // < 0 for Error
                    listenAddr = a;
                }
            }
        }

        for (addrinfo *a = result; a && listenFd == SOCKET_ERROR; a = a->ai_next) {
            if (a->ai_family == AF_INET) { // IPv4
                listenFd = socket(a->ai_family, a->ai_socktype, a->ai_protocol); // < 0 for Error
                listenAddr = a;
            }
        }

        if (listenFd == SOCKET_ERROR) {
            freeaddrinfo(result); // free the structure
            return true;
        }

#ifdef __linux
#ifdef SO_REUSEPORT
        if (options & REUSE_PORT) {
            int optval = 1;
            setsockopt(listenFd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
        }
#endif
#endif

        int enabled = true;
        setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));

        if (bind(listenFd, listenAddr->ai_addr, listenAddr->ai_addrlen) || ::listen(listenFd, 512)) { // if failed (< 0)
            ::close(listenFd);
            freeaddrinfo(result);
            return true;
        }

        // su: the creation of the 
        ListenData *listenData = new ListenData(nodeData /*Group*/);
        listenData->sslContext = sslContext;
        listenData->nodeData = nodeData; /*Group*/

        // the creation of the persistance listening socket | :-)
        Poll *listenPoll = new Poll(loop, listenFd); // group
        listenPoll->setData(listenData);
        listenPoll->setCb(accept_poll_cb<A>); // su: A == onServerAccept
        listenPoll->start(UV_READABLE);

        listenData->listenPoll = listenPoll;
        listenData->sock = listenFd;
        listenData->ssl = nullptr;

        // should be vector of listen data! one group can have many listeners!
        nodeData->user = listenData;
        freeaddrinfo(result);
        // su: if all is well return false: no error
        return false;
    }
};

}

#endif // NODE_UWS_H
