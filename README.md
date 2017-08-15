### Multi-threaded Web Server ###
* Version 1.0

### What is this repository for? ###

The objective of this project is to implement a multithreaded web server “myhttpd” in C/C++ on
a UNIX-based platform.

myhttpd is a simple web server. It binds to a given port on the given address and waits for
incoming HTTP/1.0 requests. It serves content from the given directory. That is, any requests for
documents is resolved relative to this directory (the document root – by default, the directory
where the server is running).


### How do I get set up? ###

Compile the program using the make command
SYNOPSIS: myhttpd [−d] [−h] [−l file] [−p port] [−r dir] [−t time] [−n threadnum] [−s sched]

−d : Enter debugging mode. That is, do not daemonize, only accept one connection at a time and enable logging to stdout. Without this option, the web server should run as a daemon process in the background.

−h : Print a usage summary with all options and exit.

−l file : Log all requests to the given file. See LOGGING for details.

−p port : Listen on the given port. If not provided, myhttpd will listen on port 8080.

−r dir : Set the root directory for the http server to dir.

−t time : Set the queuing time to time seconds. The default should be 60 seconds.

−n threadnum: Set number of threads waiting ready in the execution thread pool to threadnum.
The default should be 4 execution threads.

−s sched : Set the scheduling policy. It can be either FCFS or SJF. The default will be FCFS.

### Who do I talk to? ###

  Admins:
Ahmad M. Temoor - ahmadtem@buffalo.edu,
Mark Abidargham - markabid@buffalo.edu,
Syed Ali Hasan 	- syedhasa@buffalo.edu