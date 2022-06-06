#include "io_helper.h"
#include "request.h"

#define MAXBUF (8192)

// structure of items in buffer
typedef struct buffer_item
{
    int fd;
    char* file_name;
    int file_size;
    struct buffer_item* next;
}Node;

Node* createNewItem(int fd,char* file_name,int file_size)
{
	Node* new_item = (Node*)malloc(sizeof(Node));
	new_item->fd = fd;
	new_item->file_name = file_name;
	new_item->file_size = file_size;
	new_item->next = NULL;
	return new_item;
}

// queue 
typedef struct queue
{
    int no_of_items;
    Node* front;
    Node* rear;
}Queue;

Queue* insert_item(Queue* q,Node* new_item)
{
	if(q->front == NULL)
	{
			q->front = new_item;
			q->rear = new_item;
	}
	else
	{
		q->rear->next = new_item;
		q->rear = q->rear->next;
	}
    q->no_of_items = q->no_of_items + 1;
    return q;
}

Queue* delete_item(Queue* q, Node** del_item)
{
    Node* temp;
    if(q->front == NULL)
    {
        return NULL;
    }
    temp = q->front;
    q->front = q->front->next;
    if (q->front == NULL)
        q->rear = NULL;
    temp->next = NULL;
    q->no_of_items = q->no_of_items - 1;
    *del_item = temp;
    return q;
}

Queue* create()
{
    Queue* q = (Queue*)malloc(sizeof(Queue));
    q->no_of_items = 0;
    q->front = NULL;
    q->rear = NULL;
    return q;
}

//
// Sends out HTTP response in case of errors
//
void request_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXBUF], body[MAXBUF];
    
    // Create the body of error message first (have to know its length for header)
    sprintf(body, ""
        "<!doctype html>\r\n"
        "<head>\r\n"
        "  <title>OSTEP WebServer Error</title>\r\n"
        "</head>\r\n"
        "<body>\r\n"
        "  <h2>%s: %s</h2>\r\n" 
        "  <p>%s: %s</p>\r\n"
        "</body>\r\n"
        "</html>\r\n", errnum, shortmsg, longmsg, cause);
    
    // Write out the header information for this response
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    write_or_die(fd, buf, strlen(buf));
    
    sprintf(buf, "Content-Type: text/html\r\n");
    write_or_die(fd, buf, strlen(buf));
    
    sprintf(buf, "Content-Length: %lu\r\n\r\n", strlen(body));
    write_or_die(fd, buf, strlen(buf));
    
    // Write out the body last
    write_or_die(fd, body, strlen(body));
    
    // close the socket connection
    close_or_die(fd);
}
//
// Reads and discards everything up to an empty text line
//
void request_read_headers(int fd) {
    char buf[MAXBUF];
    
    readline_or_die(fd, buf, MAXBUF);
    while (strcmp(buf, "\r\n")) {
        readline_or_die(fd, buf, MAXBUF);
    }
    return;
}

//
// Return 1 if static, 0 if dynamic content (executable file)
// Calculates filename (and cgiargs, for dynamic) from uri
//
int request_parse_uri(char *uri, char *filename, char *cgiargs) {
    char *ptr;
    
    if (!strstr(uri, "cgi")) { 
    // static
    strcpy(cgiargs, "");
    sprintf(filename, ".%s", uri);
    if (uri[strlen(uri)-1] == '/') {
        strcat(filename, "index.html");
    }
    return 1;
    } else { 
    // dynamic
    ptr = index(uri, '?');
    if (ptr) {
        strcpy(cgiargs, ptr+1);
        *ptr = '\0';
    } else {
        strcpy(cgiargs, "");
    }
    sprintf(filename, ".%s", uri);
    return 0;
    }
}

//
// Fills in the filetype given the filename
//
void request_get_filetype(char *filename, char *filetype) {
    if (strstr(filename, ".html")) 
        strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif")) 
        strcpy(filetype, "image/gif");
    else if (strstr(filename, ".jpg")) 
        strcpy(filetype, "image/jpeg");
    else 
        strcpy(filetype, "text/plain");
}

//
// Handles requests for static content
//
void request_serve_static(int fd, char *filename, int filesize) {
    int srcfd;
    char *srcp, filetype[MAXBUF], buf[MAXBUF];
    
    request_get_filetype(filename, filetype);
    srcfd = open_or_die(filename, O_RDONLY, 0);
    
    // Rather than call read() to read the file into memory, 
    // which would require that we allocate a buffer, we memory-map the file
    srcp = mmap_or_die(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    close_or_die(srcfd);
    
    // put together response
    sprintf(buf, ""
        "HTTP/1.0 200 OK\r\n"
        "Server: OSTEP WebServer\r\n"
        "Content-Length: %d\r\n"
        "Content-Type: %s\r\n\r\n", 
        filesize, filetype);
       
    write_or_die(fd, buf, strlen(buf));
    
    //  Writes out to the client socket the memory-mapped file 
    write_or_die(fd, srcp, filesize);
    munmap_or_die(srcp, filesize);
}

int created = 0;
Queue* Que = NULL;
pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;   //lock
pthread_mutex_t m1 = PTHREAD_MUTEX_INITIALIZER; //lock
pthread_cond_t empty = PTHREAD_COND_INITIALIZER;   //condition variable
pthread_cond_t fill = PTHREAD_COND_INITIALIZER;    //condition variable

//Inserting according to scheduling policies
Queue* Algo_Scheduler(Queue* q,int fd,char* file_name,int file_size,int algo_type)
{
    pthread_mutex_lock(&m);
    while(q->no_of_items == buffer_max_size)
	{
		pthread_cond_wait(&empty,&m);
	}
    Node* new = createNewItem(fd,file_name,file_size);
    if(algo_type == 0) // for fifo 
    {
        insert_item(q,new);
    }
    else // for sff
    {
        Queue* temp = q;
        Queue* ptr = NULL;
        if(temp->front == NULL || new->file_size < temp->front->file_size) 
        {
            new->next = temp->front;
            temp->front = new;
        }
        else 
        {
            ptr = q;
            while(ptr->front->next != NULL && (new->file_size > ptr->front->next->file_size))
            {
                ptr->front = ptr->front->next;
            }
            new->next = ptr->front->next;
            ptr->front->next = new;
        }
        q->no_of_items = q->no_of_items + 1;
    }
    pthread_cond_signal(&fill);
	pthread_mutex_unlock(&m);
    return q;
}

void* thread_request_serve_static(void* arg)
{
    // TODO: write code to actualy respond to HTTP requests
    Node* n = NULL;
    pthread_mutex_lock(&m1);
    if(created == 0)
    {
        Que = create();
        created = 1;
    }
    pthread_mutex_unlock(&m1);
    while(1)
    {
        pthread_mutex_lock(&m);
        // if(!empty(que))
        // {
        //     pthread_cond_wait(&p_cv,&mut);
        //     printf("Count = %d\n",que->count);
        //     n = delete(que);
        // }
        while(Que->no_of_items == 0)
        {
            pthread_cond_wait(&fill,&m);
        }
        Que = delete_item(Que,&n);
        pthread_cond_signal(&empty);
        pthread_mutex_unlock(&m);
        if(n != NULL)
        {
            request_serve_static(n->fd,n->file_name,n->file_size);
            printf("File Name %s Size %d\n",n->file_name,n->file_size);
        }       
    }
    return NULL;
}

//
// Initial handling of the request
//
void request_handle(int fd) {
    int is_static;
    struct stat sbuf;
    char buf[MAXBUF], method[MAXBUF], uri[MAXBUF], version[MAXBUF];
    char filename[MAXBUF], cgiargs[MAXBUF];
    
    // get the request type, file path and HTTP version
    readline_or_die(fd, buf, MAXBUF);
    sscanf(buf, "%s %s %s", method, uri, version);
    printf("method:%s uri:%s version:%s\n", method, uri, version);

    // verify if the request type is GET is not
    if (strcasecmp(method, "GET")) {
        request_error(fd, method, "501", "Not Implemented", "server does not implement this method");
        return;
    }
    request_read_headers(fd);
    
    // check requested content type (static/dynamic)
    is_static = request_parse_uri(uri, filename, cgiargs);
    
    // get some data regarding the requested file, also check if requested file is present on server
    if (stat(filename, &sbuf) < 0) {
        request_error(fd, filename, "404", "Not found", "server could not find this file");
        return;
    }
    
    // verify if requested content is static
    if (is_static) {
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
            request_error(fd, filename, "403", "Forbidden", "server could not read this file");
            return;
        }
        //code for producer
            //pthread_mutex_lock(&mut);
            Que = Algo_Scheduler(Que,fd,filename,sbuf.st_size,scheduling_algo); // inserting into queue according to schedule
            //pthread_cond_signal(&p_cv);
            //pthread_mutex_unlock(&mut);
        // TODO: write code to add HTTP requests in the buffer based on the scheduling policy

    } else {
        request_error(fd, filename, "501", "Not Implemented", "server does not serve dynamic content request");
    }
}




