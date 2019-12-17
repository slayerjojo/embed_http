#ifndef __EMBED_HTTP_H__
#define __EMBED_HTTP_H__

#include <stdint.h>
#include <netinet/in.h>

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

typedef struct _embed_http_buffer
{
    struct _embed_http_buffer *next;

    uint32_t size;
    char buffer[];
}EmbedHttpBuffer;

typedef struct _embed_http_task
{
    EmbedHttpInstance *http;

    uint32_t pos;
    EmbedHttpBuffer *buffer;
}EmbedHttpTask;

typedef struct
{
    void (*status)(void *ctx, const char *protocol, int code, const char *message);
    void (*header)(void *ctx, const char *key, const char *value);
    void (*body)(void *ctx, uint32_t offset, const uint8_t *body, uint32_t size);
}EmbedHttpResponse;

EmbedHttpInstance *embed_http_create(const char *host, uint16_t port);
void embed_http_release(EmbedHttpInstance *http);

int embed_http_connected(EmbedHttpInstance *http);

int embed_http_task_update(EmbedHttpTask *task);
void embed_http_task_clean(EmbedHttpTask *task);

int embed_http_request(EmbedHttpInstance *http, EmbedHttpTask *task, const char *path, const char *method);
int embed_http_response(EmbedHttpInstance *http, EmbedHttpResponse *responser, void *ctx);

int embed_http_header_pack(EmbedHttpInstance *http, EmbedHttpTask *task, const char *key, const char *value);
int embed_http_header_add(EmbedHttpInstance *http, EmbedHttpTask *task, const char *parameter);
int embed_http_header_end(EmbedHttpInstance *http, EmbedHttpTask *task);

int embed_http_body_append(EmbedHttpInstance *http, EmbedHttpTask *task, void *data, uint32_t size);

#endif
