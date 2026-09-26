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

const size_t k_max_msg = 32 << 20;

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
    
    while(true)
    {
        struct sockaddr_in client_addr = {};
        socklen_t addrlen = sizeof(client_addr);
        int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
        if(connfd < 0)
        {
            continue;
        }

        while(true)
        {
            int32_t err = oneRequest(connfd);
            if(err)
            {
                break;
            }
        }

        close(connfd);

    }
    return 0;
}