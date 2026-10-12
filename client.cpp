#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <string>
#include <vector>

static void die(const char *msg)
{
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

static void msg(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
}

const size_t k_max_msg = 4096;

static int32_t read_full(int fd, uint8_t *buf, size_t n)
{
    while(n > 0)
    {
        ssize_t rv = read(fd, buf, n);
        if(rv <= 0)
        {
            return -1; // error or unexpected EOF
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t write_all(int fd, const uint8_t *buf, size_t n)
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

// +-------------++------+-----+------+-----+------+-----+-----+------+
// | totallength || nstr | len | str1 | len | str2 | ... | len | strn |
// +-------------++------+-----+------+-----+------+-----+-----+------+
static int32_t send_req(int fd, const std::vector <std::string> &cmd)
{
    uint32_t len = 4; // initial is the length of nstr
    for(const std::string &s : cmd)len += 4 + s.size(); // 4 for length of that string + the size of that string.
    
    if(len > k_max_msg) return -1;

    uint8_t wbuf[k_max_msg + 4];
    // start with the total length of everything
    memcpy(&wbuf[0], &len, 4); // assume little endian
    // after the total length is the nstr
    uint32_t n = cmd.size();
    memcpy(&wbuf[4], &n, 4);
    size_t pos = 8;
    for(const std::string &s : cmd)
    {
        uint32_t p = (uint32_t)s.size();
        // after is the length of this string
        memcpy(&wbuf[pos], &p, 4);
        pos += 4;
        // last is the string itself
        memcpy(&wbuf[pos], s.data(), s.size());
        pos += s.size();
    }

    return write_all(fd, wbuf, len + 4);
}

static int32_t read_res(int fd)
{
    // 4 bytes header 
    std::vector <uint8_t> rbuf;
    rbuf.resize(4);
    errno = 0;
    int32_t err = read_full(fd, &rbuf[0], 4);
    if(err)
    {
        if(errno == 0) msg("EOF");
        else msg("read() error");
        return err;
    }

    uint32_t len = 0;
    memcpy(&len, rbuf.data(), 4); //assume little endian
    if(len > k_max_msg)
    {
        msg("too long");
        return -1;
    }

    // 4 bytes for status
    rbuf.resize(8);
    err = read_full(fd, &rbuf[4], 4);
    if(err)
    {
        msg("read() error");
        return err;
    }
    uint32_t status = 0;
    memcpy(&status, &rbuf[4], 4); // assume little endian

    // reply body
    rbuf.resize(len + 4);
    err = read_full(fd, &rbuf[8], len - 4);
    if(err)
    {
        msg("read() error");
        return err;
    }

    // do something
    printf("len:%u\n status:%u\n  data:%.*s\n", len, status, len < 100 ? len : 100, &rbuf[8]);
    return 0;
}

int main(int argc, char **argv)
{
    // In this file descriptor table of this client, fd is the index on the table
    // where it stores the socket connect with the server (each client has its own file descriptor table)
    // And the file descriptor tables is managed by OS kernel
    int fd = socket(AF_INET, SOCK_STREAM, 0); 
    if(fd < 0)
    {
        die("socket()");
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK); //127.0.0.1
    int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if(rv)
    {
        die("connect");
    }
    
    // multiple pipelined requests
    std::vector <std::string> cmd;
    for(int i=1; i<argc; i++) cmd.push_back(argv[i]);

    int32_t err = send_req(fd, cmd);
    if(err) goto L_DONE;

    err = read_res(fd);
    if(err) goto L_DONE;

L_DONE: 
    close(fd);
    return 0;
}