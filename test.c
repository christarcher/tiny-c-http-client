#include "http.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    char* ipaddr = GenerateRandomCloudflareIP();
    HTTPRequestInfo test = {ipaddr, "test.com", 80, -1, 0, "/cdn-cgi/trace", 0, "test", NULL, -1};
    // if you need to download large file, you need to change max buffer size in the hardcoded in the http.c
    // this program is used for iot communication, so i didn't set a big size for buffer;

    int a = SendHTTPRequest(&test);
    HTTPResponseInfo *b = FetchHTTPResponse(&test);
    if (a == 0 && b->error == 0) {
        printf("[main]: fetch result: \n--------Begin of content--------\n%s--------End of content--------\n", b->l7.content);
    }
    FreeHTTPResponseResource(b);
    free(ipaddr);
}