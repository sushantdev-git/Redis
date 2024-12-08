
#include <sys/socket.h> // Core socket functions like socket(), bind(), listen(), connect()
#include <netinet/in.h> // Structures for internet addresses (e.g., sockaddr_in)
#include <arpa/inet.h>  // Functions for converting IP addresses (e.g., inet_pton(), inet_ntoa())
#include <unistd.h>     // For close() function
#include <cstring>      // For functions like memset() (optional but useful)
#include <iostream>
#include <cstdlib> // For exit()
#include "helper.c++"

static int32_t query(int fd, const char *queryText)
{
    uint32_t messageLength = (uint32_t)strlen(queryText);

    if (messageLength > k_max_msg)
    {
        return -1;
    }

    char wbuf[4 + k_max_msg];

    memcpy(wbuf, &messageLength, 4); // assume little endian
    memcpy(&wbuf[4], queryText, messageLength);

    if (int32_t err = write_all(fd, wbuf, 4 + messageLength))
    {
        return err;
    }

    // 4 bytes header
    char rbuf[4 + k_max_msg + 1];
    errno = 0;

    int32_t err = read_full(fd, rbuf, 4);

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

    memcpy(&messageLength, rbuf, 4); // assume little endian

    if (messageLength > k_max_msg)
    {
        msg("too long");
        return -1;
    }

    // reply body
    err = read_full(fd, &rbuf[4], messageLength);

    if (err)
    {
        msg("read() error");
        return err;
    }

    // do something
    rbuf[4 + messageLength] = '\0';
    printf("server says: %s\n", &rbuf[4]);

    return 0;
}

int main()
{
    // socket file descriptor
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0)
    {
        die("socket()");
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK); // 127.0.0.1

    int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));

    if (rv)
    {
        die("connect");
    }

    int32_t err = query(fd, "hello1");
    if(err) goto L_DONE;

    err = query(fd, "hello2");
    if(err) goto L_DONE;

    err = query(fd, "hello3");
    if(err) goto L_DONE;

    goto L_DONE;

    L_DONE:
        close(fd);
        return 0;
}