#!/bin/bash

build: httpproxy bonus

httpproxy: 
	gcc -g -Wall httpproxy.c -o httpproxy
	
parallel_httpproxy:
	gcc -g -Wall parallel_httpproxy.c -lpthread -o parallel_httpproxy
	
clean:
	rm -rf httpproxy parallel_httpproxy
