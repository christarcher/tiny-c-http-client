#ifndef HTTP_H
#define HTTP_H
#include <stdbool.h>

// Information required to initiate a request. The IP address and host are separated to allow custom hosts.
typedef struct {
    char* ipaddr; // IP address
    char* host;   // Host header
    int port;     // Port
    int sd;       // Socket descriptor
    int method;   // Request method: 0-4 corresponds to the array {"GET", "POST", "PUT", "DELETE", "OPTIONS"}
    char* query;  // Query string
    int content_type; // Content type: 0-3 corresponds to {"text/plain", "application/octet-stream", "application/x-www-form-urlencoded", "application/json"}
    char* cookie; // Cookie, if none, an empty string ""
    char* data;   // Data to be sent (can be NULL)
    int data_length; // Length of data to be sent (can be 0)
} HTTPRequestInfo;

// Response information
typedef struct {
    struct {
        char* buffer;     // Buffer for receiving data, must be malloc'd during initialization, needs to be freed later
        int bufferSize;   // Actual buffer size, used for handling chunked transfer
        int totalSize;    // Total amount of data received, including HTTP headers and content
    } l4;
    struct {
        int status_code;  // HTTP status code
        int content_length; // Actual length of the response body
        char* cookie;     // Cookie (pointer within the buffer)
        char* content;    // Response content (pointer within the buffer)
        bool chunkedTransfer; // Whether chunked transfer is used
    } l7;
    int error; // Error code
} HTTPResponseInfo;

// Generate a random Cloudflare edge IP. Note that the memory must be freed after use.
char* GenerateRandomCloudflareIP(void);

// Parse an IPv4 address from a hostname. The returned memory must be freed.
char* GetIPv4Address(const char* hostname);

// Send an HTTP request, requires an HTTPRequestInfo structure.
int SendHTTPRequest(HTTPRequestInfo* rq);

// Fetch an HTTP response, also requires an HTTPRequestInfo structure.
HTTPResponseInfo* FetchHTTPResponse(HTTPRequestInfo* rq);

// Free resources associated with the HTTPResponseInfo structure.
void FreeHTTPResponseResource(HTTPResponseInfo* msg);

#endif