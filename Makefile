all: myhttpd.c 
	cc -O myhttpd.c -o myhttpd -lpthread

clean: 
	$(RM) myhttpd