#include <sys/socket.h> // Core socket functions like socket(), bind(), listen(), connect()
#include <netinet/in.h> // Structures for internet addresses (e.g., sockaddr_in)
#include <arpa/inet.h>  // Functions for converting IP addresses (e.g., inet_pton(), inet_ntoa())
#include <unistd.h>     // For close() function
#include <cstring>      // For functions like memset() (optional but useful)
#include <iostream>
#include <cstdlib> // For exit()
#include <cassert>
#include <fcntl.h>

const size_t k_max_msg = 4096;

enum CONNECTION_STATE
{
    STATE_REQ = 0,
    STATE_RES = 1,
    STATE_END = 2, // mark the connection for deletion
};

struct Connection
{
    int fd = -1; // file descriptor
    CONNECTION_STATE state = STATE_REQ;

    // buffer for reading
    size_t rbuf_size = 0;
    uint8_t rbuf[4 + k_max_msg];

    // buffer for writing
    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
    uint8_t wbuf[4 + k_max_msg];
};

/**
 * read full n bytes from socket, because read syscall just returns whatever data is available in the kernel, or blocks if there is none
 */
static int32_t read_full(int fd, char *buf, size_t n)
{
    while (n > 0)
    {
        ssize_t rv = read(fd, buf, n);
        if (rv <= 0)
        {
            return -1; // error, or unexpected EOF
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

/**
 * write full n bytes into socket, because the write() syscall can return successfully with partial data written if the kernel buﬀer is full
 */
static int32_t write_all(int fd, const char *buf, size_t n)
{
    while (n > 0)
    {
        ssize_t rv = write(fd, buf, n);
        if (rv <= 0)
        {
            return -1; // error
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

void die(const std::string &message)
{
    std::cerr << message << std::endl;
    exit(EXIT_FAILURE);
}

void msg(const std::string &message)
{
    std::cout << message << std::endl;
}

/**
 * Utility method to set file descriptor to non blocking mode.
 */
static void fd_set_nb(int fd)
{
    errno = 0;
    int flags = fcntl(fd, F_GETFL, 0);

    if (errno)
    {
        die("fcntl error");
        return;
    }

    flags |= O_NONBLOCK;
    errno = 0;

    (void)fcntl(fd, F_SETFL, flags);

    if (errno)
    {
        die("fcntl error");
    }
}

/**
 * update fd2conn map with now connection indexed at conn -> fd
 */
static void conn_put(std::vector<Connection *> &fd2conn, struct Connection *conn)
{
    if (fd2conn.size() <= (size_t)conn->fd)
    {
        fd2conn.resize(conn->fd + 1);
    }
    fd2conn[conn->fd] = conn;
}


static int32_t accept_new_conn(std::vector<Connection *> &fd2conn, int fd)
{
    // accept
    struct sockaddr_in client_addr = {};
    socklen_t socklen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
    if (connfd < 0)
    {
        msg("accept() error");
        return -1; // error
    }

    // set the new connection fd to nonblocking mode
    fd_set_nb(connfd);

    // creating the struct Conn
    struct Connection *conn = (struct Connection *)malloc(sizeof(struct Connection));
    if (!conn)
    {
        close(connfd);
        return -1;
    }

    conn->fd = connfd;
    conn->state = STATE_REQ;
    conn->rbuf_size = 0;
    conn->wbuf_size = 0;
    conn->wbuf_sent = 0;
    conn_put(fd2conn, conn);

    return 0;
}

/**
 * Try to write whatever we have in write buffer.
 */
static bool try_flush_buffer(Connection *conn)
{
    ssize_t rv = 0;

    do
    {
        size_t remain = conn->wbuf_size - conn->wbuf_sent;
        rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain);
    } while (rv < 0 && errno == EINTR);

    if (rv < 0 && errno == EAGAIN)
    {
        // got EAGAIN, stop.
        return false;
    }

    if (rv < 0)
    {
        msg("write() error");
        conn->state = STATE_END;
        return false;
    }

    conn->wbuf_sent += (size_t)rv;
    assert(conn->wbuf_sent <= conn->wbuf_size);

    if (conn->wbuf_sent == conn->wbuf_size)
    {
        // response was fully sent, change state back
        conn->state = STATE_REQ;
        conn->wbuf_sent = 0;
        conn->wbuf_size = 0;
        return false;
    }

    // still got some data in wbuf, could try to write again
    return true;
}

/**
 * Try to send response 
 */
static void state_res(Connection *conn)
{
    while (try_flush_buffer(conn))
    {
    }
}

/**
 * Try to porcess on request if read buffer have enough data following the protocol.
 */
static bool try_one_request(Connection *conn)
{
    // try to parse a request from the buffer
    if (conn->rbuf_size < 4)
    {
        // not enough data in the buffer. Will retry in the next iteration
        return false;
    }

    uint32_t messageLen = 0;
    memcpy(&messageLen, &conn->rbuf[0], 4);

    if (messageLen > k_max_msg)
    {
        msg("too long");
        conn->state = STATE_END;
        return false;
    }

    if (4 + messageLen > conn->rbuf_size)
    {
        // not enough data in the buffer. Will retry in the next iteration
        return false;
    }

    // got one request, do something with it
    printf("client says: %.*s\n", messageLen, &conn->rbuf[4]);
    // generating echoing response

    memcpy(&conn->wbuf[0], &messageLen, 4);
    memcpy(&conn->wbuf[4], &conn->rbuf[4], messageLen);

    conn->wbuf_size = 4 + messageLen;

    // remove the request from the buffer.
    // note: frequent memmove is inefficient.
    // note: need better handling for production code.
    size_t remain = conn->rbuf_size - 4 - messageLen;

    if (remain)
    {
        memmove(conn->rbuf, &conn->rbuf[4 + messageLen], remain);
    }
    conn->rbuf_size = remain;

    // change state
    conn->state = STATE_RES;
    state_res(conn);

    // continue the outer loop if the request was fully processed
    return (conn->state == STATE_REQ);
}

/**
 * Try to fill read buffer of connection by reading data from socket fd
 */
static bool try_fill_buffer(Connection *conn)
{
    // try to fill the buffer
    assert(conn->rbuf_size < sizeof(conn->rbuf));

    ssize_t rv = 0;
    do
    {
        size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
        rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
    } while (rv < 0 && errno == EINTR);

    if (rv < 0 && errno == EAGAIN)
    {
        // got EAGAIN, stop.
        return false;
    }
    if (rv < 0)
    {
        msg("read() error");
        conn->state = STATE_END;
        return false;
    }
    if (rv == 0)
    {
        if (conn->rbuf_size > 0)
        {
            msg("unexpected EOF");
        }
        else
        {
            msg("EOF");
        }
        conn->state = STATE_END;
        return false;
    }
    conn->rbuf_size += (size_t)rv;
    assert(conn->rbuf_size <= sizeof(conn->rbuf) - conn->rbuf_size);

    // Try to process requests one by one.
    while (try_one_request(conn))
    {
    }
    return (conn->state == STATE_REQ);
}

/**
 * Try to process the request 
 */
static void state_req(Connection *conn)
{
    while (try_fill_buffer(conn))
    {
    }
}

/**
 * handles connection I/O Operations on basis of state
 */
static void connection_io(Connection *conn)
{
    if (conn->state == STATE_REQ)
    {
        state_req(conn);
    }
    else if (conn->state == STATE_RES)
    {
        state_res(conn);
    }
    else
    {
        assert(0); // not expected
    }
}