
#include <sys/socket.h> // Core socket functions like socket(), bind(), listen(), connect()
#include <netinet/in.h> // Structures for internet addresses (e.g., sockaddr_in)
#include <arpa/inet.h>  // Functions for converting IP addresses (e.g., inet_pton(), inet_ntoa())
#include <unistd.h>     // For close() function
#include <cstring>      // For functions like memset() (optional but useful)
#include <iostream>
#include <cstdlib> // For exit()
#include <poll.h>
#include "helper.c++"

static int32_t one_request(int connfd)
{
    // 4 bytes header
    char rbuf[4 + k_max_msg + 1];
    errno = 0;

    // read the length of the message, by reading first 4 bytes.
    int32_t err = read_full(connfd, rbuf, 4);

    if (err)
    {
        if (errno == 0)
        {
            msg("EOF");
        }
        else
        {
            msg("read() error");
        }
        return err;
    }

    uint32_t messageLength = 0;
    memcpy(&messageLength, rbuf, 4); // assume little endian
    if (messageLength > k_max_msg)
    {
        msg("too long");
        return -1;
    }

    // read full message/request body
    err = read_full(connfd, &rbuf[4], messageLength);

    if (err)
    {
        msg("read() error");
        return err;
    }

    // do something
    rbuf[4 + messageLength] = '\0';
    printf("client message length: %u\n", messageLength);
    printf("client says: %s\n", &rbuf[4]);

    // reply using the same protocol
    const char reply[] = "world";
    char wbuf[4 + sizeof(reply)];
    int writeMessageLength = (uint32_t)strlen(reply);

    memcpy(wbuf, &writeMessageLength, 4);
    memcpy(&wbuf[4], reply, writeMessageLength);

    return write_all(connfd, wbuf, 4 + writeMessageLength);
}

int main()
{

    int fd = socket(AF_INET, SOCK_STREAM, 0);

    int val = 1;

    // setting socket attributes
    // SOL_SOCKET - it's socket level attributes
    // SO_REUSEADDR - reuse the same address when socket is closed.
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(0);

    int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr));

    if (rv)
    {
        die("bind()");
    }

    rv = listen(fd, SOMAXCONN);
    if (rv)
    {
        die("listen()");
    }

    // a map of all client connections, keyed by fd
    std::vector<Connection *> fd2conn;

    fd_set_nb(fd);

    //file descriptors to poll
    std::vector<struct pollfd> poll_args;

    while (true)
    {
        poll_args.clear();

        struct pollfd pfd = {fd, POLLIN, 0};
        poll_args.push_back(pfd);

        for (Connection *conn : fd2conn)
        {
            if (!conn)
                continue;

            struct pollfd pfd = {};
            pfd.fd = conn->fd;
            pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
            pfd.events = pfd.events | POLLERR;
            poll_args.push_back(pfd);
        }

        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000);
        if (rv < 0)
            die("poll");

        // process active connections
        for (size_t i = 1; i < poll_args.size(); ++i)
        {
            if (poll_args[i].revents)
            {
                Connection *conn = fd2conn[poll_args[i].fd];
                connection_io(conn);
                if (conn->state == STATE_END)
                {
                    // client closed normally, or something bad happened.
                    // destroy this connection
                    fd2conn[conn->fd] = NULL;
                    (void)close(conn->fd);
                    free(conn);
                }
            }
        }

        // try to accept a new connection if the listening fd is active
        if (poll_args[0].revents)
        {
            (void)accept_new_conn(fd2conn, fd);
        }
    }

    return 0;
}