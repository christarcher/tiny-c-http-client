#define _GNU_SOURCE
#include "http.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/time.h>

#define HTTP_USER_AGENT "OpenwrtRouter/23.05.5"

const char* HTTPMethodString[HTTP_METHOD_MAX] = {
    [HTTP_GET]     = "GET",
    [HTTP_POST]    = "POST",
    [HTTP_PUT]     = "PUT",
    [HTTP_DELETE]  = "DELETE",
    [HTTP_OPTIONS] = "OPTIONS"
};

const char* HTTPContentTypeString[CONTENT_TYPE_MAX] = {
    [CONTENT_TYPE_TEXT_PLAIN]       = "text/plain",
    [CONTENT_TYPE_OCTET_STREAM]     = "application/octet-stream",
    [CONTENT_TYPE_FORM_URLENCODED]  = "application/x-www-form-urlencoded",
    [CONTENT_TYPE_APPLICATION_JSON] = "application/json"
};

// Generates a random Cloudflare IP address (from the range 104.16.x.x).
char* GenerateRandomCloudflareIP(void) {
	char* buffer = calloc(1, INET_ADDRSTRLEN + 1);
	if (buffer == NULL) return NULL;

    srand((unsigned) time(NULL));
    int random_b = rand() % 252;
    int random_c = rand() % 252;
    snprintf(buffer, INET_ADDRSTRLEN + 1, "104.16.%d.%d", random_b + 1, random_c + 1);
    #ifdef DEBUG
    printf("[GenerateRandomCloudflareIP]: using %s as cloudflare ip\n", buffer);
    #endif
    return buffer;
}

// Resolves a given hostname to an IPv4 address using getaddrinfo.
char* GetIPv4Address(const char* hostname) {
    char* buffer = calloc(1, INET_ADDRSTRLEN);
    if (buffer == NULL) return NULL;

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int status = getaddrinfo(hostname, "80", &hints, &res);
    if (status != 0) {
        #ifdef DEBUG
        printf("[GetIPv4Address]: %s\n", gai_strerror(status));
        #endif
        free(buffer);
        return NULL;
    }

    struct sockaddr_in *ipv4 = (struct sockaddr_in *)res->ai_addr;
    inet_ntop(res->ai_family, &(ipv4->sin_addr), buffer, INET_ADDRSTRLEN);
    freeaddrinfo(res);
    #ifdef DEBUG
    printf("[GetIPv4Address]: resolved %s\n", buffer);
    #endif
    return buffer;
}

// Creates a TCP socket, configures its timeouts, and connects it to the given IP address and port.
// Stores the socket descriptor in the HTTPRequestInfo struct.
static bool createTCPSocket(HTTPRequestInfo *rq) {
	if (!rq || !rq->ipaddr || !rq->port ) return false;
    int sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd < 0) return false;

    struct timeval timeout = { .tv_sec = 10, .tv_usec = 0 };
    setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(rq->port);
    if (inet_pton(AF_INET, rq->ipaddr, &server_addr.sin_addr) <= 0 || connect(sd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        #ifdef DEBUG
        printf("[createTCPSocket]: connect error: %s\n", strerror(errno));
        #endif
        close(sd);
        return false;
    }
    
    rq->sd = sd;
    #ifdef DEBUG
    printf("[createTCPSocket]: created fd %d\n", sd);
    #endif
    return true;
}

// Sends raw data (like GET / HTTP/1.1) through the provided socket.
// Uses MSG_NOSIGNAL to prevent the process from being killed by SIGPIPE if the connection is closed on the other side.
static bool sendTCPRawData(int sd, const char* data, size_t length) {
    if (data == NULL || length == 0) return false;

    size_t remaining = length;
    while (remaining > 0) {
        ssize_t bytes = send(sd, data, remaining, MSG_NOSIGNAL);
        if (bytes < 0) {
            #ifdef DEBUG
            printf("[sendTCPRawData]: error when sending data: %d\n", errno);
            #endif
            if (errno == EINTR) {
                continue;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // wait when the buffer is full
                struct timeval tv;
                tv.tv_sec  = 1;  
                tv.tv_usec = 0;
                fd_set writefds;
                FD_ZERO(&writefds);
                FD_SET(sd, &writefds);

                // wait sd
                int ret = select(sd + 1, NULL, &writefds, NULL, &tv);
                if (ret > 0) {
                    // check if writable
                    if (FD_ISSET(sd, &writefds)) {
                        continue;
                    } else {
                        return false;
                    }
                } else if (ret == 0) {
                    // timeout
                    return false;
                } else {
                    // select error
                    return false;
                }
            } else {
                // other errors that is unable to handle
                return false;
            }

        } else if (bytes == 0) {
            // send() return 0, which means the server closed the connection
            return false;

        } else {
            // successfully sent bytes
            data      += bytes;
            remaining -= bytes;
        }
    }

    #ifdef DEBUG
    printf("[sendTCPRawData]: sent %zu bytes, remain %zu bytes\n", length, remaining);
    #endif
    return true;
}

// Reads data from the socket and stores it in the HTTPResponseInfo structure.
// Expands the buffer if necessary, and handles errors such as EINTR and EAGAIN gracefully.
// If Content-Length is available in the response, checks that the received content matches this length.
static bool readTCPRawData(int sd, HTTPResponseInfo* msg) {
	size_t bufferExpandThreshold = 512; // when leftspace is under this value, realloc
	size_t initBufferSize = 4096; // initial buffer size
	size_t maxBufferSize = 1024 * 1024 * 64; // max buffer size
	size_t readSize = 256; // how many bytes to read() once
	
    if (!msg || sd < 0) return false;
    msg->l4.buffer = (char*)calloc(1, initBufferSize);
    if (!msg->l4.buffer) return false;
    msg->l4.bufferSize = initBufferSize;
    msg->l4.totalSize = 0;

    for (;;) {
        // calculate left space
        int remainingSpace = msg->l4.bufferSize - msg->l4.totalSize;

        // if the leftspace is not enough, then relloac to expand buffer size
        if (remainingSpace < bufferExpandThreshold) {
            int oldBufferSize = msg->l4.bufferSize;
        	msg->l4.bufferSize *= 2;
            if (msg->l4.bufferSize > maxBufferSize) {
            	#ifdef DEBUG
                printf("[readTCPRawData] Buffer size exceeded maximum allowed size %d\n", msg->l4.bufferSize);
                #endif
                return false;
            }

            char *newBuffer = (char*)realloc(msg->l4.buffer, msg->l4.bufferSize);
            if (!newBuffer) return false; // no need free here, it will be freed finally, avoid double free
            memset(newBuffer + oldBufferSize, 0, msg->l4.bufferSize - oldBufferSize);
            msg->l4.buffer = newBuffer;
        }

        ssize_t bytesRead = read(sd, msg->l4.buffer + msg->l4.totalSize, readSize);

        if (bytesRead < 0) {
            if (errno == EINTR) continue; 
            #ifdef DEBUG
            printf("[readTCPRawData]: Read %d bytes from fd %d\n", (int)bytesRead, sd);
            perror("[readTCPRawData]: Read failed");
            #endif
            return false;
        } else if (bytesRead == 0) {
            break;
        }

        msg->l4.totalSize += bytesRead;
    }

    #ifdef DEBUG
    printf("[readTCPRawData]: read result: \n--------Begin of content--------\n%s--------End of content--------\n", msg->l4.buffer);
    printf("[readTCPRawData]: allocated buffer size: %d\n", msg->l4.bufferSize);
    #endif
    return true;
}

// Parses the status line of the HTTP response (e.g., HTTP/1.x 200 OK).
// Validates the HTTP version and extracts the status code.
static bool parseHTTPStatusLine(HTTPResponseInfo* msg, const char* line) {
    if (!msg || !line) return false;

    // Check if the content starts with "HTTP/1.x"
    if (strncmp(line, "HTTP/1.", 7) != 0) return false;

    // Find the first ' '
    char* statusStart = strchr(line, ' ');
    if (!statusStart) return false;
    statusStart++;
    if (!isdigit(*statusStart)) return false;
    int result = atoi(statusStart); // " OK" will be ignored by atoi()
    if (result >= 100 && result <= 999) {
    	#ifdef DEBUG
    	printf("[parseHTTPStatusLine]: parsed http status_code: %d\n", result);
    	#endif
        msg->l7.status_code = result;
    	return true;
    }
    return false;
}

// Parses headers in the HTTP response.
// Splits the header line into field name and value (e.g., Content-Length: 123).
// Specific handling for Content-Length, Set-Cookie, and Transfer-Encoding headers.
static bool parseHTTPHeader(HTTPResponseInfo* msg, const char* line) {
    if (!msg || !line) return false;

    // split header and value by ":"
    const char* colon = strchr(line, ':');
    if (!colon) return false;

    // Extract the field name and value, strncpy the field name into fieldName
    // and take the following content as the field value:
    size_t fieldNameLen = colon - line;
    char fieldName[64];
    strncpy(fieldName, line, fieldNameLen);
    fieldName[fieldNameLen] = '\0';
    const char* fieldValue = colon + 1;
    while (*fieldValue == ' ') fieldValue++; // if multi space exists

    // handle the specified headers if you need, generally the followed are needed to handle
    if (strcasecmp(fieldName, "Content-Length") == 0) msg->l7.content_length = atoi(fieldValue);
    if (strcasecmp(fieldName, "Set-Cookie") == 0) msg->l7.cookie = (char*)fieldValue;
    if (strcasecmp(fieldName, "Transfer-Encoding") == 0 && strcmp(fieldValue, "chunked") == 0) msg->l7.chunkedTransfer = true;

    return true;
}

// Parses the entire HTTP message (status line, headers, and body).
// First, parses the status line, then the headers, and finally extracts the message body.
// If chunked transfer encoding is used, the body is handled separately.
static bool parseHTTPMessage(HTTPResponseInfo* msg) {
    if (!msg || !msg->l4.buffer || msg->l4.totalSize <= 5) return false;

    char* cursor = msg->l4.buffer; // current position
    char* end = msg->l4.buffer + msg->l4.totalSize; // end of the buffer

    // 1. parse status line
    char* lineEnd = strstr(cursor, "\r\n");
    if (!lineEnd) return false;
    *lineEnd = '\0'; // replace crlf with 0x00
    if (!parseHTTPStatusLine(msg, cursor)) return false;
    cursor = lineEnd + 2; // move to next line

    // 2. parse headers
    while (cursor < end) {
        lineEnd = strstr(cursor, "\r\n");
        if (!lineEnd) return false; // invalid header
        *lineEnd = '\0';

        if (cursor == lineEnd) {
            cursor += 2; // eat line
            break;
        }

        if (!parseHTTPHeader(msg, cursor)) return false;
        cursor = lineEnd + 2;
    }

    // 3. parse message
    if (cursor < end) {
        msg->l7.content = cursor;

        int diffSize = (int)(end - cursor);
        #ifdef DEBUG
        printf("[parseHTTPMessage]: content-length=%d real length=%d\n", msg->l7.content_length, diffSize);
        printf("[SendHTTPRequest]: http content body:\n--------Begin of content--------\n%s--------End of content--------\n", msg->l7.content);
        #endif
        // Calculate the actual length and compare it. If they are not the same, it indicates a problem in the transmission.
        // Based on the order of the code, the Content-Length header will be parsed first. So before entering this part of the code,
        // l7.content_length should contain the Content-Length value sent by the server. If the server does not send it, it is initialized to -1 (this is set during initialization).
        // Then, by comparing the pointers, we check if there is any data. If the two pointer positions are not the same, it means there is data.
        // If the server did not send a Content-Length header, the value will be less than 0 (-1 < 0), so we use the difference between the pointers as the content length.
        // If the server sent the Content-Length, the value should be greater than or equal to 0. Therefore, we perform a comparison, and if they don't match, it indicates an issue with the transmission.
        if (msg->l7.content_length >= 0) {
            if (msg->l7.content_length != diffSize) return false;
        } else {
            msg->l7.content_length = diffSize;
            return true;
        }
    }
    if (cursor == end) {
        msg->l7.content = NULL;
        msg->l7.content_length = 0;
        return true;
    }
    return false;
}

// Handles chunked transfer encoding in HTTP responses (used when the Transfer-Encoding: chunked header is present).
// Reads the chunk sizes and copies the body data into the response buffer.
static bool parseChunkedBody(HTTPResponseInfo* msg) {
    // Create a new `parseBuffer` to hold the response headers and copy them over, then clear the original buffer.
    // The reason for copying the entire `l4.bufferSize` is to ensure the buffer is large enough.
    char* parseBuffer = (char*)malloc(msg->l4.bufferSize);
    memcpy(parseBuffer, msg->l7.content, msg->l7.content_length);
    memset(msg->l4.buffer, 0, msg->l4.bufferSize);
    msg->l7.content_length = 0;

    // The original pointer needs to be preserved since it will be used later during the free operation. 
    // parsePtr and parsedSize are used to move forward and keep track of the offset.

    char* parsePtr = parseBuffer;
    
    while (1) {
        // find '\r\n' and move ptr
        char* crlf = strstr(parsePtr, "\r\n");
        if (!crlf) goto error;

        // The length is determined by the difference between the starting pointer and the pointer before the "\r\n". 
        // It needs to be converted to decimal for further calculations. If the length is 0, it indicates the end.
        char hex_size[32] = {0};
        size_t hex_len = crlf - parsePtr;
        if (hex_len >= sizeof(hex_size)) goto error;
        memcpy(hex_size, parsePtr, hex_len);
        char* endptr;
        long chunkSize = strtol(hex_size, &endptr, 16);
        if (*endptr != '\0' || chunkSize < 0) goto error;
        if (chunkSize == 0) break;

        // Move parsePtr to the position after the "\r\n", which is the start of the data chunk.
        // Copy the data in chunks back into the buffer of the structure.
        parsePtr = crlf + 2;
        memcpy(msg->l7.content + msg->l7.content_length, parsePtr, chunkSize);
        msg->l7.content_length += chunkSize;

        // move to the next chunk until the size = 0
        parsePtr += chunkSize + 2;
    }


    free(parseBuffer);
    return true;

error:
    free(parseBuffer);
    return false;
}

// Sends an HTTP request with the specified method (GET, POST, etc.) and headers.
// Includes optional body data if present.
// Sends the request in raw TCP format.
int SendHTTPRequest(HTTPRequestInfo* rq) {
    if (!createTCPSocket(rq)) return -1; // no manually creating socket needed

    if (rq->method < 0 || rq->method >= HTTP_METHOD_MAX) return -1;
    if (rq->content_type < 0 || rq->content_type >= CONTENT_TYPE_MAX) return -1;

    bool sendBody = false;
    int offset = 0;
    if (rq->data_length >= 0 && rq->data) sendBody = true;
    char buffer[4096];
    offset += snprintf(buffer, sizeof(buffer),
        "%s %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Accept: */*\r\n"
        "Accept-Language: en-US\r\n"
        "Connection: close\r\n"
        "User-Agent: "HTTP_USER_AGENT"\r\n"
        "Content-Type: %s\r\n"
        "Cookie: %s\r\n",
        HTTPMethodString[rq->method],
        rq->query, 
        rq->host,
        HTTPContentTypeString[rq->content_type],
        rq->cookie
    );

    // offset is the displacement used to move the pointer and calculate the remaining space. 
    // Since snprintf is used and the HTTP header content is relatively short, there won't be any overflow issues.
    if (sendBody) offset += snprintf(buffer + offset, sizeof(buffer) - offset, "Content-Length: %d\r\n", rq->data_length);
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "\r\n");
    if (offset == sizeof(buffer)) return -1;
    #ifdef DEBUG
    printf("[SendHTTPRequest]: about to send http request:\n--------Begin of content--------\n%s--------End of content--------\n", buffer);
    #endif
    // all of which are printable, so use strlen
    if (!sendTCPRawData(rq->sd, buffer, strlen(buffer))) return -1;
    if (sendBody && !sendTCPRawData(rq->sd, rq->data, rq->data_length)) return -1;
    return 0;
}

// Reads and parses the HTTP response after sending an HTTP request.
// Handles both normal and chunked transfer responses.
HTTPResponseInfo* FetchHTTPResponse(HTTPRequestInfo* rq) {
    HTTPResponseInfo* msg = calloc(1, sizeof(HTTPResponseInfo));
    if (!msg) return NULL;
    msg->error = 0;
    msg->l7.chunkedTransfer = false;
    msg->l7.content_length = -1;
    if (!readTCPRawData(rq->sd, msg)) {
        msg->error = -1;
        goto exit;
    }
    if (!parseHTTPMessage(msg)) {
        msg->error = -2;
        goto exit;
    }
    if (msg->l7.chunkedTransfer && !parseChunkedBody(msg)) {
        msg->error = -3;
        goto exit;
    }

exit:
    close(rq->sd);
    return msg;
}

void FreeHTTPResponseResource(HTTPResponseInfo* msg) {
    if (!msg) return;
    if (msg->l4.buffer) free(msg->l4.buffer);
    free(msg);
}