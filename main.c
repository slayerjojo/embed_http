#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "embed_http.h"

static void status(const char *protocol, int code, const char *message)
{
    printf("protocol:%s code:%d message:%s\n", protocol, code, message);
}

static void header(const char *key, const char *value)
{
    printf("%s: %s\n", key, value);
}

static void body(uint32_t offset, const uint8_t *body, uint32_t size)
{
    printf("%s", (const char *)body);
}

int main(int argc, const char *argv[])
{
    if (argc < 4)
    {
        printf("less arguments");
        return -1;
    }

    EmbedHttpInstance *http = embed_http_create(argv[1], atoi(argv[2]));

    int ret = 0;
    while (!(ret = embed_http_connected(http)));
    if (ret < 0)
        return -1;

    EmbedHttpTask task;
    while (!(ret = embed_http_request(http, &task, argv[3], HTTP_METHOD_GET)));
    if (ret < 0)
        return -1;

    while (!(ret = embed_http_task_update(&task)));
    if (ret < 0)
        return -1;

    while (!(ret = embed_http_header_add(http, &task, "Host", argv[1])));
    if (ret < 0)
        return -1;

    while (!(ret = embed_http_task_update(&task)));
    if (ret < 0)
        return -1;

    while (!(ret = embed_http_header_add(http, &task, "Content-Length", "0")));
    if (ret < 0)
        return -1;

    while (!(ret = embed_http_task_update(&task)));
    if (ret < 0)
        return -1;

    while (!(ret = embed_http_header_end(http, &task)));
    if (ret < 0)
        return -1;

    while (!(ret = embed_http_task_update(&task)));
    if (ret < 0)
        return -1;

    EmbedHttpResponse responser = {
        status,
        header,
        body
    };
    while (!(ret = embed_http_response(http, &responser)));
    if (ret < 0)
        return -1;

    embed_http_release(http);

    return 0;
}
