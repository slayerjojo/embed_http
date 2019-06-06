#include "embed_http.h"
#include <stdio.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

EmbedHttpInstance *embed_http_create(const char *host, uint16_t port)
{
    EmbedHttpInstance *http = (EmbedHttpInstance *)malloc(sizeof(EmbedHttpInstance) + strlen(host) + 1);
    if (!http)
        return 0;
    memset(http, 0, sizeof(EmbedHttpInstance));

    http->fp = -1;

    strcpy(http->host, host);
    http->port = port;

    return http;
}

void embed_http_release(EmbedHttpInstance *http)
{
    if (http->fp >= 0)
        close(http->fp);
    free(http);
}

int embed_http_connected(EmbedHttpInstance *http)
{
    if (0 == http->state)
    {
        http->sa.sin_family = AF_INET;
        http->sa.sin_port = htons(http->port);

        struct hostent *ht = gethostbyname(http->host);
        if (!ht)
            return 0;
        memcpy(&http->sa.sin_addr, ht->h_addr, ht->h_length);
        http->state = 1;
    }
    if (1 == http->state)
    {
        if (http->fp >= 0)
        {
            close(http->fp);
            http->fp = -1;
        }
        http->fp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (http->fp < 0)
            return 0;

        int opts = 1;
        setsockopt(http->fp, SOL_SOCKET, SO_REUSEADDR, (void*)&opts, sizeof(opts));
        opts = fcntl(http->fp, F_GETFL);
        opts |= O_NONBLOCK;
        fcntl(http->fp, F_SETFL, opts);

        http->state = 2;
    }
    if (2 == http->state)
    {
        int ret = connect(http->fp, (struct sockaddr *)&http->sa, sizeof(http->sa));
        if (ret < 0)
        {
            if (errno != EINPROGRESS && errno != EALREADY && errno != EISCONN)
            {
                printf("%d %d %s\n", EINPROGRESS, errno, strerror(errno));
                close(http->fp);
                http->fp = -1;
                http->state = 1;
                return 0;
            }
            if (EISCONN == errno)
                http->state = 3;
        }
        else if (ret == 0)
        {
            http->state = 3;
        }
    }
    if (3 == http->state)
    {
        return 1;
    }
    return 0;
}

int embed_http_task_update(EmbedHttpTask *task)
{
    EmbedHttpInstance *http = task->http;
    if (4 != http->state)
        return -1;
    int ret = send(http->fp, task->buffer + task->pos, task->size - task->pos, 0);
    if (0 == ret)
    {
        close(http->fp);
        http->fp = -1;
        http->state = 1;
        return -1;
    }
    if (ret < 0)
    {
        if(EAGAIN == errno || EWOULDBLOCK == errno)
            return 0;
        close(http->fp);
        http->fp = -1;
        http->state = 1;
        return -1;
    }
    task->pos += ret;
    if (task->pos >= task->size)
    {
        free(task->buffer);
        task->buffer = 0;
        http->state = 3;
        return 1;
    }
    return 0;
}

int embed_http_request(EmbedHttpInstance *http, EmbedHttpTask *task, const char *path, EmbedHttpMethod method)
{
    if (!task)
        return -1;
    if (3 != http->state)
        return -1;

    task->http = http;
    task->pos = 0;
    task->size = 0;
    if (HTTP_METHOD_PUT == method)
        task->size = 4;
    else if (HTTP_METHOD_GET == method)
        task->size = 4;
    task->size += strlen(path);
    task->size += 1 + 8 + 2;
    task->buffer = malloc(task->size + 1);
    if (!task->buffer)
        return 0;
    if (HTTP_METHOD_PUT == method)
        strcpy(task->buffer, "PUT ");
    else if (HTTP_METHOD_GET == method)
        strcpy(task->buffer, "GET ");
    strcat(task->buffer, path);
    strcat(task->buffer, " HTTP/1.1\r\n");
    http->state = 4;

    http->response = 0;
    http->size = 0;
    http->offset = 0;
    http->pos = 0;

    return 1;
}

int embed_http_response(EmbedHttpInstance *http, EmbedHttpResponse *responser)
{
    if (http->state < 3)
        return -1;

    int ret = recv(http->fp, http->buffer + http->pos, 512 - http->pos, 0);
    if (!ret)
    {
        close(http->fp);
        http->fp = -1;
        http->state = 1;
        return -1;
    }
    if (ret < 0)
    {
        if (EAGAIN == errno || EWOULDBLOCK == errno)
            return 0;
        close(http->fp);
        http->fp = -1;
        http->state = 1;
        return -1;
    }
    http->pos += ret;

    while (http->pos)
    {
        if (0 == http->response)
        {
            char *delimiter = strstr(http->buffer, "\r\n");
            if (!delimiter)
                return 0;
            *delimiter = 0;
            http->response = 1;
            http->size = 0;
            if (strncmp(http->buffer, "HTTP", 4))
            {
                close(http->fp);
                http->fp = -1;
                http->state = 1;
                return -1;
            }
            char *protocol = http->buffer;
            char *code = strstr(protocol, " ");
            if (!code)
            {
                close(http->fp);
                http->fp = -1;
                http->state = 1;
                return -1;
            }
            *code++ = 0;
            char *message = strstr(code, " ");
            if (!message)
            {
                close(http->fp);
                http->fp = -1;
                http->state = 1;
                return -1;
            }
            *message++ = 0;

            if (responser->status)
                responser->status(protocol, atoi(code), message);
            if ((uint16_t)(delimiter + 2 - http->buffer) < http->pos)
                memmove(http->buffer, delimiter + 2, http->pos - (delimiter + 2 - http->buffer));
            http->pos -= delimiter + 2 - http->buffer;
        }
        else if (1 == http->response)
        {
            char *delimiter = strstr(http->buffer, "\r\n");
            if (!delimiter)
                return 0;
            *delimiter = 0;
            if (delimiter != http->buffer)
            {
                char *value = strstr(http->buffer, ": ");
                if (!value)
                {
                    close(http->fp);
                    http->fp = -1;
                    http->state = 1;
                    return -1;
                }
                *value = 0;
                value += 2;

                if (!strcmp(http->buffer, "Content-Length"))
                    http->size = atoi(value);
                
                if (responser->header)
                    responser->header(http->buffer, value);
            }
            else
            {
                http->response = 2;
                http->offset = 0;
            }
            if (delimiter + 2 - http->buffer < http->pos)
                memmove(http->buffer, delimiter + 2, http->pos - (delimiter + 2 - http->buffer));
            http->pos -= delimiter + 2 - http->buffer;
        }
        else if (2 == http->response)
        {
            if (responser->body)
                responser->body(http->offset, http->buffer, http->pos);
            http->offset += http->pos;
            http->pos = 0;
            if (http->size == http->offset)
                return 1;
        }
    }
    return 0;
}

int embed_http_header_add(EmbedHttpInstance *http, EmbedHttpTask *task, const char *key, const char *value)
{
    if (!task)
        return -1;
    if (3 != http->state)
        return -1;

    task->http = http;
    task->pos = 0;
    task->size = strlen(key) + 2 + strlen(value) + 2;
    task->buffer = malloc(task->size + 1);
    if (!task->buffer)
        return 0;
    strcpy(task->buffer, key);
    strcat(task->buffer, ": ");
    strcat(task->buffer, value);
    strcat(task->buffer, "\r\n");
    http->state = 4;

    return 1;
}

int embed_http_header_end(EmbedHttpInstance *http, EmbedHttpTask *task)
{
    if (!task)
        return -1;
    if (3 != http->state)
        return -1;

    task->http = http;
    task->pos = 0;
    task->size = 2;
    task->buffer = malloc(2 + 1);
    if (!task->buffer)
        return 0;
    strcpy(task->buffer, "\r\n");
    http->state = 4;

    return 1;
}

int embed_http_body_append(EmbedHttpInstance *http, EmbedHttpTask *task, void *data, uint32_t size)
{
    if (!task)
        return -1;
    if (3 != http->state)
        return -1;

    task->http = http;
    task->pos = 0;
    task->size = size;
    task->buffer = malloc(task->size);
    if (!task->buffer)
        return 0;
    memcpy(task->buffer, data, size);
    http->state = 4;

    return 1;
}