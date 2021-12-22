#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define LRU_MAGIC_NUMBER 9999
// Least Recently Used
// LRU: 가장 오랫동안 참조되지 않은 페이지를 교체하는 기법

#define CACHE_OBJS_COUNT 10

                 /* You won't lose style points for including this long line in your code */
                 static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *requestline_hdr_format = "GET %s HTTP/1.0\r\n";
static const char *endof_hdr = "\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";

static const char *host_key = "Host";
static const char *connection_key = "Connection";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *user_agent_key = "User-Agent";

void *thread(void *vargsp);
void doit(int connfd);
void parse_uri(char *uri, char *hostname, char *path, int *port);
void build_http_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio);
int connect_endServer(char *hostname, int port, char *http_header);

// cache function
void cache_init();
int cache_find(char *url);
void cache_uri(char *uri, char *buf);

void readerPre(int i);
void readerAfter(int i);

typedef struct
{
    char cache_obj[MAX_OBJECT_SIZE];
    char cache_url[MAXLINE];
    int LRU;
    int isEmpty;

    int readCnt;      // count of readers
    sem_t wmutex;     // protects accesses to cache
    sem_t rdcntmutex; // protects accesses to readcnt
} cache_block;

typedef struct
{
    cache_block cacheobjs[CACHE_OBJS_COUNT]; // ten cache blocks
    int cache_num;
} Cache;

Cache cache;

int main(int argc, char **argv)
{
    int listenfd, connfd;
    socklen_t clientlen;
    char hostname[MAXLINE], port[MAXLINE];
    pthread_t tid;
    struct sockaddr_storage clientaddr;

    cache_init();

    if (argc != 2)
    {
        // fprintf: 출력을 파일에다 씀. strerr: 파일 포인터
        fprintf(stderr, "usage: %s <port> \n", argv[0]);
        exit(1); // exit(1): 에러 시 강제 종료
    }
    Signal(SIGPIPE, SIG_IGN);
    // 비정상 종료된 소켓에 접근할시 발생하는 프로세스 종료 signal 을 무시하는 함수
    // 나머지 클라이언트들과의 연결 중인 전체 프로세스를 유지해야하기때문.
    listenfd = Open_listenfd(argv[1]);
    while (1)
    {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s %s).\n", hostname, port);

        // 첫 번째 인자 *thread: 쓰레드 식별자
        // 두 번째: 쓰레드 특성 지정 (기본: NULL)
        // 세 번째: 쓰레드 함수
        // 네 번째: 쓰레드 함수의 매개변수
        Pthread_create(&tid, NULL, thread, (void *)connfd);

        // pthread 에서 해줄테니 main 에서 doit 하거나 close 하지 않는다.
        // doit(connfd);
        // Close(connfd);
    }
    return 0;
}

void *thread(void *vargsp)
{
    int connfd = (int)vargsp;
    // 해당 쓰레드를 원래 프로세스로부터 분리한다.
    // 프로세스로 부터 분리되면 특정 쓰레드가 끝날때 자동으로 메모리를 반환해서
    // 다른 쓰레드가 해당 메모리를 사용할 수 있도록 만든다.
    Pthread_detach(pthread_self());
    doit(connfd);
    Close(connfd);
}

void doit(int connfd)
{
    int end_serverfd;

    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char endserver_http_header[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE];
    int port;

    // rio: client's rio / server_rio: endserver's rio
    rio_t rio, server_rio;

    Rio_readinitb(&rio, connfd);
    Rio_readlineb(&rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version); // read the client reqeust line

    if (strcasecmp(method, "GET"))
    {
        printf("Proxy does not implement the method");
        return;
    } //여기까지 concurrent proxy 와 동일하다.

    char url_store[100];
    strcpy(url_store, uri);

    // the url is cached?
    int cache_index;
    // in cache then return the cache content
    if ((cache_index = cache_find(url_store)) != -1)
    {
        readerPre(cache_index);
        Rio_writen(connfd, cache.cacheobjs[cache_index].cache_obj, strlen(cache.cacheobjs[cache_index].cache_obj));
        readerAfter(cache_index);
        return;
    } // 여기서 캐시를 찾아서 바로 클라이언트에 쏴준다

    // parse the uri to get hostname, file path, port
    parse_uri(uri, hostname, path, &port);

    // build the http header which will send to the end server
    build_http_header(endserver_http_header, hostname, path, port, &rio);

    // connect to the end server
    end_serverfd = connect_endServer(hostname, port, endserver_http_header);
    if (end_serverfd < 0)
    {
        printf("connection failed\n");
        return;
    }

    Rio_readinitb(&server_rio, end_serverfd);

    // write the http header to endserver
    Rio_writen(end_serverfd, endserver_http_header, strlen(endserver_http_header));

    // recieve message from end server and send to the client
    char cachebuf[MAX_OBJECT_SIZE];
    int sizebuf = 0;
    size_t n;
    while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0)
    {
        // printf("proxy received %ld bytes, then send\n", n);
        sizebuf += n;
        if (sizebuf < MAX_OBJECT_SIZE)
            strcat(cachebuf, buf);
        Rio_writen(connfd, buf, n);
    }
    Close(end_serverfd);

    // store it
    if (sizebuf < MAX_OBJECT_SIZE)
    {
        cache_uri(url_store, cachebuf);
    }
}

void build_http_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio)
{
    char buf[MAXLINE], request_hdr[MAXLINE], other_hdr[MAXLINE], host_hdr[MAXLINE];

    // request line
    sprintf(request_hdr, requestline_hdr_format, path);

    // get other request header for client rio and change it
    while (Rio_readlineb(client_rio, buf, MAXLINE) > 0)
    {
        if (strcmp(buf, endof_hdr) == 0)
            break; // EOF

        if (!strncasecmp(buf, host_key, strlen(host_key)))
        {
            strcpy(host_hdr, buf);
            continue;
        }

        if (!strncasecmp(buf, connection_key, strlen(connection_key)) && !strncasecmp(buf, proxy_connection_key, strlen(proxy_connection_key)) && !strncasecmp(buf, user_agent_key, strlen(user_agent_key)))
        {
            strcat(other_hdr, buf);
        }
    }
    if (strlen(host_hdr) == 0)
    {
        sprintf(host_hdr, host_hdr_format, hostname);
    }
    sprintf(http_header, "%s%s%s%s%s%s%s",
            request_hdr,
            host_hdr,
            conn_hdr,
            prox_hdr,
            user_agent_hdr,
            other_hdr,
            endof_hdr);
    return;
}

// Connect to the end server
inline int connect_endServer(char *hostname, int port, char *http_header)
{
    char portStr[100];
    sprintf(portStr, "%d", port);
    return Open_clientfd(hostname, portStr);
}

// parse the uri to get hostname, file path, port
void parse_uri(char *uri, char *hostname, char *path, int *port)
{
    *port = 80;
    char *pos = strstr(uri, "//");

    pos = pos != NULL ? pos + 2 : uri;

    char *pos2 = strstr(pos, ":");
    // sscanf(pos, "%s", hostname);
    if (pos2 != NULL)
    {
        *pos2 = '\0';
        sscanf(pos, "%s", hostname);
        sscanf(pos2 + 1, "%d%s", port, path);
    }
    else
    {
        pos2 = strstr(pos, "/");
        if (pos2 != NULL)
        {
            *pos2 = '\0'; // 중간에 끊으려고
            sscanf(pos, "%s", hostname);
            *pos2 = '/';
            sscanf(pos2, "%s", path);
        }
        else
        {
            scanf(pos, "%s", hostname);
        }
    }
    return;
}

void cache_init()
{
    cache.cache_num = 0;
    int i;
    for (i = 0; i < CACHE_OBJS_COUNT; i++)
    {
        cache.cacheobjs[i].LRU = 0;
        cache.cacheobjs[i].isEmpty = 1;

        // Sem_init 첫 번째 인자: 초기화할 세마포어의 포인터
        // 두 번째: 0 - 쓰레드들끼리 세마포어 공유, 그 외 - 프로세스 간 공유
        // 세 번째: 초기 값
        Sem_init(&cache.cacheobjs[i].wmutex, 0, 1);
        Sem_init(&cache.cacheobjs[i].rdcntmutex, 0, 1);
        // what is semaphore?
        // 공유된 하나의 자원에 여러개의 프로세스가 동시에 접근할대 발생하는 문제를 다루는 방법.
        // -> 한번에 하나의 프로세스만 접근 가능하도록 만들어줘야함.
        // -> 이 방법이 프로세스에서 사용될때는 세마포어, 쓰레드에서는 뮤텍스 라고 함.
        // semaphore 값을 할당하는데 이때 값이 0이면 접근할 수 없고 0보다 크면 해당자원에 접근가능
        // 초기값을 1 로 놓는 이유.

        cache.cacheobjs[i].readCnt = 0;
    }
}

void readerPre(int i)
{
    P(&cache.cacheobjs[i].rdcntmutex);
    cache.cacheobjs[i].readCnt++;
    if (cache.cacheobjs[i].readCnt == 1)
        P(&cache.cacheobjs[i].wmutex);
    V(&cache.cacheobjs[i].rdcntmutex);
}

void readerAfter(int i)
{
    P(&cache.cacheobjs[i].rdcntmutex);
    cache.cacheobjs[i].readCnt--;
    if (cache.cacheobjs[i].readCnt == 0)
        V(&cache.cacheobjs[i].wmutex);
    V(&cache.cacheobjs[i].rdcntmutex);
}

int cache_find(char *url)
{
    int i;
    for (i = 0; i < CACHE_OBJS_COUNT; i++)
    {
        readerPre(i);
        if ((cache.cacheobjs[i].isEmpty == 0) && (strcmp(url, cache.cacheobjs[i].cache_url) == 0))
            break;
        readerAfter(i);
    }
    if (i >= CACHE_OBJS_COUNT)
        return -1;
    return i;
}

int cache_eviction()
{
    int min = LRU_MAGIC_NUMBER;
    int minindex = 0;
    int i;
    for (i = 0; i < CACHE_OBJS_COUNT; i++)
    {
        readerPre(i);
        // cache block 이 다 차있지 않을때는 비어있는 인덱스를 반환
        if (cache.cacheobjs[i].isEmpty == 1)
        {
            minindex = i;
            readerAfter(i);
            break;
        }
        // cache block 이 다 차있을때는 가장 낮은 LRU를 가진 cache block을
        // 덮어쓰기위해 minindex로 갱신하고 이 값을 리턴.
        // -> 'eviction policy'
        if (cache.cacheobjs[i].LRU < min)
        {
            minindex = i;
            min = cache.cacheobjs[i].LRU;
            readerAfter(i);
            continue;
        }
        readerAfter(i);
    }
    return minindex;
}

void writePre(int i)
{
    P(&cache.cacheobjs[i].wmutex);
}

void writeAfter(int i)
{
    V(&cache.cacheobjs[i].wmutex);
}

// update the LRU number except the new cache one
// 자신을 제외한 cache block 들의 LRU를 내려주는 함수.
void cache_LRU(int index)
{
    int i;
    for (i = 0; i < index; i++)
    {
        writePre(i);
        if (cache.cacheobjs[i].isEmpty == 0 && i != index)
            cache.cacheobjs[i].LRU--;
        writeAfter(i);
    }
    i++;
    for (i; i < CACHE_OBJS_COUNT; i++)
    {
        writePre(i);
        if (cache.cacheobjs[i].isEmpty == 0 && i != index)
        {
            cache.cacheobjs[i].LRU--;
        }
        writeAfter(i);
    }
}

// cache the uri and content in cache
void cache_uri(char *uri, char *buf)
{
    int i = cache_eviction();

    writePre(i);

    strcpy(cache.cacheobjs[i].cache_obj, buf);
    strcpy(cache.cacheobjs[i].cache_url, uri);
    cache.cacheobjs[i].isEmpty = 0;
    cache.cacheobjs[i].LRU = LRU_MAGIC_NUMBER;
    cache_LRU(i); // 나빼고 다 LRU 하나씩 내려라. 난 9999 할거니까

    writeAfter(i);
}