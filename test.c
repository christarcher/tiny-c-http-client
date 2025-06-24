#include "http.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    char* ipaddr = GenerateRandomCloudflareIP();
    HTTPRequestInfo test = {ipaddr, "test.com", 80, -1, HTTP_GET, "/cdn-cgi/trace", CONTENT_TYPE_TEXT_PLAIN, "test", NULL, -1};

    int a = SendHTTPRequest(&test);
    HTTPResponseInfo *b = FetchHTTPResponse(&test);
    if (a == 0 && b->error == 0) {
        printf("[main]: fetch result: \n--------Begin of content--------\n%s--------End of content--------\n", b->l7.content);
    }
    FreeHTTPResponseResource(b);
    free(ipaddr);
}