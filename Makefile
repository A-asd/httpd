all: httpd

httpd: httpd.c
	gcc -W -Wall -o httpd httpd.c -lpthread
run:httpd
	./httpd
clean:
	rm httpd
