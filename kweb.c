#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kthread.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/stat.h>
#include <net/sock.h>

#define BUFFSIZE (1*1024)

static unsigned short port = 8000;
module_param(port, ushort, S_IRUGO);
static unsigned int KiB = 2*1024;
module_param(KiB, uint, S_IRUGO);

#ifdef KWEB_DEBUG
    #define KWEBMSG(_msg,args...) printk( KERN_DEBUG "kweb: " _msg, ## args)
#else
    #define KWEBMSG(_msg,args...) /* print nothing */
#endif
#define _KWEBMSG(_msg,args...) /* print nothing */

static int connection_handler(void *data);
void http_server(struct socket *csocket);
int sendmsg(struct socket *csocket, const void *data, size_t datalength, int flags);



static struct task_struct *connection_tsk;


static int connection_handler(void *data)
{
    int rc,s_status;
    struct socket *sock = NULL;
    struct socket *newsock = NULL;
    struct sockaddr_in locaddr;
    int one = 1;
    
    KWEBMSG("Socket server start.\n");

    rc = sock_create_kern(PF_INET, SOCK_STREAM, 0, &sock);
    if ( rc < 0 ) {
        KWEBMSG( "ERROR - Could not create socket\n");
        goto threadout;
    }

    rc = kernel_setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one));
    if (rc < 0) {
        KWEBMSG("Failed to set SO_REUSEADDR on socket: %d", rc);
    }

    memset(&locaddr, 0, sizeof(locaddr));
    locaddr.sin_family = AF_INET;  
    locaddr.sin_port = htons(port);  
    locaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    rc = kernel_bind(sock, (struct sockaddr *)&locaddr, sizeof(locaddr));

    if (rc == -EADDRINUSE) {
        KWEBMSG( "ERROR - Port %d already in use\n",port);
        goto threadout;
    }
    if (rc != 0) {
        KWEBMSG( "ERROR - Can't bind to port %d\n",port);
        goto threadout;
    }

    rc = kernel_listen(sock, 0);

    KWEBMSG("HTTP server listening on port:%d\n",port);
    KWEBMSG("Waiting for client's request\n");


    while ( ! kthread_should_stop() ) {

        rc = wait_event_interruptible_timeout(
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,35))
                *(sock->sk->sk_sleep),
#else
                *sk_sleep(sock->sk),
#endif
                (s_status = kernel_accept(sock, &newsock, O_NONBLOCK)) >= 0,
                5 * HZ 
            );
        /* Always relaunch after 5 sec, so it wouldn't block 'kweb_module_cleanup'*/

        if (rc == 0) {
            _KWEBMSG("Time Out\n");
            continue;
        }

        _KWEBMSG("%d,%d\n",rc,s_status);

        if ( ! kthread_should_stop() ) http_server(newsock);

        KWEBMSG("Close client socket\n");
        if( newsock != NULL ) sock_release(newsock);
        newsock = NULL;

    }
    KWEBMSG("Socket server stop.\n");


    rc = 0;

threadout:
    connection_tsk = NULL;
    if ( sock != NULL ) sock_release(sock);
    sock = NULL;
    return rc;

}

void http_server(struct socket *csocket)
{
    char *request;
    char *response;

    char *ptr_res;

    char * substring;

    int length,i;
    struct msghdr msg;
    struct kvec iov;

    request = kmalloc(BUFFSIZE, GFP_KERNEL);
    memset(request, 0, BUFFSIZE);
    response = kmalloc(BUFFSIZE, GFP_KERNEL);
    memset(response, 0, BUFFSIZE);

    iov.iov_base = (void *)request;
    iov.iov_len = (__kernel_size_t)BUFFSIZE;

    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;

    length = kernel_recvmsg(
        csocket, /*Client socket*/
        &msg, /*Received message*/
        &iov, /*Input s/g array for message data(msg.msg_iov)*/
        1, /*Size of input s/g array(msg.msg_iovlen)*/
        BUFFSIZE, /*Number of bytes to read*/
        0 /*Message flags*/ 
    );

    if ( length <= 0 ) {
        KWEBMSG( "Read from socket failed\n");
        goto out;
    }

    _KWEBMSG("Request:%s\n",request);
    ptr_res = request;

    substring = strsep(&ptr_res," ");
    KWEBMSG("Method:%s\n",substring);

    /* Only support "GET" method */
    if ( substring && strncmp(substring, "GET",3) != 0 ) goto out;

    substring = strsep(&ptr_res," ");
    KWEBMSG("Query:%s\n",substring);

    KWEBMSG("HTTP request received\n");

    if ( substring && strncmp(substring, "/give_me_data",13) == 0 )
    {
        snprintf(response, BUFFSIZE, "HTTP/1.0 200 OK\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: %d\r\n\n",KiB*1024);

        sendmsg(csocket,response,strlen(response),MSG_MORE);
        i = KiB*4;
        while( i-- >= 0 ){
            sendmsg(csocket,"Lorem ipsum, Ei duo fugit errem, assum minimum nam in, aeterno definitionem et per. Decore civibus luptatum ei sea. Id oblique meliore reprimique nec, dolorem ocurreret constituam mea an? Nonumy consul facilisis in has! Ex possim delenit definitiones mei.\n",256,0);
        }

        KWEBMSG("HTTP send msg(%d Kbytes)\n",KiB);
    }
    else {
        snprintf(response, BUFFSIZE, "HTTP/1.0 404 Not Found\r\n\nNot Found\n");
        sendmsg(csocket,response,strlen(response),0);
    }


out:
    kfree(request);
    kfree(response);
}

int sendmsg(struct socket *csocket, const void *data, size_t datalength, int flags)
{
    struct msghdr msg;
    struct kvec iov;
    int len;

    iov.iov_base = (void *)data;
    iov.iov_len = (__kernel_size_t)datalength;

    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;

    msg.msg_flags = flags;

    len = kernel_sendmsg(csocket, &msg, &iov, 1, datalength);
    
    return len;
}

static int __init kweb_module_init(void)
{
    int rv;

    KWEBMSG("Kweb module init\n");

    connection_tsk = kthread_run(connection_handler, NULL, "kweb_connection");
    if (IS_ERR(connection_tsk)) {
        rv = PTR_ERR(connection_tsk);
        KWEBMSG("kthread create ERROR: %d\n",rv);
        connection_tsk = NULL;
        return rv;
    }

    return 0;

}

static void __exit kweb_module_cleanup(void)
{
    if ( connection_tsk != NULL ) kthread_stop(connection_tsk);

    KWEBMSG("Kweb module exit\n");
}

module_init(kweb_module_init);
module_exit(kweb_module_cleanup);
MODULE_DESCRIPTION("Kernel HTTP server for speed test.");
MODULE_LICENSE("GPL");
