/*
 *Proxy socket programming
 * */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#define PORT "3490"  // the port users will be connecting to

#define BACKLOG 10   // how many pending connections queue will hold
#define MAX_SIZE 1000000
#define DELIMITER "\r\n\r\n"
void sigchld_handler(int s)
{
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    while(waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


bool isHTTPS(char url[10240], char method[2000]) {
    
    if (strstr(method, "CONNECT") != NULL || strstr(method, "connect") != NULL) return true;

    if (strstr(url, "https://") != NULL || strstr(url, "HTTPS://") != NULL) {
        return true;
    }

    return false;
}

int sendall(int s, char *buf, int *len)
{
    int total = 0;        // how many bytes we've sent
    int bytesleft = *len; // how many we have left to send
    int n;

    while(total < *len) {
        n = send(s, buf+total, bytesleft, 0);
        if (n == -1) { break; }
        total += n;
        bytesleft -= n;
    }

    *len = total; // return number actually sent here

    return n==-1?-1:0; // return -1 on failure, 0 on success
}

int main()
{
    int sockfd, client_fd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes=1;
    char s[INET6_ADDRSTRLEN];
    int rv;
    //int MAX_SIZE = 10000;
    char buffer[MAX_SIZE];
    //char buffer_send[MAX_SIZE];
    int numbytes;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    
    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    struct addrinfo hints_client;
    memset(&hints_client, 0, sizeof hints_client);
    hints_client.ai_family = AF_UNSPEC;
    hints_client.ai_socktype = SOCK_STREAM;
    char server_url[100];
    printf("proxy: probando conexiones...\n");

    while(1) {  // main accept() loop
        sin_size = sizeof their_addr;
        client_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (client_fd == -1) {
            perror("accept");
            printf("error en el accept\n");
            continue;
        }

        inet_ntop(their_addr.ss_family,
            get_in_addr((struct sockaddr *)&their_addr),
            s, sizeof s);
        printf("proxy: got connection from %s\n", s);
        
        if (!fork()) { // this is the child process
            close(sockfd); // child doesn't need the listener
            // Se deberia enviar la informacion del client al server
            
            //if (send(new_fd, "Hello, world!", 13, 0) == -1)
              //  perror("send");
            numbytes = recv(client_fd, buffer, MAX_SIZE-1, 0);
            if (numbytes < 0) {
                perror("recv");
                close(client_fd);
                exit(0);
            }
            buffer[numbytes] = '\0';
            printf("Received data: %s\n", buffer);
            char *host_start = strstr(buffer, "Host: ");
            if (host_start == NULL) {
            // Handle error: Host header not found
                host_start = strstr(buffer, "host: ");
                if (host_start == NULL) {
                    perror("reading host");
                    close(client_fd);
                    continue;
                }
            }
            char *host_end = strstr(host_start, "\r\n");
            if (host_end == NULL) {
            // Handle error: Malformed request
                perror("Reading host");
                close(client_fd);
                continue;
            }
            size_t host_len = host_end - host_start - strlen("Host: ");
            char host[host_len + 1];
            strncpy(host, host_start + strlen("Host: "), host_len);
            host[host_len] = '\0';
            
            printf("Destination server: %s\n", host);
            char method[2000];
            char url[10240];
            char optional[10000];
            sscanf(buffer, "%s %s %s", method, url, optional);
            printf("method %s url %s\n", method, url);
            bool is_https = isHTTPS(url, method);
            char *port;
            char url_server[200];
            char *colon = strstr(host, ":");
            //*color = '\0';// null at the end
            if (colon == NULL) {
                if (is_https) port = "443";
                else port = "80";
            }
            else {
                *colon = '\0';
                port = (colon + 1);
            }
            strncpy(url_server, host, colon - host);
            int server_fd;
            //printf(" %s and port %d\n", url_server, port);
            if ((rv = getaddrinfo(url_server, port, &hints_client, &servinfo)) != 0) {
                fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
                close(client_fd);
                continue;
            }

            for(p = servinfo; p != NULL; p = p->ai_next) {
                if ((server_fd = socket(p->ai_family, p->ai_socktype,
                    p->ai_protocol)) == -1) {
                    perror("client: socket");
                    continue;
                }

                if (connect(server_fd, p->ai_addr, p->ai_addrlen) == -1) {
                    close(server_fd);
                    perror("client: connect");
                    continue;
                }

                break;
            }

            if (p == NULL) {
                fprintf(stderr, "client: failed to connect\n");
                close(server_fd);
                close(client_fd);
                continue;
            }


            inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
                                    server_url, sizeof server_url);
            
            printf("client: connecting to %s\n", server_url);

            freeaddrinfo(servinfo); // all done with this structure
            // send data from client to the server.
            int len2 = numbytes+1;
             if (sendall(server_fd, buffer, &len2) == -1) {
                printf("no se pudo enviar la informacion\n");
                close(client_fd);
                close(server_fd);
                continue;
             }
            numbytes = recv(server_fd, buffer, MAX_SIZE-1, 0);
            if (numbytes < 0) {
                perror("recv");
                close(client_fd);
                close(server_fd);
                exit(0);
            }
            buffer[numbytes] = '\0';
            printf("Received data from server: %s\n", buffer);
            // If there is more than one message in the response from the server.
            if (numbytes > 0)
            {
                char *msg1_end = strstr(buffer, DELIMITER);
                if (msg1_end != NULL) {
                    size_t msg1_len = msg1_end - buffer;
                    char msg1[MAX_SIZE];
                    memcpy(msg1, buffer, msg1_len);
                    msg1[msg1_len] = '\0';
                    printf("Received message 1: %s\n", msg1);
                }
            }
            // Send again to the client
            len2= numbytes+1;
            if (sendall(client_fd, buffer, &len2) == -1) {
                printf("no se pudo enviar la informacion\n");
                close(client_fd);
                close(server_fd);
                continue;
             }
            printf("Enviando informacion del server al client\n");
            close(server_fd);
            close(client_fd);
            exit(0);
        }
        close(client_fd);  // parent doesn't need this
    }

    return 0;
}
