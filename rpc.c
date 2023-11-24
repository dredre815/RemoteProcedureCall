#include "rpc.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <stdint.h>

// Task 9
#define NONBLOCKING

/* Maximum length of function name */
#define MAX_RPC_NAME_LENGTH 1024

/* Maximum number of functions that can be registered */
#define MAX_RPC_FUNCTION_COUNT 128

/* Maximum number of worker threads in the thread pool */
#define THREAD_POOL_SIZE 16

/* Maximum number of pending connection requests */
#define BACK_LOG 128

/* Timeout duration */
#define TIMEOUT 64

/* Global variable to indicate whether the server should stop */
volatile sig_atomic_t server_stop_flag = 0;

/* Message types */
#define MSG_TYPE_FUNC_EXIST_QUERY 1
#define MSG_TYPE_FUNC_CALL 2

/* Random number seed */
#define RANDOM_NUMBER_SEED 42

typedef struct rpc_function rpc_function;
typedef struct thread_pool thread_pool;
static void set_socket_reuse(int sockfd);
static rpc_handler find_rpc_handler(rpc_server *srv, const char *name);
static void *client_thread(void *arg);
static int add_client_to_pool(thread_pool *pool, int sockfd, struct sockaddr_in addr);
static void signal_handler(int sig);
static void set_socket_timeout(int sockfd, int timeout_sec);
int function_exists_on_server(rpc_client *cl, char *name);
static int send_all(int sockfd, const void *buffer, size_t length);
static int recv_all(int sockfd, void *buffer, size_t length);
static int send_int(int sockfd, int value);
static int recv_int(int sockfd, int *value);
static int send_sizet(int sockfd, size_t value);
static int recv_sizet(int sockfd, size_t *value);
uint64_t htonll(uint64_t value);
uint64_t ntohll(uint64_t value);

/* Store the name of the function and its handler */
struct rpc_function{
    char name[MAX_RPC_NAME_LENGTH];
    rpc_handler handler;
};

/* Thread pool for handling clients */
struct thread_pool{
    pthread_t threads[THREAD_POOL_SIZE];
    rpc_client *clients[THREAD_POOL_SIZE];
    int client_count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int shutdown;
};

struct rpc_server {
    int sockfd;
    struct sockaddr_in addr;
    rpc_function functions[MAX_RPC_FUNCTION_COUNT];
    int function_count;
    thread_pool pool;
};

rpc_server *rpc_init_server(int port) {
    // Allocate memory for the server
    rpc_server *srv = (rpc_server *)malloc(sizeof(rpc_server));
    if (!srv) {
        return NULL;
    }

    struct addrinfo hints, *res;
    int ret;

    // Initialise hints
    memset(&hints, 0, sizeof(hints));
    // Allow IPv6 addresses
    hints.ai_family = AF_INET6;
    // Allow stream sockets
    hints.ai_socktype = SOCK_STREAM;
    // Allow passive sockets
    hints.ai_flags = AI_PASSIVE;
    // Allow any protocol
    hints.ai_protocol = 0;

    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", port);
    // Get address information
    ret = getaddrinfo(NULL, port_str, &hints, &res);
    if (ret != 0) {
        free(srv);
        return NULL;
    }

    // Create a socket
    srv->sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (srv->sockfd < 0) {
        perror("socket");
        freeaddrinfo(res);
        free(srv);
        return NULL;
    }

    // Set socket to reuse address
    set_socket_reuse(srv->sockfd);

    // Set socket timeout
    set_socket_timeout(srv->sockfd, TIMEOUT);

    // Bind to port
    if (bind(srv->sockfd, res->ai_addr, res->ai_addrlen) < 0) {
        perror("bind");
        close(srv->sockfd);
        freeaddrinfo(res);
        free(srv);
        return NULL;
    }

    // Free memory allocated by getaddrinfo
    freeaddrinfo(res);

    // Listen for connections
    if (listen(srv->sockfd, BACK_LOG) < 0) {
        perror("listen");
        close(srv->sockfd);
        free(srv);
        return NULL;
    }

    // Initialise function count to 0
    srv->function_count = 0;

    // Initialise the thread pool
    srv->pool.client_count = 0;
    srv->pool.shutdown = 0;
    pthread_mutex_init(&srv->pool.mutex, NULL);
    pthread_cond_init(&srv->pool.cond, NULL);

    // Set the signal handler for SIGINT
    signal(SIGINT, signal_handler);

    // Create the worker threads
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        int thread_creation_status = pthread_create(&srv->pool.threads[i], NULL, client_thread, srv);
        // Check if the thread was created successfully
        if (thread_creation_status != 0) {
            close(srv->sockfd);
            pthread_mutex_destroy(&srv->pool.mutex);
            pthread_cond_destroy(&srv->pool.cond);
            free(srv);
            return NULL;
        }
    }

    return srv;
}

int rpc_register(rpc_server *srv, char *name, rpc_handler handler) {
    // Check if more functions can be registered
    if (srv->function_count >= MAX_RPC_FUNCTION_COUNT) {
        return -1;
    }

    // Copy the function name and handler into the server
    strncpy(srv->functions[srv->function_count].name, name, MAX_RPC_NAME_LENGTH - 1);
    srv->functions[srv->function_count].name[MAX_RPC_NAME_LENGTH - 1] = '\0';
    srv->functions[srv->function_count].handler = handler;

    srv->function_count++;
    return 0;
}

void rpc_serve_all(rpc_server *srv) {
    // Check if the server should stop
    while (!server_stop_flag) {
        while (1) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            // Accept a connection
            int client_fd = accept(srv->sockfd, (struct sockaddr *)&client_addr, &client_len);
            
            if (client_fd < 0) {
                if (errno == EINTR) {
                    // If the system call was interrupted, retry
                    continue;
                } else {
                    // If the system call failed, break out of the loop
                    break;
                }
            }

            // Add the client to the thread pool
            if (add_client_to_pool(&srv->pool, client_fd, client_addr) < 0) {
                close(client_fd);
                continue;
            }
        }
    }

    // If the server should stop, set the shutdown flag
    srv->pool.shutdown = 1;
    pthread_cond_broadcast(&srv->pool.cond);

    // Wait for the worker threads to exit
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        pthread_join(srv->pool.threads[i], NULL);
    }

    // Close the socket and free the memory
    close(srv->sockfd);
    pthread_mutex_destroy(&srv->pool.mutex);
    pthread_cond_destroy(&srv->pool.cond);
    free(srv);
}

struct rpc_client {
    int sockfd;
    struct sockaddr_in addr;
};

struct rpc_handle {
    char name[MAX_RPC_NAME_LENGTH];
    rpc_client *client;
};

/* Set socket to reuse address */
static void set_socket_reuse(int sockfd) {
    int enable = 1;

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
}

/* Find the handler for a given function name */
/* RETURNS: handler on success, NULL on error */
static rpc_handler find_rpc_handler(rpc_server *srv, const char *name) {
    for (int i = 0; i < srv->function_count; i++) {
        // If the function name matches, return the handler
        if (strcmp(srv->functions[i].name, name) == 0) {
            return srv->functions[i].handler;
        }
    }

    return NULL;
}

/* Main function for the worker threads*/
static void *client_thread(void *arg) {
    // Get the server state
    rpc_server *srv = (rpc_server *)arg;
    thread_pool *pool = &srv->pool;

    while (1) {
        // Lock the mutex
        pthread_mutex_lock(&pool->mutex);

        // Wait for a client to be available
        while (pool->client_count == 0 && !pool->shutdown) {
            pthread_cond_wait(&pool->cond, &pool->mutex);
        }

        // If shutdown is set, exit
        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->mutex);
            pthread_exit(NULL);
        }

        // Get the client from the pool
        rpc_client *client = pool->clients[pool->client_count - 1];
        pool->client_count--;

        // Unlock the mutex
        pthread_mutex_unlock(&pool->mutex);

        // Main loop for process the client
        while (1) {
            int message_type = 0;
            // Receive the message type code
            if (recv_int(client->sockfd, &message_type) == -1) {
                close(client->sockfd);
                break;
            }

            if (message_type == MSG_TYPE_FUNC_EXIST_QUERY) {
                // Receive the function name
                char func_name[MAX_RPC_NAME_LENGTH];
                if (recv_all(client->sockfd, func_name, MAX_RPC_NAME_LENGTH) != 0) {
                    close(client->sockfd);
                    break;
                }

                // Check if the function exists
                int exists = find_rpc_handler(srv, func_name) != NULL;

                // Send the function existence response (1 for exists, 0 for does not exist)
                if (send_int(client->sockfd, exists) == -1) {
                    close(client->sockfd);
                    break;
                }
            } else if (message_type == MSG_TYPE_FUNC_CALL) {
                // Receive the function name
                char name[MAX_RPC_NAME_LENGTH];
                if (recv_all(client->sockfd, name, MAX_RPC_NAME_LENGTH) != 0) {
                    close(client->sockfd);
                    break;
                }

                // Find the handler for the function
                rpc_handler handler = find_rpc_handler(srv, name);
                
                // Check if the handler exists
                if (!handler) {
                    close(client->sockfd);
                    break;
                }
                
                // Receive the payload
                rpc_data input;
                if (recv_int(client->sockfd, &input.data1) == -1 ||
                    recv_sizet(client->sockfd, &input.data2_len) == -1) {
                    close(client->sockfd);
                    break;
                }

                // Allocate memory for the payload data
                void *buffer = malloc(input.data2_len);
                if (buffer == NULL) {
                    close(client->sockfd);
                    break;
                }
                
                // Receive the payload data
                if (recv_all(client->sockfd, buffer, input.data2_len) != 0) {
                    free(buffer);
                    close(client->sockfd);
                    break;
                }
                input.data2 = buffer;

                // Check if the input data is valid
                if (input.data2_len > 0 && input.data2 == NULL) {
                    close(client->sockfd);
                    break;
                }

                // Call the handler and free the buffer
                rpc_data *output = handler(&input);
                free(buffer);

                // Check if the output is NULL
                if (output == NULL) {
                    // Send the null output flag (1 for NULL)
                    int null_output = 1;
                    if (send_int(client->sockfd, null_output) == -1) {
                        close(client->sockfd);
                        break;
                    }
                    // Jump to the next iteration of the loop
                    continue;
                } else {
                    // Send the null output flag (0 for not NULL)
                    int null_output = 0;
                    if (send_int(client->sockfd, null_output) == -1) {
                        close(client->sockfd);
                        break;
                    }
                }

                // Check if the output is valid
                if (output->data2_len > 0 && output->data2 == NULL) {
                    // Send the invalid output flag (1 for invalid)
                    int invalid_output = 1;
                    if (send_int(client->sockfd, invalid_output) == -1) {
                        close(client->sockfd);
                        break;
                    }
                    // Jump to the next iteration of the loop
                    continue;
                } else {
                    // Send the invalid output flag (0 for valid)
                    int invalid_output = 0;
                    if (send_int(client->sockfd, invalid_output) == -1) {
                        close(client->sockfd);
                        break;
                    }
                }

                // Send the output
                if (send_int(client->sockfd, output->data1) == -1 ||
                    send_sizet(client->sockfd, output->data2_len) == -1 ||
                    (output->data2_len > 0 && send_all(client->sockfd, output->data2, output->data2_len) != 0)) {
                    close(client->sockfd);
                    break;
                }

                // Free the output
                rpc_data_free(output);
            } else {
                // Invalid message type, close the socket
                close(client->sockfd);
                break;
            }
        }

        // Free the client
        free(client);
    }
}

/* Add a client to the thread pool */
/* RETURNS: 0 on success, -1 on error */
static int add_client_to_pool(thread_pool *pool, int sockfd, struct sockaddr_in addr) {
    // Lock the mutex
    pthread_mutex_lock(&pool->mutex);

    // Check if the thread pool is full
    if (pool->client_count >= THREAD_POOL_SIZE) {
        pthread_mutex_unlock(&pool->mutex);
        return -1;
    }

    // Create a new client
    rpc_client *new_client = (rpc_client *)malloc(sizeof(rpc_client));
    new_client->sockfd = sockfd;
    new_client->addr = addr;

    // Add the client to the pool
    pool->clients[pool->client_count] = new_client;
    pool->client_count++;
    pthread_cond_signal(&pool->cond);

    // Unlock the mutex
    pthread_mutex_unlock(&pool->mutex);

    return 0;
}

rpc_client *rpc_init_client(char *addr, int port) {
    // Allocate memory for the client
    rpc_client *cl = (rpc_client *)malloc(sizeof(rpc_client));
    if (!cl) {
        return NULL;
    }

    struct addrinfo hints, *res, *rp;
    int ret;

    // Initialise hints
    memset(&hints, 0, sizeof(hints));
    // Allow IPv6 addresses
    hints.ai_family = AF_INET6;
    // Allow stream sockets
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    // Allow any protocol
    hints.ai_protocol = 0;

    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", port);
    // Get address information
    ret = getaddrinfo(addr, port_str, &hints, &res);
    if (ret != 0) {
        free(cl);
        return NULL;
    }

    // Iterate through the results and connect to the first one that works
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        // Create a socket
        cl->sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (cl->sockfd < 0) {
            perror("socket");
            continue;
        }

        if (connect(cl->sockfd, rp->ai_addr, rp->ai_addrlen) != -1) {
            break;  // Success
        }

        close(cl->sockfd);
    }

    // Check if the connection was successful
    if (rp == NULL) {
        freeaddrinfo(res);
        free(cl);
        return NULL;
    }

    // Set the socket timeout
    set_socket_timeout(cl->sockfd, TIMEOUT);

    // Free memory allocated by getaddrinfo
    freeaddrinfo(res);

    return cl;
}

rpc_handle *rpc_find(rpc_client *cl, char *name) {
    // Check if the function exists on the server side
    if (!function_exists_on_server(cl, name)) {
        return NULL;
    }

    // Allocate memory for the handle
    rpc_handle *h = (rpc_handle *)malloc(sizeof(rpc_handle));
    if (!h) {
        return NULL;
    }

    // Copy the name into the handle
    strncpy(h->name, name, MAX_RPC_NAME_LENGTH - 1);
    h->name[MAX_RPC_NAME_LENGTH - 1] = '\0';

    // Store the client information in the handle
    h->client = cl;

    return h;
}

rpc_data *rpc_call(rpc_client *cl, rpc_handle *h, rpc_data *payload) {
    // Check if the arguments are valid
    if (cl == NULL || h == NULL || payload == NULL) {
        close(cl->sockfd);
        return NULL;
    }

    // Check if the payload data is valid
    if (payload->data2_len > 0 && payload->data2 == NULL) {
        close(cl->sockfd);
        return NULL;
    }

    // Use the client information stored in the handle
    cl = h->client;

    // Send the message type
    int msg_type = MSG_TYPE_FUNC_CALL;
    if (send_int(cl->sockfd, msg_type) == -1) {
        return NULL;
    }

    // Check if the function name is NULL
    if (h->name == NULL) {
        return NULL;
    }

    // Send the function name
    if (send_all(cl->sockfd, h->name, MAX_RPC_NAME_LENGTH) != 0) {
        return NULL;
    }

    // Send the payload
    if (send_int(cl->sockfd, payload->data1) == -1 ||
        send_sizet(cl->sockfd, payload->data2_len) == -1 ||
        (payload->data2_len > 0 && send_all(cl->sockfd, payload->data2, payload->data2_len) != 0)) {
        return NULL;
    }

    // Receive the null output flag
    int null_output;
    if (recv_int(cl->sockfd, &null_output) == -1) {
        return NULL;
    }
    // Check if the output is NULL
    if (null_output) {
        return NULL;
    }

    // Receive the invalid output flag
    int invalid_output;
    if (recv_int(cl->sockfd, &invalid_output) == -1) {
        return NULL;
    }
    // Check if the output is invalid
    if (invalid_output) {
        return NULL;
    }

    // Allocate memory for the result
    rpc_data *result = (rpc_data *)malloc(sizeof(rpc_data));
    if (result == NULL) {
        return NULL;
    }

    // Receive the result
    if (recv_int(cl->sockfd, &result->data1) == -1 ||
        recv_sizet(cl->sockfd, &result->data2_len) == -1) {
        free(result);
        return NULL;
    }

    // Receive the result data only if result->data2_len is not 0
    // If result->data2_len is 0, set result->data2 to NULL
    if (result->data2_len > 0) {
        result->data2 = malloc(result->data2_len);
        if (result->data2 == NULL) {
            free(result);
            return NULL;
        }

        if (recv_all(cl->sockfd, result->data2, result->data2_len) != 0) {
            free(result->data2);
            free(result);
            return NULL;
        }
    } else {
        result->data2 = NULL;
    }
    
    // Check if the result data is valid
    if (result->data1 == 0 && result->data2_len == 0 && result->data2 == NULL) {
        free(result);
        return NULL;
    } else {
        return result;
    }
}

void rpc_close_client(rpc_client *cl) {
    close(cl->sockfd);
    free(cl);
}

void rpc_data_free(rpc_data *data) {
    if (data == NULL) {
        return;
    }
    if (data->data2 != NULL) {
        free(data->data2);
    }
    free(data);
}

/* Set the signal handler for SIGINT */
static void signal_handler(int sig) {
    if (sig == SIGINT) {
        server_stop_flag = 1;
    }
}

/* Setting a timeout for the socket operations */
static void set_socket_timeout(int sockfd, int timeout_sec) {
    struct timeval timeout;
    timeout.tv_sec = timeout_sec;
    timeout.tv_usec = 0;

    // Handle the SO_RCVTIMEO and SO_SNDTIMEO options
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        exit(EXIT_FAILURE);
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        exit(EXIT_FAILURE);
    }
}

/* Check if a function exists on the server */
/* RETURNS: 1 if exists, 0 if does not exist */
int function_exists_on_server(rpc_client *cl, char *name) {
    // Send the message type for function existence query
    int msg_type = MSG_TYPE_FUNC_EXIST_QUERY;
    if (send_int(cl->sockfd, msg_type) == -1) {
        return 0;
    }

    // Send the function name
    if (send_all(cl->sockfd, name, MAX_RPC_NAME_LENGTH) != 0) {
        return 0;
    }

    // Receive the function existence response
    int exists;
    if (recv_int(cl->sockfd, &exists) == -1) {
        return 0;
    }

    return exists;
}

/* Send all data in buffer */
/* RETURNS: 0 on success, -1 on error */
static int send_all(int sockfd, const void *buffer, size_t length) {
    const char *buf = buffer;

    // Send the data in chunks
    while (length > 0) {
        ssize_t sent = write(sockfd, buf, length);
        if (sent <= 0) {
            return -1;
        }
        buf += sent;
        length -= sent;
    }

    return 0;
}

/* Receive all data into buffer */
/* RETURNS: 0 on success, -1 on error */
static int recv_all(int sockfd, void *buffer, size_t length) {
    char *buf = buffer;

    // Receive the data in chunks
    while (length > 0) {
        ssize_t received = read(sockfd, buf, length);
        if (received <= 0) {
            return -1;
        }
        buf += received;
        length -= received;
    }

    return 0;
}

/* Send an integer */
/* RETURNS: 0 on success, -1 on error */
static int send_int(int sockfd, int value) {
    // Convert the integer to network byte order
    uint64_t net_value = htonll(value);
    return send_all(sockfd, &net_value, sizeof(net_value));
}

/* Receive an integer */
/* RETURNS: 0 on success, -1 on error */
static int recv_int(int sockfd, int *value) {
    uint64_t net_value;
    if (recv_all(sockfd, &net_value, sizeof(net_value)) == -1) {
        return -1;
    }
    // Convert the integer to host byte order
    *value = ntohll(net_value);
    return 0;
}

/* Send a size_t value (rpc_data->data2_len)*/
/* RETURNS: 0 on success, -1 on error */
static int send_sizet(int sockfd, size_t value) {
    // Convert the size_t value to network byte order
    uint32_t net_value = htonl(value);
    return send_all(sockfd, &net_value, sizeof(net_value));
}

/* Receive a size_t value (rpc_data->data2_len)*/
/* RETURNS: 0 on success, -1 on error */
static int recv_sizet(int sockfd, size_t *value) {
    uint32_t net_value;
    if (recv_all(sockfd, &net_value, sizeof(net_value)) == -1) {
        return -1;
    }
    // Convert the size_t value to host byte order
    *value = ntohl(net_value);
    return 0;
}

/* Convert a uint64_t value to network byte order */
/* RETURNS: uint64_t value in network byte order */
uint64_t htonll(uint64_t value) {
    const int num = RANDOM_NUMBER_SEED;
    // If the system is little endian, convert the value to network byte order
    if (*(char *)&num == num) {
        const uint32_t high_part = htonl((uint32_t)(value >> 32));
        const uint32_t low_part = htonl((uint32_t)(value & 0xFFFFFFFFLL));
        return ((uint64_t)low_part << 32) | high_part;
    } else {
        // If the system is big endian, return the value
        return value;
    }
}

/* Convert a uint64_t value to host byte order */
/* RETURNS: uint64_t value in host byte order */
uint64_t ntohll(uint64_t value) {
    return htonll(value);
}