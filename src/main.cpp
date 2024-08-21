#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>
#include <string>
#include <vector>


int DecodeMessage(const char *buf, const int bytesRead)
{
    // Look for the first NULL
    int separator = -1;
    for (int i = 0; i < bytesRead; ++i)
    {
        if (buf[i] == '\0')
        {
            separator = i;
            break;
        }
    }
    // Check if separator was found and if the username and message lengths are valid
    if (separator == -1 || separator > 32 || (bytesRead - separator - 1) > 240)
    {
        return 1; // Failure
    }
    // Extract username and message
    char username[33]; // 32 bytes + 1 for null terminator
    char message[241]; // 240 bytes + 1 for null terminator

    strncpy(username, buf, separator);
    username[separator] = '\0'; // Ensure null termination

    strncpy(message, buf + separator + 1, bytesRead - separator - 1);
    message[bytesRead - separator - 1] = '\0'; // Ensure null termination

    return 0; // Success
}

char *EncodeMessage(std::string username, std::string message)
{
    // Allocate the memory
    char *buf;
    // Add a extra byte for the NULL
    buf = (char *)malloc(username.length() + message.length() + 1);
    int pos = 0;
    for (char c : username)
    {
        buf[pos] = c;
        pos++;
    }
    // Place in the NULL byte
    buf[pos] = '\0';
    pos++;
    for (char c : message)
    {
        buf[pos] = c;
        pos++;
    }
    return buf;
}

int main(const int argc, char const *argv[])
{
    /* code */
    if (argc < 4)
    {
        printf("Usage: %s username host port",
               argv[0]); // arg0 is the program name
        return 1;
    }

    std::string username(argv[1]);
    if (username.length() > 32)
    {
        std::cout << "Username is too long" << std::endl;
        return 1;
    }

    struct addrinfo hints;
    struct addrinfo *address;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;      // IPv4
    hints.ai_socktype = SOCK_DGRAM; // UDP
    int s = getaddrinfo(
        argv[2], argv[3], &hints,
        &address); // arg1 is the host name and arg2 is the port number
    if (s != 0)
    {
        fprintf(stderr, "Failed to resolve address: %s\n", gai_strerror(s));
        return 1;
    }

    // Open the socket
    int socket_fd =
        socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (socket_fd == -1)
    {
        // Failed.
        perror("Failed to create socket");
        return 1;
    }

    // Allow multiple applications to use the same port (to run two versions of
    // the app side by side for testing)
    int optval = true;
    if (0 != setsockopt(socket_fd, SOL_SOCKET, SO_REUSEPORT, &optval,
                        sizeof(optval)))
    {
        perror("Failed to set SO_REUSEPORT");
        return 1;
    }

    int optbroad = true;
    if (0 != setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, &optbroad,
                        sizeof(optbroad)))
    {
        perror("Failed to set SO_BROADCAST");
        return 1;
    }

    // Bind it to the address and port
    if (0 != bind(socket_fd, address->ai_addr, address->ai_addrlen))
    {
        perror("Failed to bind");
        return 1;
    }

    // Prepare the pollfd array with the list of file handles to monitor
    struct pollfd pfds[] = {
        {
            // monitor the socket
            .fd = socket_fd,
            .events = POLLIN | POLLERR,
        },
        {
            // monitor the STDIN
            .fd = STDIN_FILENO,
            .events = POLLIN | POLLERR,
        },
    };

    static char buf[512];
    for (;;)
    {
        poll(pfds, sizeof(pfds) / sizeof(struct pollfd), -1);

        // Event Handling
        if (pfds[0].revents)
        {
            // Receive the UDP packet
            ssize_t bytesRead = read(socket_fd, buf, sizeof(buf) - 1); // With room for trailing NULL
            if (bytesRead < 0)
            {
                std::cout << "Invalid message, could not read" << std::endl;
                continue;
            }
            // Check the message length
            if (bytesRead > 32+1+240)
            {
                std::cout << "Message is too long" << std::endl;
            }
            // Make the message NULL terminated
            buf[bytesRead] = 0;
            // First is username, then message
            int res = DecodeMessage(buf, bytesRead);
            if (res != 0)
            {
                std::cout << "Could not decode message" << std::endl;
            }
        }
        if (pfds[1].revents)
        {
            // Send from STDIN
            std::string msg;
            std::getline(std::cin, msg);
            // Now encode the message
            // Testing
            std::cout << username << " is sending message " << msg << std::endl;
            char *encodedMsg = EncodeMessage(username, msg);
            // Send the message, ensure the correct message length is sent.
            s = sendto(socket_fd, encodedMsg, (username.length() + msg.length() + 1), 0, address->ai_addr,
                       address->ai_addrlen);
            free(encodedMsg);
            if (s == -1)
            {
                perror("Failed to send");
                return 1;
            }
            printf("Sent %i bytes\n", s);
        }
    }

    return 0;
}
