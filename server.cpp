// stdlib
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
// system
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
// C++
#include <vector>

static void msg(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
}

static void msg_errno(const char *msg) 
{
    fprintf(stderr, "[errno:%d] %s\n", errno, msg);
}

static void die(const char *msg)
{
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

static void fd_set_nb(int fd)
{
    // fcntl set the global errno variable if something goes wrong
    errno = 0;
    // file descriptor (fd) has a set of behavior flags stored as a bitmask like 000110011
    // so, we retrieve the flags first.
    int flags = fcntl(fd, F_GETFL, 0);
    if(errno)
    {
        die("fcntl error");
        return;
    }

    // turn on the NONBLOCKING bit
    flags |= O_NONBLOCK;

    errno = 0;
    // update the flags to fd
    (void)fcntl(fd, F_SETFL, flags);
    if(errno)
    {
        die("fcntl error");
    }
}

const size_t k_max_msg = 32 << 20; // large message because all messages go in one thread

struct Conn
{
    int fd = -1;
    // application's intention, for the event loop
    bool want_read = false;
    bool want_write = false;
    bool want_close = false;
    // buffered input and output
    std::vector <uint8_t> incoming; // data from the socket for the protocal parser to work on
    std::vector <uint8_t> outgoing; // generate respone that are written to the socket
};

// append to the back
static void buf_append(std::vector<uint8_t> &buf, const uint8_t *data, size_t len)
{
    buf.insert(buf.end(), data, data + len);
}

// remove from the front
static void buf_consume(std::vector<uint8_t> &buf, size_t len)
{
    buf.erase(buf.begin(), buf.begin() + len);
}

// application callback when the listening socket is ready to read or write
static Conn *handle_accept(int fd)
{
    struct sockaddr_in client_addr = {};
    socklen_t addrlen = sizeof(client_addr); // equals 16 bytes
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen); // if accept() run, it overwrites the zeros inside client_addr memory with the client's actual IP and port.
    if(connfd < 0)
    {
        msg_errno("accept() error");
        return NULL;
    }
    uint32_t ip = client_addr.sin_addr.s_addr;
    fprintf(stderr, "new client from %u.%u.%u.%u:%u\n",
        ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, ip >> 24,
        ntohs(client_addr.sin_port)
    );

    // set the new connection fd to nonblocking mode
    fd_set_nb(connfd);

    // create a `struct Conn`
    Conn *con = new Conn();
    con->fd = connfd;
    con->want_read = true;
    return con;
}

static int32_t read_full(int fd, char *buf, size_t n)
{
    while(n > 0)
    {
        ssize_t rv = read(fd, buf, n);
        if(rv <= 0)
        {
            return -1;
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t write_all(int fd, char *buf, size_t n)
{
    while(n > 0)
    {
        ssize_t rv = write(fd, buf, n);
        if(rv <= 0)
        {
            return -1;
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t oneRequest(int connfd)
{
    char rbuf[4 + k_max_msg];
    errno = 0;
    int32_t err = read_full(connfd, rbuf, 4);
    if(err)
    {
        msg(errno==0 ? "EOF" : "read() error");
        return -1;
    }
    uint32_t len = 0;
    memcpy(&len, rbuf, 4);
    if(len > k_max_msg)
    {
        msg("too long");
        return -1;
    }
    err = read_full(connfd, &rbuf[4], len);
    if(err)
    {
        msg("read() error");
        return err;
    }
    printf("client says: %.*s\n", len, &rbuf[4]);
    const char reply[] = "world";
    char wbuf[4 + sizeof(reply)];
    len = (uint32_t)strlen(reply);
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], reply, len);
    return write_all(connfd, wbuf, len + 4);
}

int main()
{
    // initiallize IPtype and protocol type
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0)
    {
        die("socket()");
    }
    
    // set socket opt
    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    // bind the server to this address
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(0);
    int rv = bind(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if(rv)
    {
        die("Bind()");
    }

    // set the listen fd to nonblocking mode
    fd_set_nb(fd);

    // listen to the address
    rv = listen(fd, SOMAXCONN);
    if(rv)
    {
        die("listen()");
    }
    
    // a map of all client connections, keyed by fd
    std::vector <Conn *> fd2conn;
    // the event loop
    std::vector <struct pollfd> poll_args;
    while(true)
    {
        // prepare step
        poll_args.clear();
        // put the listening sockets in the first position
        struct pollfd pfd = {fd, POLLIN, 0};
        poll_args.push_back(pfd);
        // the rest are connection sockets
        for(Conn *conn : fd2conn)
        {
            if(!conn)continue;
            // always poll() for error
            struct pollfd pfd = {conn->fd, POLLERR, 0};
            // poll() flags from the application's intent
            if(conn->want_read)
            {
                pfd.events |= POLLIN;
            }
            if(conn->want_write)
            {
                pfd.events |= POLLOUT;
            }
            poll_args.push_back(pfd);
        }

        // wait for readiness
        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), -1);
        if(rv < 0 && errno == EINTR) 
        {
            continue;   // not an error
        }
        if(rv < 0) 
        {
            die("poll");
        }

        if(poll_args[0].revents)
        {
            if(Conn *conn = handle_accept(fd))
            {
                // put it into the map
                if(fd2conn.size() <= (size_t)conn->fd)fd2conn.resize(conn->fd + 1);
                assert(!fd2conn[conn->fd]);
                fd2conn[conn->fd] = conn;
            }
        }
    }
    return 0;
}