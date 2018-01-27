#!/bin/bash

build: httpproxy bonus

httpproxy: 
	gcc -g -Wall httpproxy.c -o httpproxy
	
bonus:
	gcc -g -Wall bonus.c -lpthread -o bonus
	
run_httpproxy_checker:
	./proxy_tester.py httpproxy
	
run_bonus_checker:
	./proxy_tester.py bonus
	
clean:
	rm -rf httpproxy bonus
