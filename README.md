# COMP30023: Computer Systems - Project 2: Remote Procedure Call (RPC)

## Overview

This project focuses on building a custom RPC system to allow computations to be distributed across multiple computers. The project requires implementing an RPC system in C, adhering to the provided API and protocol specifications.

## RPC System Architecture

- Design and implement a simple RPC system using client-server architecture.
- The system will be coded in `rpc.c` and `rpc.h`, linked to either a client or a server.

## Project Details

### API
- Implement RPC API with the following data structures and functions:
  - `rpc_data`: Data structure for passing and receiving data.
  - Server-side API: Functions for server initialization, registration, and serving.
  - Client-side API: Functions for client initialization, closing, finding, and calling procedures.
  - Shared API: Function to free allocated rpc_data.

### Planning Task
- Design an application layer protocol and address specific questions regarding server accessibility, authentication, transport layer protocol, and more.

### Protocol
- Document a simple application layer protocol for the RPC system.

### Test Harness
- Implement `rpc-server` and `rpc-client` executables for testing.

### Stretch Goal: Non-Blocking Performance
- Implement non-blocking operation to handle multiple simultaneous requests efficiently.

## Marking Criteria

The project will be assessed on:
1. Correct client-server module finding and remote procedure calls.
2. Accurate results returned to the client and support for multiple procedures.
3. Portability, safety, and build quality.
4. Quality of software practices.
5. Responses to planning task questions and protocol documentation.
6. Implementation of non-blocking operation.

## Submission

- Code written in C, following specified guidelines.
- Submission through a SHA1 hash of the commit and pushing to a designated repository.

## Testing

- Utilize provided skeleton for basic testing.
- Encouraged to write additional unit and integration tests.
- Continuous Integration (CI) pipeline available for progress feedback.

## Collaboration and Plagiarism

- Strict adherence to individual work and academic integrity.
- Use of AI tools like ChatGPT must be documented and will affect marking.