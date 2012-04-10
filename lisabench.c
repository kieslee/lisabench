/* 
 *  Defines
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <event.h>

/*
 * Macros
 */
#define	lisatimersub(tvp, uvp, vvp)						\
	do {								\
		(vvp)->tv_sec = (tvp)->tv_sec - (uvp)->tv_sec;		\
		(vvp)->tv_usec = (tvp)->tv_usec - (uvp)->tv_usec;	\
		if ((vvp)->tv_usec < 0) {				\
			(vvp)->tv_sec--;				\
			(vvp)->tv_usec += 1000000;			\
		}							\
	} while (0)


/* 
 * Vars
 */
static const char * host_def = "10.32.11.124";
static int port_def = 33733;

int concurrency;
int request_nums;
int round_nums;
int conn_type;

int fin;

int run_nums = 0; // use for one round

int * fds = NULL;
struct event *events = NULL;


int get_connect (char * host, int port)
{
    if (host == NULL || port < 0) {
	fprintf (stderr, "get_connect invalid args\n");
	return -1;
    }

    int conn;
    struct sockaddr_in 	servaddr;
    conn = socket (AF_INET, SOCK_STREAM, 0);
    if (conn < 0) {
	fprintf (stderr, "socket failed\n");
	return -1;
    }

    bzero (&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons (port);
    inet_pton (AF_INET, host, &servaddr.sin_addr);

    int ret = connect (conn, (struct sockaddr *)&servaddr, sizeof(servaddr));
    if (ret !=0) {
	fprintf (stderr, "connect failed\n");
	return -1;
    }

    return conn;
}

int read_done (char * buf)
{
    if (strstr (buf, "ok") != NULL) 
	return 1;

    return 0;
}

void read_cb (int sock, short what, void *arg) 
{
    //printf ("in function read_cb\n");
    int block_size = 512;
    char buf[512];
    bzero (buf, 512);

    int nbyte = read (sock, buf, block_size);
    if (nbyte > 0) {
	//fprintf (stdout, "read : %s\n", buf);
	if (read_done (buf))
	    fin += 1;
    }
    else {
	fprintf (stderr, "read failed\n");
    }

    return ;
}


struct timeval *run_once (int cnt)
{
    printf ("cnt : %d\n", cnt);
    int ix;
    for (ix = 0; ix < concurrency; ix++) {
	event_del (&events[ix]);
	event_set (&events[ix], fds[ix], EV_READ | EV_PERSIST, read_cb, NULL);
	event_add (&events[ix], NULL);
    }

    event_loop (EVLOOP_ONCE | EVLOOP_NONBLOCK);

    char * buf = "stat\n";
    int buf_len = strlen(buf);

    while ( (cnt--) > 0) {
        for (ix = 0; ix < concurrency; ix++)
            write (fds[ix], buf, buf_len);

	fin = 0;
	do {
            event_loop (EVLOOP_ONCE);
        } while ( fin != concurrency);
    }

    return NULL;
}


int main(int argc, char * argv[]) 
{
    int ix, iy, c;
    concurrency = 10;
    request_nums = 1000;
    round_nums = 1;
    conn_type = 0; // 1 - long ; 0 - short
    char * host = NULL;
    int port = -1;

    while ( (c = getopt(argc, argv, "c:n:r:t:h:p:")) != -1) {
	switch (c) {
	    case 'c':
		concurrency = atoi (optarg);
		break;
	    case 'n':
		request_nums = atoi (optarg);
		break;
	    case 'r':
		round_nums = atoi (optarg);
		break;
	    case 't':
	        conn_type = atoi (optarg);
		break;
	    case 'h':
		host = strdup (optarg);
		break;
	    case 'p':
		port = atoi (optarg);
		break;
	    default:
		fprintf (stderr, "usage: bench -c -n -r -t\n");
		exit(-1);
	}
    }

    if (host == NULL) 
	host = strdup (host_def);

    if (port == -1) 
	port = port_def;

    // calc the run_nums in one round
    run_nums = request_nums / concurrency;
    if (run_nums < 1) {
	fprintf (stderr, "requset is less than concurrency\n");
	exit (-1);
    }

    // create the sockets
    fds = (int*)malloc(sizeof(int) * concurrency);
    if (fds == NULL) {
	fprintf (stderr, "malloc for fds failed\n");
	exit (-1);
    }
    
    // connect 
    int fd_tmp;
    int concurrency_tmp = concurrency;
    int connect_fail = 0;
    for (ix = 0, iy = 0; ix < concurrency; ix++) {
	fd_tmp = get_connect (host, port);
	if (fd_tmp == -1) {
	    concurrency_tmp -= 1;
	    connect_fail += 1;
	    continue;
	}
	fds[iy++] = fd_tmp;
    }

    // judge
    if (connect_fail == concurrency) {
	fprintf (stderr, "All Connect Failed\n");
	exit (-1);
    }

    concurrency = concurrency_tmp;
    fprintf (stdout, "connect fail : %d\n", connect_fail);

    // create events
    events = (struct event*)malloc(sizeof(struct event) * concurrency);
    if (events == NULL) {
	fprintf (stderr, "Malloc For events Failed\n");
	exit (-1);
    }

    event_init();

    struct timeval ts, te, ta;
    int r_num = 0;
    gettimeofday (&ts, NULL);
    int round_nums_tmp = round_nums;
    while ( (round_nums_tmp--) > 0) {
	r_num += 1;
	run_once(run_nums);
	fprintf (stdout, "%d round finish\n", r_num);
    }
    gettimeofday (&te, NULL);

    lisatimersub (&te, &ts, &ta);


    /* 
       Output 
     */
    fprintf (stdout, "\n\n");
    fprintf (stdout, "Service Name : %s\n", host);
    fprintf (stdout, "Service Port : %d\n", port);
    fprintf (stdout, "\n");

    fprintf (stdout, "Concurrency Level : \t\t%10d\n", concurrency);
    fprintf (stdout, "Requests Nums : \t\t%10d\n", request_nums * round_nums);
    fprintf (stdout, "Round Nums : \t\t\t%10d\n", round_nums);
    fprintf (stdout, "\n");

    double spend_time = (double)ta.tv_sec + (double)(ta.tv_usec) / 1000000;
    fprintf (stdout, "Time taken for tests : \t%20.10g seconds\n", spend_time);
    fprintf (stdout, "Requests per seconds : \t%20.10g [#/sec]\n", \
	                   (double)(request_nums * round_nums) / spend_time);

    return 0;
}

