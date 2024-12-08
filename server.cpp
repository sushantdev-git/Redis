
#include <sys/socket.h> // Core socket functions like socket(), bind(), listen(), connect()
#include <netinet/in.h> // Structures for internet addresses (e.g., sockaddr_in)
#include <arpa/inet.h>  // Functions for converting IP addresses (e.g., inet_pton(), inet_ntoa())
#include <unistd.h>     // For close() function
#include <cstring>      // For functions like memset() (optional but useful)
#include <iostream>
#include <cstdlib> // For exit()
#include "helper.c++"

static void do_something(int connfd)
{
    char rbuf[64] = {};
    ssize_t n = read(connfd, rbuf, sizeof(rbuf) - 1);
    if (n < 0)
    {
        msg("read() error");
        return;
    }
    printf("client says: %s\n", rbuf);
    char wbuf[] = "world";
    write(connfd, wbuf, strlen(wbuf));
}

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
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(0);
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

    while (true)
    {
        // accept
        struct sockaddr_in client_addr = {};
        socklen_t socklen = sizeof(client_addr);
        int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
        if (connfd < 0)
        {
            continue; // error
        }
        std::cout << "Address: " << inet_ntoa(client_addr.sin_addr) << std::endl;

        //read multiple request from single connection
        while (true)
        {
            int32_t err = one_request(connfd);
            if (err)
            {
                break;
            }
        }

        close(connfd);
    }

    return 0;
}