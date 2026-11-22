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

// Supported Data types
enum {
    TAG_NIL = 0, // nil
    TAG_ERR = 1, // error code + msg
    TAG_STR = 2, // string
    TAG_INT = 3, // int64
    TAG_DBL = 4, // double
    TAG_ARR = 5, // array
};

static int32_t print_res(const uint8_t *buf, uint32_t len)
{
    if(len < 1)
    {
        msg("length of buffer can't be less than 1");
        return -1;
    }
    if(buf[0] == TAG_NIL)
    {
        printf("(nil)\n");
        return 1;
    }
    if(buf[0] == TAG_ERR)
    {
        if(len < 1 + 8)
        {
            msg("length of buffer need to be at least 1 + 8");
            return -1;
        }
        uint32_t code = 0, size = 0;
        memcpy(&code, &buf[1], 4);
        memcpy(&size, &buf[5], 4);
        if(len < 1 + 8 + size)
        {
            msg("can't find the error message");
            return -1;
        }
        printf("(err) %d %.*s\n", code, len, &buf[9]);
        return 1 + 8 + size;
    }
    if(buf[0] == TAG_STR)
    {
        if(len < 1 + 4)
        {
            msg("can't find the size of the string in TAG_STR");
            return -1;
        }
        uint32_t size = 0;
        memcpy(&size, &buf[1], 4);
        if(len < 1 + 4 + size)
        {
            msg("something wrong with the string");
            return -1;
        }
        printf("(str) %.*s\n", size, &buf[5]);
        return 1 + 4 + size;
    }
    if(buf[0] == TAG_INT)
    {
        if(len < 1 + 8)
        {
            msg("can't find the integer");
            return -1;
        }
        int64_t val = 0;
        memcpy(&val, &buf[1], 8);
        printf("(int) %ld\n", val);
        return 1 + 8;
    }
    if(buf[0] == TAG_DBL)
    {
        if(len < 1 + 8)
        {
            msg("can't find the double");
            return -1;
        }
        double val = 0;
        memcpy(&val, &buf[1], 8);
        printf("(double) %g\n", val);
        return 1 + 8;
    }
    if(buf[0] == TAG_ARR)
    {
        if(len < 1 + 4)
        {
            msg("can't find the size of the array");
            return -1;
        }
        uint32_t size = 0;
        memcpy(&size, &buf[1], 4);
        printf("(arr) len=%u\n", size);
        size_t pos = 1 + 4;
        for(uint32_t i = 0; i < size; i++)
        {
            int32_t rv = print_res(&buf[pos], len - pos);
            if(rv < 0)
            {
                return rv;
            }

            pos += (size_t)rv;
            if(pos > len)
            {
                msg("position can't be greater than length");
                return -1;
            }
        }
        printf("(arr) end\n");
        return (int32_t)pos;
    }

    msg("something went wrong");
    return -1;
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

    // reply body
    rbuf.resize(len + 4);
    err = read_full(fd, &rbuf[4], len);
    if(err)
    {
        msg("read() error");
        return err;
    }

    // print the result out
    uint32_t rv = print_res((uint8_t *)&rbuf[4], len);
    if(rv > 0 && (uint32_t)rv != len)
    {
        msg("something went wrong");
        rv = -1;
    }

    return rv;
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