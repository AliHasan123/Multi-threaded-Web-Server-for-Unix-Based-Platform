#define BUF_LEN 8192
#define QUEUE_SIZE 100

#include    <stdio.h>
#include    <string.h>
#include    <stdlib.h>
#include    <errno.h>
#include    <unistd.h> 
#include    <arpa/inet.h>    
#include    <sys/types.h>
#include    <sys/socket.h>
#include    <netinet/in.h>
#include    <sys/time.h> 
#include    <ctype.h>
#include    <netdb.h>
#include    <inttypes.h>
#include    <time.h>
#include    <sys/stat.h>
#include	<semaphore.h>
#include	<dirent.h>
#include	<libgen.h>

void usage();
void setup_server();
void parse_buf(int);
void send_metadata(char[], int);
int send_to_client(char[], int);
void get_last_modified(char *, int);
int get_content_length(char *, int, int);
void get_type(char *, int);
void send_basic_info(int, int);
void send_file(char *, int);
void close_client();
void add_to_queue(char *);
void create_thread_pool(void (*)());
void minute_wait();
void schedule_queue();
void execute_FCFS();
void execute_SJF();
int get_sjf_index();
int file_exists(char[]);
void get_file_name(char[]);
void write_log(int);
void list_dir(int);
void get_myhttpd_dir();

char *progname;
int addrlen, new_sock, max_clients = 50, i, max_sd, rv, sd, sock, ch, aflg, q_time, threadnum, queuePosition, cLength, initial_time, sjf_policy, workers, log_flag, d_flag;
int client_socket[50] = {0};
struct sockaddr_in serv;
int soctype = SOCK_STREAM;
char *host = NULL;
char *port = NULL;
char *log_file_name = NULL;
char *r_dir = NULL;
char *curr_request;
char parsed_strings[3][50];
char p_strings[3][50];
extern char *optarg;
extern int optind;
pthread_t *thread_pool;
pthread_t scheduler_thread;
sem_t semaphore;
FILE *logging_file;
pthread_mutex_t queue_mutex;
char myhttpd_dir[1024];

struct qItem{
	char* arrivalTime;
	char* request;
	int fileSize;
	int sd;
	int scheduled;
	//logging info below
	char* ip;
	char* sched_time;
	char* status;
};

struct qItem readyQueue[QUEUE_SIZE];
 
int main(int argc , char *argv[])
{
	get_myhttpd_dir();

	if ((progname = rindex(argv[0], '/')) == NULL)
	progname = argv[0];
	else
	progname++;
	
	sem_init( &semaphore, 0, 1);	//initialize semaphore to 1

	while ((ch = getopt(argc, argv, "ds:p:ht:n:l:r:")) != -1)
	switch(ch) {
		case 'd':
			d_flag = 1;
			max_clients = 1;
			break;
		case 's':   // schedule policy; FCFS => 0 , SJF => 1
			if (!strcmp(optarg, "SJF")){
				sjf_policy = 1; 
			} else {
				sjf_policy = 0;
			}
			break;
		case 'p':
			port = optarg;
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
			log_file_name = optarg;
			log_flag = 1;
			break;
		case 'r':
			r_dir = optarg;
			chdir(r_dir);
			//char f_name[] = "/ex.txt";    //These two lines were added for testing chdir()
			//get_last_modified(f_name);
			break;
		case '?':
			fprintf(stderr, "unknown arg encountered\n");
		default:
			usage();
	}
	argc -= optind;
	if (argc != 0 || host != NULL)
		usage();

	if(d_flag == 0){
		daemon_init();
	}

	setup_server();

	//open file for logging

	return 0;
}

void
usage()
{
	fprintf(stderr, "usage: myhttpd [-d] [-h] [-l file] [-p port] [-r dir] [-t time] [-n threadnum] [-s sched]\n");
	exit(1);
}


void create_thread_pool(void (*execute_algorithm)()){
	if(threadnum == 0){
		//create 4 worker threads default
		workers = 4;
	}
	else{
		//create n threads
		workers = threadnum;
	}
	thread_pool = malloc(sizeof(pthread_t) * workers); 
	int i=0;
	for (i; i<workers; i++){
		pthread_create( &thread_pool[i], NULL, *execute_algorithm, NULL );
		//pthread_join( thread_pool[i], NULL);
	}
}

void setup_server()
{ 
	char buffer[BUF_LEN]; 

	fd_set ready;

	sock = socket(AF_INET , SOCK_STREAM , 0);
	
	serv.sin_family = AF_INET;
	serv.sin_addr.s_addr = htonl(INADDR_ANY);

	//default port set to 8080 unless p flag is passed
	if (port == NULL){
		serv.sin_port = htons(8080);
	}
	else if(isdigit(*port)){
		serv.sin_port = htons(atoi(port));
	}
	  
	if (bind(sock, (struct sockaddr *)&serv, sizeof(serv))<0) 
	{
		perror("bind");
		exit(1);
	}

	fprintf(stderr, "Port number is %d\n", ntohs(serv.sin_port));
	 
	listen(sock, 1);
	
	// starting scheduling thread
	pthread_create( &scheduler_thread, NULL, schedule_queue, NULL );
	//join stops execution untile thread completes
	//pthread_join( scheduler_thread, NULL);
	//printf("SUCCES");

	addrlen = sizeof(serv);
	 
	while(1) 
	{
		FD_ZERO(&ready);
	
		FD_SET(sock, &ready);
		max_sd = sock;
		 
		for (i = 0 ; i<max_clients ; i++) 
		{
			sd = client_socket[i];
			 
			if(sd > 0){
				FD_SET(sd , &ready);
			}
			 
			if(sd > max_sd){
				max_sd = sd;
			}
		}
	
		select(max_sd + 1 , &ready , NULL , NULL , NULL);
		  
		//incoming conn
		if (FD_ISSET(sock, &ready) == 1) 
		{
			new_sock = accept(sock, (struct sockaddr *)&serv, (socklen_t*)&addrlen);
		
			printf("New connection , socket fd is %d , ip is : %s , port : %d \n" , new_sock , inet_ntoa(serv.sin_addr) , ntohs(serv.sin_port));
			
			for (i = 0; i<max_clients; i++) 
			{
				if(client_socket[i] == 0)
				{
					client_socket[i] = new_sock;
					printf("Adding to list of sockets as %d\n" , i);           
					break;
				}
			}
		}
		  
		else{
			for (i = 0; i<max_clients; i++) 
			{
				sd = client_socket[i];
				  
				if (FD_ISSET(sd, &ready)) 
				{
					if ((rv = read(sd , buffer, 1024)) == 0)
					{
						close_client();
					}
				  
					else
					{
						buffer[rv] = '\0';
						//create_thread_pool();
						//parse_buf(buffer);
						if(strcmp(buffer,"\n") == 0){
							pthread_mutex_lock(&queue_mutex); 
							add_to_queue(curr_request);
							pthread_mutex_unlock(&queue_mutex); 
						} else 
						{
							printf("%s\n", buffer);
							curr_request = malloc (1 + strlen (buffer));
							strcpy (curr_request, buffer);
						}
					}
				}
			}
		}
	}
}

void close_client(int counter_index){
	readyQueue[counter_index].ip = inet_ntoa(serv.sin_addr);
	//close connection to client
	getpeername(readyQueue[counter_index].sd, (struct sockaddr*)&serv, (socklen_t*)&addrlen);
	printf("Client has been closed , ip %s , port %d \n" , inet_ntoa(serv.sin_addr) , ntohs(serv.sin_port));
	send_to_client("\n++++++++++++++++++\nYou've been SERVED.\nPlease reconnect for any further requests\n++++++++++++++++++\n", counter_index);
	close(readyQueue[counter_index].sd);
	for (i = 0; i<max_clients; i++){
		if (client_socket[i] == readyQueue[counter_index].sd){
			client_socket[i] = 0;
			break;
		}
	}
}

void 
parse_buf(int counter_index)
{
	char *buf;
	buf = strdup(readyQueue[counter_index].request);
	char *delimeter = strtok(buf," ");
	int i = 0;

	memset(parsed_strings, 0, sizeof parsed_strings); // clean parsed_strings array

	while(delimeter != NULL)
	{ 
		strcpy(parsed_strings[i++], delimeter);
		//printf("%s\n",delimeter);
		delimeter = strtok(NULL, " ");
	}

	char *tilda_char = "~";
	char* dir = dirname(tilda_char);
	if (parsed_strings[0] == '~'){

		char temp_file_name[(sizeof(parsed_strings) + sizeof(myhttpd_dir))];
		memmove(parsed_strings[1], parsed_strings[1]+1, strlen(parsed_strings[1]));
		snprintf(temp_file_name,(sizeof(temp_file_name)),"%s/%s", myhttpd_dir, parsed_strings[1]);
		strcpy( parsed_strings[1], temp_file_name );
		//parsed_strings[1] = temp_file_name;

	}
	//removes first character in file_name string (should always be a slash)
	memmove(parsed_strings[1], parsed_strings[1]+1, strlen(parsed_strings[1]));

	//check if previous string was head request or get
	if(strcmp(parsed_strings[0], "HEAD")==0){
		send_metadata(parsed_strings[1], counter_index);
	}

	else if(strcmp(parsed_strings[0], "GET")==0){
		send_metadata(parsed_strings[1], counter_index);
		send_file(parsed_strings[1], counter_index);
	}
	close_client(counter_index);
	//close(readyQueue[counter_index].sd);
}

void add_to_queue(char *request){
	//create item with all info about request to add to the ready queue 
	//temp.requestType = strdup(parsed_strings[0]);
	//temp.fileName = strdup(parsed_strings[1]);
	struct qItem temp;

	time_t t;
    time(&t);
    temp.arrivalTime = strdup(ctime(&t));

	temp.request = strdup(request);
	get_file_name(request);
	temp.fileSize = get_content_length(p_strings[1], 0, 0);
	temp.sd = sd;
	temp.scheduled = 1;

	readyQueue[queuePosition++] = temp;
}

void
send_metadata(char file_name[], int counter_index)
{
	//printf("%s","SENDING METADATA ");
	//printf("%s",file_name);

	//removes first character in file_name string (should always be a slash)
	//memmove(file_name, file_name+1, strlen(file_name));

	if(file_exists(file_name) == 1) {
		// file exists
		send_basic_info(200, counter_index);

		readyQueue[counter_index].status = "200";

		get_last_modified(file_name, counter_index);

		//create copy of file name string because get type modifies + makes get content length not work
		char *name_copy = malloc (1 + strlen (file_name));
		strcpy (name_copy, file_name);

		get_type(name_copy, counter_index);
		get_content_length(file_name, counter_index, 1);
	} 
	else {
		// file doesn't exist
		send_basic_info(404, counter_index);

		readyQueue[counter_index].status = "404";
	}
}

int file_exists(char* file_name){
	if( access( file_name, F_OK ) != -1 ){
		//file exists
		return 1; 
	}
	else{
		//file doesnt exist
		return 0;
	}
}

// issue with date of ex.txt
void
get_last_modified(char *file_path, int counter_index)  // will need to add functionality to account for directory parameter
{
	struct stat attrib;
	stat(file_path, &attrib);
	char date[10];
	strftime(date, 10, "%d-%m-%y", gmtime(&(attrib.st_ctime)));

	char last_modified[BUF_LEN];
	snprintf(last_modified,sizeof(last_modified),"Last Modified: %s was last modified on %s\n", file_path, date);
	send_to_client(last_modified, counter_index);

	date[0] = 0;
}

int
get_content_length(char *file_path, int counter_index, int flag) 
//flag is for purpose of getting size when adding to queue (dont want to send any info to client yet). if 1 send if 0 dont
{
	struct stat attrib;
	stat(file_path, &attrib);
	int file_size = attrib.st_size;

	char content_length[BUF_LEN];
	snprintf(content_length,sizeof(content_length),"Content Length: %d bytes\n\n", file_size);
	if(flag == 1){
		//send to client
		send_to_client(content_length, counter_index);
	}
	if(file_exists(file_path) == 0){
		file_size = 0;
	}
	return file_size;
}

void get_file_name(char file_name[]){
	memset(p_strings, 0, sizeof p_strings);
	char *delimeter = strtok(file_name," ");
	int i = 0;

	while(delimeter != NULL)
	{ 
		strcpy(p_strings[i++], delimeter);
		delimeter = strtok(NULL, " ");
	}
	memmove(p_strings[1], p_strings[1]+1, strlen(p_strings[1]));
}

void 
get_type(char *name, int counter_index)
{
	char *delimeter = strtok(name,".");
	char *extension;

	while(delimeter != NULL)
	{ 
		extension = delimeter;
		delimeter = strtok(NULL, " ");
	}

	if(strcmp(extension,"txt") == 0 || strcmp(extension,"html") == 0){
		send_to_client("Content Type: text/html\n", counter_index);
	}
	else{
		send_to_client("Content Type: image/gif\n", counter_index);
	}
}

//sends timestamp + server name + status code
void 
send_basic_info(int status, int counter_index)
{
	if(status == 200){
		send_to_client("HTTP/1.1 200 OK\n", counter_index);
	}
	else if(status == 404){
		send_to_client("HTTP/1.1 404 FILE NOT FOUND\n", counter_index);
		list_dir(counter_index);
	}
	time_t t = time(NULL);
	struct tm tm = *gmtime(&t);
	char timestamp[BUF_LEN];

	snprintf(timestamp,sizeof(timestamp),"Date: %d-%d-%d %d:%d:%d GMT\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	send_to_client(timestamp, counter_index);
	send_to_client("Server: myhttpd\n", counter_index);
}

int 
send_to_client(char t_buf[], int counter_index)
{
	int t_bytes;
	t_bytes = (strlen(t_buf) + 1) * sizeof(char);
	return send(readyQueue[counter_index].sd, t_buf, t_bytes, 0);
}

void 
send_file(char* filename, int counter_index) 
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
			int sent = send(readyQueue[counter_index].sd, &buff[c], val - c, 0);
			c += sent;
		}
	} 
	fclose(file);
} 

void
minute_wait(){
	int sec;
	if (q_time==0){
		sec = 61;
	}
	else{
		sec = q_time;
	}
	sleep(sec);
}

void
schedule_queue()
{
	minute_wait();
	
	if (sjf_policy){
		create_thread_pool(execute_SJF);
	}
	else {
		create_thread_pool(execute_FCFS);
	}
}

void
execute_FCFS()
{
	int request_ptr = 0;
	printf("executing FCFS\n");
	while(1)
	{
		if(readyQueue[request_ptr].scheduled == 1){
			int write_index = request_ptr;
			readyQueue[request_ptr].scheduled = 0;
			sem_wait(&semaphore);

			time_t t;
			time(&t);
			readyQueue[request_ptr].sched_time = strdup(ctime(&t));

			parse_buf((request_ptr++) % QUEUE_SIZE);
			write_log(write_index);

			sleep(1);
		}
		sem_post(&semaphore);	
	}
}

void
execute_SJF()
{
	while(1)
	{
		int request_ptr = get_sjf_index();
		if(request_ptr == 0 && readyQueue[0].scheduled == 0){
			/*this is checking if the if above is never being passed and 
			we are returning an index of 0 when we shouldnt be because that is
			what index is initialized to
			*/
			
			//do nothing 
		}
		else{
			readyQueue[request_ptr].scheduled = 0;
			sem_wait(&semaphore);
			
			time_t t;
			time(&t);
			readyQueue[request_ptr].sched_time = strdup(ctime(&t));

			parse_buf((request_ptr) % QUEUE_SIZE);

			write_log(request_ptr);

			sleep(1);
			sem_post(&semaphore);
		}
	}
}

int
get_sjf_index()
{	
	int i;
	int smallest_file = 2147483646;	//value of INT_MAX in c
	int index;

	for (i=0; i<QUEUE_SIZE; i++){
		if(readyQueue[i].fileSize < smallest_file && readyQueue[i].scheduled == 1){
			smallest_file = readyQueue[i].fileSize;
			index = i;
		}
	}
	return index;
}

void
list_dir(int counter_index)
{
	int n;
	struct dirent **contents_list;
	n = scandir(".", &contents_list, 0, alphasort);
	char directory[BUF_LEN];

	if (n < 0)
		 perror("\nscandir error\n");

	else {
		send_to_client("\nDirectory Listing:\n", counter_index);
		int i;
		char *period_char = ".";

		 for (i=0; i<n-1; i++){
			char *dir_element = contents_list[i]->d_name[0];

			if (strcmp(&dir_element, period_char) == 0) { // do not print files starting with '.'
				free(contents_list[i]);
				continue;
			}

			snprintf(directory,sizeof(directory)," - %s\n", contents_list[i]->d_name);
			send_to_client(directory, counter_index);

			free(contents_list[i]);
		}
		free(contents_list);
	}
}

void
get_myhttpd_dir()
{
	chdir("~");
	getcwd(myhttpd_dir, sizeof(myhttpd_dir));
}

void write_log(int index){
	struct qItem temp = readyQueue[index];
	temp.arrivalTime[strlen(temp.arrivalTime)-1] = 0;
	temp.sched_time[strlen(temp.sched_time)-1] = 0;
	temp.request[strlen(temp.request)-1] = 0;
	if(log_flag == 1){
		logging_file = fopen(log_file_name, "a");
		fprintf(logging_file, "%s - [%s -0600] [%s -0600] \"%s\" %s %d\n", temp.ip, temp.arrivalTime, temp.sched_time, temp.request, temp.status, temp.fileSize);
		fclose(logging_file);
	}
	if(d_flag == 1){
		fprintf(stdout, "%s - [%s -0600] [%s -0600] \"%s\" %s %d\n", temp.ip, temp.arrivalTime, temp.sched_time, temp.request, temp.status, temp.fileSize);
	}
}

int daemon_init() {
	pid_t pid;
	if ((pid=fork())<0) return (-1);
	else if (pid!=0) exit (0); //parent goes away 
	setsid(); //becomes session leader 
	chdir("/"); //cwd to root
	for (i = 0; i < 3; i++) close (i); 
	umask(0); //clear file creation mask
	return (0); 
}