#ifndef ASIO_H
#define ASIO_H

#include <boost/asio.hpp>

typedef boost::asio::ip::tcp::socket::native_type uv_os_sock_t;
static const int UV_READABLE = 1;
static const int UV_WRITABLE = 2;

// su: createLoop, destroy, run
// extends asio::io_service : similar to channel queue in golang
struct Loop : boost::asio::io_service {
    //  su: returns pointer
    static Loop *createLoop(bool defaultLoop = true) {
        return new Loop;
    }

    void destroy() {
        delete this;
    }

    void run() {
        boost::asio::io_service::run();
    }
};


// su: simple timer for bomb activation
struct Timer {
    boost::asio::deadline_timer asio_timer;
    void *data; // will contain Group<isServer> {<Group.cpp}

    Timer(Loop *loop) : asio_timer(*loop) /*asio_timer is given the io_sevice*/ {

    }

    void start(void (*cb)(Timer *), int first, int repeat) {
        // su: async manager
        asio_timer.expires_from_now(boost::posix_time::milliseconds(first));
        // su: start again after some first milliseconds
        // starts an asyncronous wait
        asio_timer.async_wait([this, cb, repeat](const boost::system::error_code &ec) {
            if (ec != boost::asio::error::operation_aborted) {
                if (repeat) {
                    start(cb, repeat, repeat);
                }
                cb(this);
            }
        });
    }

    void setData(void *data) {
        this->data = data;
    }

    void *getData() {
        return data;
    }

    void stop() {
        asio_timer.cancel();
    }

    void close() {
        asio_timer.get_io_service().post([this]() {
            delete this;
        });
    }
};

// su: all the posting and task execution is run on loop
// su: used for posting jobs as callback
struct Async {
    Loop *loop;
    // su: function pointer 
    // pointername: cb
    // function takes Async type as argument
    void (*cb)(Async *);
    void *data; // will contain NodeData {>Networking.h}

    boost::asio::io_service::work asio_work;

    Async(Loop *loop) : loop(loop), asio_work(*loop) {
    }

    void start(void (*cb)(Async *)) {
        this->cb = cb;
    }

    void send() {
        loop->post([this]() {
            cb(this);
        });
    }

    void close() {
        loop->post([this]() {
            delete this;
        });
    }

    void setData(void *data) {
        this->data = data;
    }

    void *getData() {
        return data; // will be casted to NodeData
    }
};

// little advance socket wrapper
struct Poll {
    boost::asio::posix::stream_descriptor *socket;
    void *data; // will be casted to SocketData {<Networking.h}
    void (*cb)(Poll *p, int status, int events);
    Loop *loop;
    boost::asio::ip::tcp::socket::native_type fd;

    Poll(Loop *loop, uv_os_sock_t fd) {
        init(loop, fd);
    }

    void init(Loop *loop, uv_os_sock_t fd) {
        this->fd = fd;
        this->loop = loop; // basically a io_service
        // su: create a new socket
        socket = new boost::asio::posix::stream_descriptor(*loop, fd);
        socket->non_blocking(true);
    }

    Poll() {

    }

    ~Poll() {
    }

    void setData(void *data) {
        this->data = data;
    }

    bool isClosing() {
        return !socket;
    }

    boost::asio::ip::tcp::socket::native_type getFd() {
        return fd;//socket->native_handle();
    }

    void *getData() {
        return data;
    }

    void setCb(void (*cb)(Poll *p, int status, int events)) {
        this->cb = cb;
    }

    // su: start trying to accept connections
    void start(int events) {
        if (events & UV_READABLE) { // su: if event == UV_READABLE
            // su: read asyncronously from buffer
            socket->async_read_some(boost::asio::null_buffers(), [this](boost::system::error_code ec, std::size_t) {
                if (ec != boost::asio::error::operation_aborted) {
                    // register read write_some
                    start(UV_READABLE);
                    // cb(Poll *p, int status, int events)
                    cb(this, ec ? -1 : 0, UV_READABLE);
                }
            });
        }

        if (events & UV_WRITABLE) {
            socket->async_write_some(boost::asio::null_buffers(), [this](boost::system::error_code ec, std::size_t) {
                if (ec != boost::asio::error::operation_aborted) {
                    // register next write_some
                    start(UV_WRITABLE);
                    cb(this, ec ? -1 : 0, UV_WRITABLE);
                }
            });
        }
    }

    void change(int events) {
        socket->cancel();
        start(events);
    }

    void stop() {
        socket->cancel();
    }

    void close() {
        socket->release();
        socket->get_io_service().post([this]() {
            delete this;
        });
        delete socket;
        socket = nullptr;
    }

    void (*getPollCb())(Poll *, int, int) {
        return (void (*)(Poll *, int, int)) cb;
    }

    Loop *getLoop() {
        return loop;//(Loop *) &socket->get_io_service();
    }
};

#endif // ASIO_H
