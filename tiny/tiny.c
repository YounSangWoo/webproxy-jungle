/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
//void serve_static(int fd, char *filename, int filesize, char *method); //숙제 11.11
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
//void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);//숙제 11.11
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

void echo(int connfd);

//argc : 인자의 갯수, argv : 인자의 배열
int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2)//입력되는 인자의 갯수가 2개가 아니라면 표준 오류를 출력한다.
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]); //Open_listenfd 함수를 통해 듣기 소켓을 오픈 인자로 포트번호를 넘겨준다. argv[0]는 ./tiny
  while (1)                          // 반복적 연결 요청을 접수하기 위한 무한루프
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    //Accept 함수는 1.듣기 식별자,2.소켓 주소 구조체의 주소,3.주소(소켓 구조체)의 길이를 인자로 받는다.
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,0);
    //Getaddrinfo 함수는 호스트의 주소, 서비스 이름(포트번호)의 스트링 표시를 소켓 주소 구조체로 변환한다.
    //Getnameinfo 함수는 호스트의 위를 반대로 소켓 주소 구조체에서 스트링 표시로 변환한다.
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    
    doit(connfd);
    //echo(connfd);// 숙제 문제 11.6-A : echo 함수

    Close(connfd); //자신쪽의 연결 끝을 닫음
  }
}

// doit handles one HTTP transaction

// doit 함수는 main 함수에서 호출되며, connfd를 인자로 받는다.
// 클라이언트가 요청한 데이터가 정적 인지 동적인지에 따라 구분하여 서버에 전달
void doit(int fd)
{
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;
  /* Read request line and headers */
  //클라이언트가 보낸 요청 라인과 헤더를 읽고 분석한다.
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version); // buf에 있는 내용을 method, uri, version이라는 문자열에 저장한다.
  
  //tiny는 GET메소드만 지원 클라이언트가 다른 메소드를 요청하면 에러메세지 보냄
  if (strcasecmp(method, "GET"))
  //if (!(strcasecmp(method, "GET") == 0 || strcasecmp(method, "HEAD") == 0))//숙제 11.11
  {
    clienterror(fd, method, "501", "Not implemented","Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio);

  /* Parse URI from GET request */
  //parse(분석)_uri 함수명 그대로 uri 를 분석하여 file name, cgiargs를 채워 넣는다. 정적 컨텐츠 일경우 1반환 아니면 0
  is_static = parse_uri(uri, filename, cgiargs);
  
  if (stat(filename, &sbuf) < 0)
  {
    clienterror(fd, filename, "404", "Not found","Tiny couldn’t find this file");
    return;
  }
  if (is_static)
  { /* Serve static content */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "Forbidden","Tiny couldn’t read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size);
    //serve_static(fd, filename, sbuf.st_size, method);
  }
  else
  { /* Serve dynamic content */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "Forbidden","Tiny couldn’t run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs);
    //serve_dynamic(fd, filename, cgiargs,method);
  }
}

// 숙제 문제 11.6-A : echo 함수
void echo(int connfd)
{
  size_t n;
  char buf[MAXLINE];
  rio_t rio;
  Rio_readinitb(&rio, connfd); // connfd와 &rio를 연결
  while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0)
  {
    if (strcmp(buf, "\r\n") == 0)
      break;
    printf("server received %d bytes\n", (int)n);
    Rio_writen(connfd, buf, n);
  }
}

//clienterror sends an error message to the client
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];
  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n",body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);
  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

//read_requesthdrs reads and ignores request headers.
/* 
Tiny does not use any of the information in the request headers. It simply reads and
ignores them by calling the read_requesthdrs function 
*/
void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];
  Rio_readlineb(rp, buf, MAXLINE);
  while (strcmp(buf, "\r\n"))
  {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

//parse_uri parses an HTTP URI.
/*  
Tiny assumes that the home directory for static content is its current directory and
that the home directory for executables is ./cgi-bin. Any URI that contains the
string cgi-bin is assumed to denote a request for dynamic content. The default
filename is ./home.html. -> home work 11.10 adder.html
*/

int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;
  // request isfor static content
  if (!strstr(uri, "cgi-bin"))
  { /* Static content */
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    if (uri[strlen(uri) - 1] == '/')
      strcat(filename, "adder.html");
    return 1;
  }
  else
  { /* Dynamic content */
    ptr = index(uri, '?');
    if (ptr)
    {
      strcpy(cgiargs, ptr + 1);
      *ptr = '\0';
    }
    else
      strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

void serve_static(int fd, char *filename, int filesize)
//void serve_static(int fd, char *filename, int filesize, char *method)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];
  /* Send response headers to client */
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));
  printf("Response headers:\n");
  printf("%s", buf);
  
  // if (strcasecmp(method, "HEAD") == 0)
  //   return;
  
  /* Send response body to client */
  srcfd = Open(filename, O_RDONLY, 0);
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  
  //srcp = (char *)Malloc(filesize);//숙제 11.9 Mmap 과 Rio_readn 대신 Malloc rio_readn,riowriten 사용 연결 식별자 복사해보기
  //Rio_readn(srcfd, srcp, filesize); //숙제 11.9

  Close(srcfd);
  Rio_writen(fd, srcp, filesize);
  Munmap(srcp, filesize);

  //free(srcp); //숙제 11.9
}

/* * get_filetype - Derive file type from filename */
void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
  else
    strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs)
//void serve_dynamic(int fd, char *filename, char *cgiargs, char* method)
{
  char buf[MAXLINE], *emptylist[] = {NULL};
  /* Return first part of HTTP response */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  /* 부모 프로세스를 fork하여 자식프로세스를 생성한다. */
  if (Fork() == 0)
  { /* Child */
    /* Real server would set all CGI vars here */
    setenv("QUERY_STRING", cgiargs, 1);
    //setenv("REQUEST_METHOD", method, 1); //숙제11.11
    /* 자식 프로세스에서 얻은 값을 클라이언트 쪽으로 출력하기위해 자식의 표준 출력을 
    connfd로  재지정한다. */
    Dup2(fd, STDOUT_FILENO);  /* Redirect stdout to client */
    Execve(filename, emptylist, environ); /* Run CGI program */
  }

  Wait(NULL); /* Parent waits for and reaps child */
}
