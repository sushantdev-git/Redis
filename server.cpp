
#include <sys/socket.h> // Core socket functions like socket(), bind(), listen(), connect()
#include <netinet/in.h> // Structures for internet addresses (e.g., sockaddr_in)
#include <arpa/inet.h>  // Functions for converting IP addresses (e.g., inet_pton(), inet_ntoa())
#include <unistd.h>     // For close() function
#include <cstring>      // For functions like memset() (optional but useful)
#include <iostream>
#include <cstdlib> // For exit()


void die(const std::string& message) {
    std::cerr << message << std::endl;
    exit(EXIT_FAILURE);
}

void msg(const std::string& message) {
    std::cout << message << std::endl;
}

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

        do_something(connfd);
        close(connfd);
    }

    return 0;
}