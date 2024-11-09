# Custom RPC (Remote Procedure Call) System

## Overview

This project implements a custom RPC (Remote Procedure Call) system in C that enables distributed computing across multiple machines. The system follows a client-server architecture and provides a robust framework for remote function execution with support for non-blocking operations.

## Features

- **Client-Server Architecture**: Implements a complete RPC system with separate client and server components
- **Thread Pool**: Uses a thread pool for handling multiple simultaneous client connections
- **Non-Blocking Operations**: Supports non-blocking operations for improved performance
- **Timeout Handling**: Implements socket timeouts for robust error handling
- **Byte Order Handling**: Manages different system architectures through proper byte order conversion
- **Dynamic Memory Management**: Efficient memory allocation and deallocation
- **Signal Handling**: Graceful server shutdown with SIGINT handling

## Technical Specifications

### Protocol Design

The protocol uses a custom packet structure:
+----------------+----------------+----------------+------------------------+
| Packet Type | Message Type | Payload Length | Payload |
| (1 byte)    | (1 byte)     | (4 bytes)      | (Variable Length) |
+----------------+----------------+----------------+------------------------+

### Message Types
- Function Existence Query (1)
- Function Call (2)
- Results (3)
- Errors (4)

### Key Components

1. **Server Component**
   - Initialization and configuration
   - Function registration system
   - Thread pool management
   - Client connection handling

2. **Client Component**
   - Server connection management
   - Remote function discovery
   - Remote procedure calls
   - Result handling

## API Reference

### Server-side Functions

```c
rpc_server *rpc_init_server(int port);
int rpc_register(rpc_server *srv, char *name, rpc_handler handler);
void rpc_serve_all(rpc_server *srv);
```

### Client-side Functions

```c
rpc_client *rpc_init_client(char *addr, int port);
rpc_handle *rpc_find(rpc_client *cl, char *name);
rpc_data *rpc_call(rpc_client *cl, rpc_handle *h, rpc_data *payload);
void rpc_close_client(rpc_client *cl);
```

### Shared Functions

```c
void rpc_data_free(rpc_data *data);
```

## Building the Project

The project uses a Makefile for compilation. To build:

```bash
make all
```

This will create the static library `rpc.a`.

## Security Considerations

- The system currently accepts connections from all clients
- No built-in authentication mechanism
- Uses TCP for reliable communication
- Implements basic error checking and handling

## Technical Details

### Threading Model
- Uses a fixed-size thread pool (16 threads)
- Supports up to 128 simultaneous connections
- Implements mutex locks for thread synchronization

### Memory Management
- Dynamic allocation for client/server structures
- Proper cleanup mechanisms for all allocated resources
- Memory leak prevention through systematic free operations

### Network Communication
- Uses IPv6-compatible sockets
- Implements timeout mechanisms (64-second default)
- Handles different endianness across systems

## Limitations

1. Maximum function name length: 1024 bytes
2. Maximum registered functions: 128
3. Fixed thread pool size: 16 threads
4. Fixed connection backlog: 128 connections

## License

This project is licensed under the MIT License - see the LICENSE file for details.