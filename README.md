# multiplayer-trivia
Computer Network Design Course Final Project


# Design Paper:
Multiplayer Game Protocol Inspired by Kahoot

# 1. Project Overview Objective
The project aims to design and implement a client-server network protocol for a multiplayer game similar to Kahoot. The game will support multiple clients simultaneously, allowing players to connect, participate in quizzes, and receive feedback in real-time.

## Key Features:
- Real-time Multiplayer Gameplay: Multiple clients can connect to the server and participate in the quiz simultaneously.
- Socket Programming: Utilizes both multicast (for broadcasting quiz questions) and TCP (for reliable communication of responses and results).
- Multi-threaded Server: The server handles multiple clients concurrently using multi-threading.
- State Management: Both the server and clients maintain state machines to manage the game's flow.
# 2. Managing Multiple Clients
## Multi-threading on the Server
- The server will use multi-threading to manage multiple clients simultaneously. Each client connection will be handled by a separate thread, ensuring that the server can process requests concurrently without blocking.

# 3. Socket Programming
## Sockets Used
- TCP Socket: For reliable communication between the server and each client. This will be used for connection establishment, sending responses, and receiving results.
- Multicast UDP Socket: For broadcasting quiz questions to all clients simultaneously.
# 4. Communication Protocol
## Protocol Details
- Connection Establishment: Clients initiate a TCP connection to the server.
- Quiz Broadcasting: The server uses a multicast UDP socket to broadcast questions.
- Response Submission: Clients send their responses via the established TCP connection.
- Result Broadcasting: The server sends results back to the clients over TCP.
## Timeouts
- Connection Timeout: If a client fails to establish a connection within a specific timeframe, the server will close the connection.
- Response Timeout: Clients must submit their responses within a specified timeframe after receiving a question. Late responses will not be considered.
- Idle Timeout: If a client remains idle for too long, the server may terminate the connection to free up resources.
