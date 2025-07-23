#include "http.h"
#include <stdio.h>
#include <stdlib.h>

/**
 * HTTP Client Usage Example
 * 
 * This example demonstrates how to make a simple HTTP request using the HTTP client library.
 * 
 * IMPORTANT MEMORY MANAGEMENT NOTES:
 * - All data in HTTPResponseInfo (including l7.content, l7.cookie, etc.) points to internal buffer
 * - The data is only valid until FreeHTTPResponseResource() is called
 * - If you need to use any response data after freeing the response, you MUST copy it before calling free
 * - Use strcpy(), strdup(), or memcpy() to preserve data you need
 * 
 * BASIC REQUEST FLOW:
 * 1. Prepare HTTPRequestInfo structure with target server details
 * 2. Call SendHTTPRequest() to send the request
 * 3. Call FetchHTTPResponse() to receive and parse the response
 * 4. Use response data immediately or copy it to your own buffers
 * 5. Call FreeHTTPResponseResource() to cleanup memory
 */

int main(void) {
    // Step 1: Generate a random Cloudflare IP address for demonstration, if your server is on cloudflare
    // In real applications, you might resolve the hostname or use a specific IP
    char* ipaddr = GenerateRandomCloudflareIP();
    
    // Step 2: Setup the HTTP request structure
    HTTPRequestInfo test = {
        ipaddr,                          // IP address to connect to
        "test.com",                      // HTTP Host header value
        80,                              // Port number (80 for HTTP, 443 for HTTPS)
        -1,                              // Socket file descriptor (auto-managed, random value is accepted)
        HTTP_GET,                        // HTTP method (GET, POST, PUT, DELETE, OPTIONS)
        "/cdn-cgi/trace?page=1",         // Request URL path and query string
        CONTENT_TYPE_TEXT_PLAIN,         // Content-Type header
        NULL,                            // Cookie header (NULL sends empty cookie)
        NULL,                            // POST data (NULL for none)
        -1                               // POST data length (-1 means no data to send)
    };
    
    // Step 3: Send the HTTP request
    int a = SendHTTPRequest(&test);
    
    // Step 4: Fetch and parse the HTTP response
    HTTPResponseInfo *b = FetchHTTPResponse(&test);
    
    // Step 5: Check if both request and response were successful
    if (a == 0 && b->error == 0) {
        printf("[main]: fetch result: \n--------Begin of content--------\n%s--------End of content--------\n", b->l7.content);
        
        // IMPORTANT: If you need to use any response data later, copy it NOW!
        // Example of copying response data before freeing:
        /*
        char* saved_content = NULL;
        char* saved_cookie = NULL;
        
        if (b->l7.content) {
            saved_content = strdup(b->l7.content);  // Copy response body
        }
        
        if (b->l7.cookie) {
            saved_cookie = strdup(b->l7.cookie);    // Copy cookie value
        }
        
        // Use saved_content and saved_cookie after FreeHTTPResponseResource()
        // Don't forget to free(saved_content) and free(saved_cookie) when done!
        */
        
    } else {
        // Handle errors
        if (a != 0) {
            printf("[main]: Failed to send HTTP request, error code: %d\n", a);
        }
        if (b->error != 0) {
            printf("[main]: Failed to parse HTTP response, error code: %d\n", b->error);
        }
    }
    
    // Step 6: Cleanup memory resources
    // WARNING: After this call, all pointers in 'b' (including b->l7.content, b->l7.cookie) become invalid!
    FreeHTTPResponseResource(b);
    
    // Step 7: Free the allocated IP address string
    free(ipaddr);
    
    return 0;
}

/**
 * Additional Usage Notes:
 * 
 * FOR POST REQUESTS:
 * - Set test.method = HTTP_POST
 * - Set test.data to your POST data buffer
 * - Set test.data_length to the length of your POST data
 * - Choose appropriate content_type (e.g., CONTENT_TYPE_APPLICATION_JSON for JSON data)
 * 
 * FOR COOKIE HANDLING:
 * - Set test.cookie to send cookies with request: "sessionid=abc123; token=xyz789"
 * - Access received cookies via b->l7.cookie after FetchHTTPResponse()
 * - Remember to copy cookie data before calling FreeHTTPResponseResource()
 * 
 * ERROR HANDLING:
 * - SendHTTPRequest() returns 0 on success, negative values on error
 * - FetchHTTPResponse() always returns a valid HTTPResponseInfo pointer
 * - Check b->error: 0 = success, -1 = read error, -2 = parse error, -3 = chunked encoding error
 * - Always call FreeHTTPResponseResource() even if there were errors
 */