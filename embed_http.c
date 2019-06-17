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
    if (!task->buffer)
        return 1;
    int ret = send(http->fp, task->buffer->buffer + task->pos, task->buffer->size - task->pos, 0);
    if (0 == ret)
    {
        close(http->fp);
        http->fp = -1;
        http->state = 1;

        embed_http_task_clean(task);
        return -1;
    }
    if (ret < 0)
    {
        if(EAGAIN == errno || EWOULDBLOCK == errno)
            return 0;
        close(http->fp);
        http->fp = -1;
        http->state = 1;

        embed_http_task_clean(task);
        return -1;
    }
    task->pos += ret;
    if (task->pos >= task->buffer->size)
    {
        task->pos = 0;
        EmbedHttpBuffer *buffer = task->buffer;
        task->buffer = task->buffer->next;
        free(buffer);

        if (task->buffer)
            return 0;
        http->state = 3;
        return 1;
    }
    return 0;
}

void embed_http_task_clean(EmbedHttpTask *task)
{
    if (!task)
        return;
    
    while (task->buffer)
    {
        EmbedHttpBuffer *buffer = task->buffer;
        task->buffer = task->buffer->next;
        free(buffer);
    }
}

static char *task_buffer_add(EmbedHttpTask *task, uint32_t size)
{
    EmbedHttpBuffer *buffer = (EmbedHttpBuffer *)malloc(sizeof(EmbedHttpBuffer) + size + 1);
    if (!buffer)
        return 0;
    buffer->size = size;
    buffer->next = 0;
    if (!task->buffer)
    {
        task->buffer = buffer;
    }
    else
    {
        EmbedHttpBuffer *tail = task->buffer;
        while (tail->next)
            tail = tail->next;
        tail->next = buffer;
    }
    return buffer->buffer;
}

int embed_http_request(EmbedHttpInstance *http, EmbedHttpTask *task, const char *path, EmbedHttpMethod method)
{
    uint32_t size = 0;
    char *buffer = 0;
    if (!task)
        return -1;
    switch (http->state)
    {
        case 3:
        {
            task->http = http;
            task->pos = 0;
            task->buffer = 0;
            break;
        }
        case 4:
            break;
        default:
            return -1;
    }
    if (HTTP_METHOD_POST == method)
        size = 5;
    else if (HTTP_METHOD_GET == method)
        size = 4;
    size += strlen(path);
    size += 1 + 8 + 2;

    buffer = task_buffer_add(task, size);
    if (!buffer)
        return 0;
    if (HTTP_METHOD_POST == method)
        strcpy(buffer, "POST ");
    else if (HTTP_METHOD_GET == method)
        strcpy(buffer, "GET ");
    strcat(buffer, path);
    strcat(buffer, " HTTP/1.1\r\n");
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
                responser->body(http->offset, (uint8_t *)http->buffer, http->pos);
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
    char *buffer = 0;
    if (!task)
        return -1;
    switch (http->state)
    {
        case 3:
        {
            task->http = http;
            task->pos = 0;
            task->buffer = 0;
            break;
        }
        case 4:
            break;
        default:
            return -1;
    }

    buffer = task_buffer_add(task, strlen(key) + 2 + strlen(value) + 2);
    if (!buffer)
        return 0;
    strcpy(buffer, key);
    strcat(buffer, ": ");
    strcat(buffer, value);
    strcat(buffer, "\r\n");
    http->state = 4;

    return 1;
}

int embed_http_header_end(EmbedHttpInstance *http, EmbedHttpTask *task)
{
    char *buffer = 0;
    if (!task)
        return -1;
    switch (http->state)
    {
        case 3:
        {
            task->http = http;
            task->pos = 0;
            task->buffer = 0;
            break;
        }
        case 4:
            break;
        default:
            return -1;
    }

    buffer = task_buffer_add(task, 2);
    if (!buffer)
        return 0;
    strcpy(buffer, "\r\n");
    http->state = 4;

    return 1;
}

int embed_http_body_append(EmbedHttpInstance *http, EmbedHttpTask *task, void *data, uint32_t size)
{
    char *buffer = 0;
    if (!task)
        return -1;
    switch (http->state)
    {
        case 3:
        {
            task->http = http;
            task->pos = 0;
            task->buffer = 0;
            break;
        }
        case 4:
            break;
        default:
            return -1;
    }

    buffer = task_buffer_add(task, size);
    if (!buffer)
        return 0;
    memcpy(buffer, data, size);
    http->state = 4;

    return 1;
}
