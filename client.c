/*
 * soc.c - program to open sockets to remote machines
 *
 * $Author: kensmith $
 * $Id: soc.c 6 2009-07-03 03:18:54Z kensmith $
 */

static char svnid[] = "$Id: soc.c 6 2009-07-03 03:18:54Z kensmith $";

#define BUF_LEN 8192

#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>
#include    <ctype.h>
#include    <sys/types.h>
#include    <sys/socket.h>
#include    <netdb.h>
#include    <netinet/in.h>
#include    <inttypes.h>
#include    <time.h>
#include    <sys/stat.h>
#include	<unistd.h>

char *progname;
char buf[BUF_LEN];

void usage();
int setup_client();
int setup_server();
void parse_buf(char[]);
void send_metadata(char[]);
int send_to_client(char[]);
void get_last_modified(char *);
void get_content_length(char *);
void get_type(char *);
void send_basic_info(int);
void send_file(char *);

int s, sock, ch, server, done, bytes, aflg, q_time, threadnum;
int soctype = SOCK_STREAM;
char *host = NULL;
char *port = NULL;
char *sch_policy = NULL;
char *log_file = NULL;
char *r_dir = NULL;
char parsed_strings[3][50];
extern char *optarg;
extern int optind;

int
main(int argc,char *argv[])
{
	fd_set ready;
	struct sockaddr_in msgfrom;
	int msgsize;
	union {
	uint32_t addr;
	char bytes[4];
	} fromaddr;

	if ((progname = rindex(argv[0], '/')) == NULL)
	progname = argv[0];
	else
	progname++;
	while ((ch = getopt(argc, argv, "adsp:c:htnr:")) != -1)
	switch(ch) {
		case 'a':
			aflg++;     /* print address in output */
			break;
		case 'd':
			soctype = SOCK_DGRAM;
			break;
		case 's':
			server = 1;
			sch_policy = optarg;
			break;
		case 'p':
			port = optarg;
			break;
		case 'c':   // changed from 'h' to 'c'
			host = optarg;
			break;
		case 'h':
			usage();
			break;
		case 't':
			q_time = atoi(optarg);    
			break;
		case 'n':
			threadnum = atoi(optarg);
			break;
		case 'l':
			log_file = optarg;
			break;
		case 'r':
			r_dir = optarg;
			chdir(r_dir);
			//char f_name[] = "/ex.txt";	//These two lines were added for testing chdir()
			//get_last_modified(f_name);
			break;
		case '?':
			fprintf(stderr, "unknown arg encountered\n");
		default:
			usage();
	}
	argc -= optind;
	if (argc != 0)
	usage();
	if (!server && (host == NULL || port == NULL))
	usage();
	if (server && host != NULL)
	usage();
/*
 * Create socket on local host.
 */
	if ((s = socket(AF_INET, soctype, 0)) < 0) {
	perror("socket");
	exit(1);
	}
	if (!server)
	sock = setup_client();
	else
	sock = setup_server();
/*
 * Set up select(2) on both socket and terminal, anything that comes
 * in on socket goes to terminal, anything that gets typed on terminal
 * goes out socket...
 */
	while (!done) {
	FD_ZERO(&ready);
	FD_SET(sock, &ready);
	FD_SET(fileno(stdin), &ready);
	if (select((sock + 1), &ready, 0, 0, 0) < 0) {
		perror("select");
		exit(1);
	}
	if (FD_ISSET(fileno(stdin), &ready)) {  // it is reading from the keyboard here, change it to the file requested=
		if ((bytes = read(fileno(stdin), buf, BUF_LEN)) <= 0)   
		done++;
		send(sock, buf, bytes, 0);
	}
	msgsize = sizeof(msgfrom);
	if (FD_ISSET(sock, &ready)) {
		if ((bytes = recvfrom(sock, buf, BUF_LEN, 0, (struct sockaddr *)&msgfrom, &msgsize)) <= 0) {    // PARSE HERE
		done++;
		} else if (aflg) {
		fromaddr.addr = ntohl(msgfrom.sin_addr.s_addr);
		fprintf(stderr, "%d.%d.%d.%d: ", 0xff & (unsigned int)fromaddr.bytes[0],
			0xff & (unsigned int)fromaddr.bytes[1],
			0xff & (unsigned int)fromaddr.bytes[2],
			0xff & (unsigned int)fromaddr.bytes[3]);
		}

		//write(fileno(stdout), buf, bytes);
		/*
		probably need to parse buf and send back the response here
		*/

		if (server)
		parse_buf(buf);     // if seg fault then too many arguments were sent
		else
		write(fileno(stdout), buf, bytes);

		// can put try catch here for rhobustness
		memset(buf, 0, sizeof buf);
		}
	}
	return(0);
}

/*
 * setup_client() - set up socket for the mode of soc running as a
 *      client connecting to a port on a remote machine.
 */

int
setup_client() {

	struct hostent *hp, *gethostbyname();
	struct sockaddr_in serv;
	struct servent *se;

/*
 * Look up name of remote machine, getting its address.
 */
	if ((hp = gethostbyname(host)) == NULL) {
	fprintf(stderr, "%s: %s unknown host\n", progname, host);
	exit(1);
	}
/*
 * Set up the information needed for the socket to be bound to a socket on
 * a remote host.  Needs address family to use, the address of the remote
 * host (obtained above), and the port on the remote host to connect to.
 */
	serv.sin_family = AF_INET;
	memcpy(&serv.sin_addr, hp->h_addr, hp->h_length);
	if (isdigit(*port))
	serv.sin_port = htons(atoi(port));
	else {
	if ((se = getservbyname(port, (char *)NULL)) < (struct servent *) 0) {
		perror(port);
		exit(1);
	}
	serv.sin_port = se->s_port;
	}
/*
 * Try to connect the sockets...
 */
	if (connect(s, (struct sockaddr *) &serv, sizeof(serv)) < 0) {
	perror("connect");
	exit(1);
	} else
	fprintf(stderr, "Connected...\n");
	return(s);
}

/*
 * setup_server() - set up socket for mode of soc running as a server.
 */

int
setup_server() {
	struct sockaddr_in serv, remote;
	struct servent *se;
	int newsock, len;

	len = sizeof(remote);
	memset((void *)&serv, 0, sizeof(serv));
	serv.sin_family = AF_INET;
	if (port == NULL)
	//serv.sin_port = htons(atoi(port));
	serv.sin_port = htons(8080);    // default port set to 8080
	else if (isdigit(*port))
	serv.sin_port = htons(atoi(port));
	else {
	if ((se = getservbyname(port, (char *)NULL)) < (struct servent *) 0) {
		perror(port);
		exit(1);
	}
	serv.sin_port = se->s_port;
	}
	if (bind(s, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
	perror("bind");
	exit(1);
	}
	if (getsockname(s, (struct sockaddr *) &remote, &len) < 0) {
	perror("getsockname");
	exit(1);
	}
	fprintf(stderr, "Port number is %d\n", ntohs(remote.sin_port));
	listen(s, 3);
	newsock = s;
	if (soctype == SOCK_STREAM) {
	fprintf(stderr, "Entering accept() waiting for connection.\n");
	int p = 0;
	while(p<3){
		newsock = accept(s, (struct sockaddr *) &remote, &len);
		p++;
	}
	}
	return(newsock);
}

/*
 * usage - print usage string and exit
 */

void
usage()
{
	//fprintf(stderr, "usage: %s -h host -p port\n", progname);
	//fprintf(stderr, "usage: %s -s [-p port]\n", progname);
	fprintf(stderr, "usage: myhttpd [Ã¢Ë†â€™d] [Ã¢Ë†â€™h] [Ã¢Ë†â€™l file] [Ã¢Ë†â€™p port] [Ã¢Ë†â€™r dir] [Ã¢Ë†â€™t time] [Ã¢Ë†â€™n threadnum] [Ã¢Ë†â€™s sched]\n");

	exit(1);
}


void 
parse_buf(char buf[])
{
	//check if empty line
	if(strcmp(buf,"\n") == 0){ 
		//check if previous string was head request or get
		if(strcmp(parsed_strings[0], "HEAD")==0){

			//send_to_client("got head request...\n");
			send_metadata(parsed_strings[1]);
		}
		else if(strcmp(parsed_strings[0], "GET")==0){

			//int client_s;
			//send_to_client("GOT GET REQUEST...!\n");
			//printf("%s%d\n", "sent GET request of size ", client_s);
			send_metadata(parsed_strings[1]);
			send_file(parsed_strings[1]);
		}
	}

	else{
		char *delimeter = strtok(buf," ");
		int i = 0;

		memset(parsed_strings, 0, sizeof parsed_strings); // clean parsed_strings array

		while(delimeter != NULL)
		{ 
			strcpy(parsed_strings[i++], delimeter);
			printf("%s\n",delimeter);
			delimeter = strtok(NULL, " ");
		}
	}
	
}

void
send_metadata(char file_name[])
{
	//printf("%s","SENDING METADATA ");
	//printf("%s",file_name);
	//timestamp = "GMT: %d-%d-%d %d:%d:%d\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec;

	//removes first character in file_name string (should always be a slash)
	memmove(file_name, file_name+1, strlen(file_name));

	if( access( file_name, F_OK ) != -1 ) {
	    // file exists
	    send_basic_info(200);
	    get_last_modified(file_name);

	    //create copy of file name string because get type modifies + makes get content length not work
	    char *name_copy = malloc (1 + strlen (file_name));
	    strcpy (name_copy, file_name);

	    get_type(name_copy);
	    get_content_length(file_name);
	} 
	else {
	    // file doesn't exist
	    send_basic_info(404);
	}
}

void
get_last_modified(char *file_path)	// will need to add functionality to account for directory parameter
{
    struct stat attrib;
    stat(file_path, &attrib);
    char date[10];
    strftime(date, 10, "%d-%m-%y", gmtime(&(attrib.st_ctime)));

    char last_modified[BUF_LEN];
	snprintf(last_modified,sizeof(last_modified),"Last Modified: %s was last modified on %s\n", file_path, date);
    send_to_client(last_modified);

    //last_modified = "The file %s was last modified at %s\n", file_path, date;

    date[0] = 0;
}

void
get_content_length(char *file_path)
{
	//printf("%s\n","!!!!!!!!!!!!!!!!!!!!");
	struct stat attrib;
    stat(file_path, &attrib);
    //printf("!!!!!!!!!!!!!!!!!!!!");
    int file_size = attrib.st_size;

    char content_length[BUF_LEN];
	snprintf(content_length,sizeof(content_length),"Content Length: %d bytes\n\n", file_size);
    send_to_client(content_length);

    //content_length = "The file size is %d bytes\n",file_size;
}

void get_type(char *name){
	char *delimeter = strtok(name,".");
	char *extension;

	while(delimeter != NULL)
	{ 
		extension = delimeter;
		delimeter = strtok(NULL, " ");
	}

	if(strcmp(extension,"txt") == 0 || strcmp(extension,"html") == 0){
		send_to_client("Content Type: text/html\n");
	}
	else{
		send_to_client("Content Type: image/gif\n");
	}
}

//sends timestamp + server name + status code
void 
send_basic_info(int status)
{
	if(status == 200){
		send_to_client("HTTP/1.1 200 OK\n");
	}
	else if(status == 404){
		send_to_client("HTTP/1.1 404 FILE NOT FOUND\n");
	}
	time_t t = time(NULL);
	struct tm tm = *gmtime(&t);
	char timestamp[BUF_LEN];

	snprintf(timestamp,sizeof(timestamp),"Date: %d-%d-%d %d:%d:%d GMT\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	send_to_client(timestamp);
	send_to_client("Server: myhttpd\n");
}

int 
send_to_client(char t_buf[])
{
	int t_bytes;
	//char *t_buf = NULL;
	//t_buf = "GOT GET REQUEST...!";
	t_bytes = (strlen(t_buf) + 1) * sizeof(char);
	return send(sock, t_buf, t_bytes, 0);
}

void send_file(char* filename) 
{ 
    char buff[BUF_LEN]; 
    FILE *file = fopen(filename, "rb"); 
    if (!file)
    {
        printf("Can't open file\n"); 
        return;
    }
    while (!feof(file)) 
    { 
        int val = fread(buff, 1, sizeof buff, file); 
        int c = 0;
        while (c < val){
        	int sent = send(sock, &buff[c], val - c, 0);
            c += sent;
        }
    } 
    fclose(file);
} 