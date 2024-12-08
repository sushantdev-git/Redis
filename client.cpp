
#include <sys/socket.h> // Core socket functions like socket(), bind(), listen(), connect()
#include <netinet/in.h> // Structures for internet addresses (e.g., sockaddr_in)
#include <arpa/inet.h>  // Functions for converting IP addresses (e.g., inet_pton(), inet_ntoa())
#include <unistd.h>     // For close() function
#include <cstring>      // For functions like memset() (optional but useful)
#include <iostream>
#include <cstdlib> // For exit()
#include "helper.cpp"

static int32_t sendRequest(int fd, const char *requestBody)
{
    uint32_t messageLength = (uint32_t)strlen(requestBody);

    if (messageLength > k_max_msg)
    {
        return -1;
    }

    char wbuf[4 + k_max_msg];

    memcpy(wbuf, &messageLength, 4); // assume little endian
    memcpy(&wbuf[4], requestBody, messageLength);

    return write_all(fd, wbuf, 4 + messageLength);
}

static int32_t readResponse(int fd)
{

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

    uint32_t messageLength = 0;
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
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1

    int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));

    if (rv)
    {
        die("connect");
    }

    const char *query_list[3] = {"hello1", "hello2", "hello3"};
    for (size_t i = 0; i < 3; ++i)
    {
        int32_t err = sendRequest(fd, query_list[i]);
        if (err)
        {
            goto L_DONE;
        }
    }
    for (size_t i = 0; i < 3; ++i)
    {
        int32_t err = readResponse(fd);
        if (err)
        {
            goto L_DONE;
        }
    }

    goto L_DONE;

L_DONE:
    close(fd);
    return 0;
}