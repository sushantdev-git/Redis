
#include <sys/socket.h> // Core socket functions like socket(), bind(), listen(), connect()
#include <netinet/in.h> // Structures for internet addresses (e.g., sockaddr_in)
#include <arpa/inet.h>  // Functions for converting IP addresses (e.g., inet_pton(), inet_ntoa())
#include <unistd.h>     // For close() function
#include <cstring>      // For functions like memset() (optional but useful)
#include <iostream>
#include <cstdlib> // For exit()
#include <poll.h>
#include "helper.cpp"

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