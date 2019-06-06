#ifndef __EMBED_HTTP_H__
#define __EMBED_HTTP_H__

#include <stdint.h>
#include <netinet/in.h>

typedef enum {
    HTTP_METHOD_GET = 0,
    HTTP_METHOD_PUT
}EmbedHttpMethod;

typedef struct _embed_http_instance
{
    uint8_t state;

    uint8_t response;
    uint32_t size;
    uint32_t offset;
    char buffer[512];
    uint16_t pos;

    int fp;

    struct sockaddr_in sa;

    uint16_t port;
    char host[];
}EmbedHttpInstance;

typedef struct _embed_http_task
{
    EmbedHttpInstance *http;

    uint8_t *buffer;
    uint32_t size;
    uint32_t pos;
}EmbedHttpTask;

typedef struct
{
    void (*status)(const char *protocol, int code, const char *message);
    void (*header)(const char *key, const char *value);
    void (*body)(uint32_t offset, const uint8_t *body, uint32_t size);
}EmbedHttpResponse;

EmbedHttpInstance *embed_http_create(const char *host, uint16_t port);
void embed_http_release(EmbedHttpInstance *http);

int embed_http_connected(EmbedHttpInstance *http);
int embed_http_task_update(EmbedHttpTask *task);

int embed_http_request(EmbedHttpInstance *http, EmbedHttpTask *task, const char *path, EmbedHttpMethod method);
int embed_http_response(EmbedHttpInstance *http, EmbedHttpResponse *responser);

int embed_http_header_add(EmbedHttpInstance *http, EmbedHttpTask *task, const char *key, const char *value);
int embed_http_header_end(EmbedHttpInstance *http, EmbedHttpTask *task);

int embed_http_body_append(EmbedHttpInstance *http, EmbedHttpTask *task, void *data, uint32_t size);

#endif
