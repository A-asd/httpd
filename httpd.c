#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <assert.h>
#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"
#define handle_error(msg) \
           do {                         \
                printf("%s\n",msg);     \
                exit(-1);               \
            } while (0)
int startup(int *port){
    int httpd = socket(AF_INET,SOCK_STREAM,0);
    if(httpd == -1){
        handle_error("socket");
    }
    struct sockaddr_in my_addr;
    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET;//地址类型ipv4
    my_addr.sin_port = htons(*port);//端口转化为网络字节序
    my_addr.sin_addr.s_addr = htonl(INADDR_ANY);//本机任意可用ip地址，把本机字节序转化为网络字节序
    if (bind(httpd, (struct sockaddr *) &my_addr,sizeof(my_addr)) == -1){
        handle_error("bind");
    }
    if(*port == 0){
        socklen_t namelen = sizeof(my_addr);
        if (getsockname(httpd, (struct sockaddr*)&my_addr, &namelen) == -1)//
            handle_error("getsockname");
        *port = ntohs(my_addr.sin_port);//修改端口号，网络字节序转化成本地字节序
    }
    if (listen(httpd, 5) == -1){
        handle_error("listen");
    }
    return httpd;
}
int get_line(int sock,char *buf,int size){
    int i = 0;
    char c = '\0';
    int n;
 
    /*把终止条件统一为 \n 换行符，标准化 buf 数组*/
    while ((i < size - 1) && (c != '\n'))
    {
        /*一次仅接收一个字节*/
        n = recv(sock, &c, 1, 0);
        /* DEBUG printf("%02X\n", c); */
        if (n > 0)
        {
            /*收到 \r 则继续接收下个字节，因为换行符可能是 \r\n */
            if (c == '\r')
            {
                /*使用 MSG_PEEK 标志使下一次读取依然可以得到这次读取的内容，可认为接收窗口不滑动*/
                n = recv(sock, &c, 1, MSG_PEEK);
                /* DEBUG printf("%02X\n", c); */
                /*但如果是换行符则把它吸收掉*/
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            /*存到缓冲区*/
            buf[i] = c;
            i++;
        }
        else
            c = '\n';
    }
    buf[i] = '\0';
 
    /*返回 buf 数组大小*/
    return(i);
    // char c;
    // int pos = 0;
    // int res = 0;
    // while(pos < len&&c != '\n'){
    //     res = recv(socket,&c,1,0);
    //     if(res == -1)return -1;
    //     if(res == 0)return pos;
    //     if(c == '\r'){
    //         recv(socket,&c,1,MSG_PEEK);
    //         if(c == '\n'){
    //             recv(socket,&c,1,0);
    //             buf[pos++] = c;
    //             break;
    //         }
    //     }
    //     buf[pos++] = c;
    // }
    // return pos;
}
void header(int socket){
    char buf[1024] = "HTTP/1.0 200 OK\r\n";
    //状态行
    send(socket,buf,strlen(buf),0);
    //响应头部
    strcpy(buf, SERVER_STRING);
    send(socket, buf, strlen(buf), 0);
    sprintf(buf,"%s","Content-Type=text/html\r\n");
    send(socket,buf,strlen(buf),0);
    
    //\r\n
    sprintf(buf,"%s","\r\n");
    send(socket,buf,strlen(buf),0);
}
void cat(int socket,char *filename){
    FILE*fp = fopen(filename,"r");
    char buf[1024];
    while(fgets(buf,sizeof(buf),fp)!=NULL){
        send(socket,buf,strlen(buf),0);
    }
    fclose(fp);
}
void serve_file(int socket,char *filename){
    printf("serve_file:%s\n",filename);
    char buf[1024];
    while(get_line(socket,buf,sizeof(buf))!=-1){
        if(!strcmp(buf,"\n"))break;
    }
    header(socket);
    cat(socket,filename);
}
void not_found(int client){
    char buf[256];
    sprintf(buf,"HTTP/1.0 404 Not Found\r\n");
    send(client, buf, strlen(buf), 0);
    /*服务器信息*/
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);

}
void execute_cgi(int socket,char *method,char *path,char*querystr){
    printf("execute_cgi:%s method:%s querystr:%s\n",path,method,querystr);
    int status;
    int pid;
    int content_length = -1;
    char buf[256];
    
    if(strcasecmp("GET",method)==0){
        while(get_line(socket,buf,sizeof(buf))>0){
            if(strcmp(buf,"\n")==0)break;
        }
    }else{
        while(get_line(socket,buf,sizeof(buf))>0){
            if(strcmp(buf,"\n")==0)break;
            //printf("%s",buf);
            buf[15] = 0;
            if(strcasecmp("content-length:",buf) == 0){
                content_length = atoi(&buf[16]);
            }
        }
        if(content_length == -1){
            assert(0);
        }
    }
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(socket, buf, strlen(buf), 0);
    int cgi_input[2];
    int cgi_output[2];
    pipe(cgi_input);
    pipe(cgi_output);
    
    if((pid = fork())<0){
        assert(0);
    }
    
    if(pid == 0){//child
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        assert(dup2(cgi_input[0],0)!=-1);
        assert(dup2(cgi_output[1],1)!=-1);
        close(cgi_input[1]);
        close(cgi_output[0]);
        
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);
        
        if(strcasecmp("POST",method)==0){
            sprintf(length_env,"CONTENT_LENGTH=%d",content_length);
            putenv(length_env);
            
        }else{
            sprintf(query_env,"QUERY_STRING=%s",querystr);
            putenv(query_env);
        }

        execl(path,path,NULL);
        exit(0);
    }else{//parent
        close(cgi_input[0]);
        close(cgi_output[1]);
        char c;
        if(strcasecmp("POST",method) ==0){
            // while(recv(socket,&c,1,0)>0){
            //     write(cgi_input[1],&c,1);
            // }
            for(int i= 0;i<content_length;i++){
                recv(socket,&c,1,0);
                write(cgi_input[1],&c,1);
            }
        }
        while(read(cgi_output[0],&c,1)>0){
            send(socket,&c,1,0);
        }
        close(cgi_input[1]);
        close(cgi_output[0]);
        waitpid(pid, &status, 0);
    }
    
}
void*accept_request(void *pclient){
    
    int client_socket = *(int*)pclient;
    char buf[1024];
    int res = get_line(client_socket,buf,sizeof(buf));
    if(res == -1){
        handle_error("get_line");
    }
    if(strncmp(buf,"POST",sizeof("POST")-1)!=0&&strncmp(buf,"GET",sizeof("GET")-1)!=0){
        //printf("%d  %d\n",strncmp(buf,"POST",sizeof("POST")),strncmp(buf,"GET",sizeof("GET")));
        buf[4] = 0;
        printf("unimplement %s \n",buf);
        return NULL;
    }
    char method[16];
    if(strncmp(buf,"POST",sizeof("POST")-1)==0){
        strncpy(method,buf,sizeof("POST"));
    }
    if(strncmp(buf,"GET",sizeof("GET")-1)==0){
        strncpy(method,buf,sizeof("GET"));
    }
    method[strlen(method)-1] = 0;
    int cgi = 0;
    if(strcmp(method,"POST") == 0){
        cgi = 1;
    }
    char *pstarturl = buf+strlen(method)+1;
    //if method is get, get parameter
    char *querystr = NULL;
    if(strcmp(method,"GET") == 0){
        querystr = strstr(pstarturl,"?");
        if(querystr != NULL){
            querystr += 1;
        }
    }
    char path[512];
    char url[256];
    char *purlend = NULL;
    //printf("pstarturl:%s  method:%s\n",pstarturl,method);
    //printf("buf:%s\npstarturl:%s\nmethod:%s\n",buf,pstarturl,method);
    if(querystr == NULL){
        purlend = strstr(pstarturl," ");
    }else{
        purlend = strstr(pstarturl,"?");
        
    }
    if(purlend == NULL){
        handle_error("wrong:get url");
        return NULL;
    }
    strncpy(url,pstarturl,purlend - pstarturl);
     

    sprintf(path,"htdocs%s",url);

    if(path[strlen(path) - 1] == '/'){
        strcat(path,"index.html");
    }
    
    struct stat statbuf;
    if(stat(path,&statbuf)!=0){
        char buf[1024];
        while(get_line(client_socket,buf,sizeof(buf))!=-1){
            if(strcmp("\n",buf)==0)break;
        }
        not_found(client_socket);
        return NULL;
    }
    if((statbuf.st_mode &S_IXUSR) ||(statbuf.st_mode &S_IXGRP)||(statbuf.st_mode &S_IXOTH)){
        cgi = 1;
    }
    if(cgi){
        execute_cgi(client_socket,method,path,querystr);
        
    }else{
        //printf("asd\n");
        serve_file(client_socket,path);
    }       
    close(client_socket);
    return NULL;
}
int main(){
    int port = 0;
    int httpd = startup(&port);
    printf("http://127.0.0.1:%d/ \n",port);
    pthread_t thread;
    while(1){
        
        int client_socket = accept(httpd,NULL,NULL);
        if(client_socket == -1){
            handle_error("accept");
        }
        if(pthread_create(&thread,NULL,accept_request,(void*)&client_socket)){
            handle_error("pthread_create");
        }
    }
}