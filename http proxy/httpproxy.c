#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

#define MAXLEN 500

typedef struct {
	char* request;
	char* hostname;
	int port;
	char* resource_path;
	int toCache;
} HTTPRequest;

/*
 * Folosesc aceasta structura pentru a lega un fisier temporar din cache de
 * cererea http care l-a generat.
 */
typedef struct {
	char* request_line;
	FILE* file;
} FileMap;

/*
 * Folosesc aceasta structura pentru a aloca dinamic memorie pentru vectorul
 * ce stocheaza fisierele din cache, astfel incat sa utilizez memoria in mod
 * eficient.
 */
typedef struct {
	FileMap* elements;
	int size;
	int capacity;
} FileMapSet;

FileMapSet* createFileMapSet(int initialCapacity) {

	FileMapSet* newSet = (FileMapSet*) malloc(sizeof(FileMapSet));

	/*
	 * Initial, multimea este goala.
	 */
	newSet->size = 0;
	newSet->capacity = initialCapacity;
	newSet->elements = (FileMap*) malloc(initialCapacity * sizeof(FileMap));

	return newSet;
}

int contains(FileMapSet* set, FileMap element) {
	int i;

	for(i = 0; i < set->size; i++) {
		if(!memcmp(&set->elements[i], &element, sizeof(element)))
			return i;
	}

	return -1;
}

void doubleIfFull(FileMapSet* set) {
	FileMap* p;
	if(set->size == set->capacity) {
		set->capacity = set->capacity * 2;
		p = (FileMap*) realloc(set->elements, set->capacity * sizeof(FileMap));
		if(p)
			set->elements = p;
	}
}

int add(FileMapSet* set, FileMap newElement) {
	if(contains(set, newElement) >= 0)
		return 0;
	doubleIfFull(set);
	set->elements[set->size] = newElement;
	set->size++;
	return 1;
}

void delete_last(FileMapSet* set) {
	set->size--;
}

void error(char *msg) {
	perror(msg);
	exit(1);
}

/**
 * Citeste maxim maxlen octeti din socket-ul sockfd. Intoarce
 * numarul de octeti cititi.
 * Functie preluata din scheletul de laborator.
 */
ssize_t Readline(int sockd, void *vptr, size_t maxlen) {
	ssize_t n, rc;
	char c, *buffer;

	buffer = vptr;

	for(n = 1; n < maxlen; n++) {
		if((rc = read(sockd, &c, 1)) == 1) {
			*buffer++ = c;
			if(c == '\n')
				break;
		}
		else if(rc == 0) {
			if(n == 1)
				return 0;
			else
				break;
		}
		else {
			if(errno == EINTR)
				continue;
			return -1;
		}
	}

	*buffer = 0;
	return n;
}

/*
 * O cerere HTTP trebuie sa contina mai intai de toate o comanda valida
 * (conform rfc 1945 aceste cereri sunt GET, POST, HEAD, PUT, DELETE, LINK, 
 * UNLINK). De asemenea, o cerere valida trebuie sa contina hostul si calea 	
 *  catre resursa. O cerere formata doar din linia METODA / HTTP/1.0 e
 * invalida, deoarece nu precizeaza hostul.
 */
int check_request(char* buffer) {
	/*
	 * Cererea nu specifica hostul.
	 */
	if(!strstr(buffer, "http://") && !strstr(buffer, "Host:"))
		return -1;

	/*
	 * Cererea nu specifica calea catre resursa. Daca pe prima linie avem calea
	 * absoluta, insa se specifica doar hostul, nu si calea catre resursa
	 * (adica cererea ar avea forma http://server ), atunci cererea este
	 * invalida.
	 */
	char* find_newline = strchr(buffer, '\n');
	int newline_position = find_newline - buffer;
	char* request_line = (char*) calloc(newline_position, sizeof(char));
	strncpy(request_line, buffer, newline_position);


	char* token =  strtok(request_line, " ");
	char* request_line_tokens[3];
	int index = 0;
	while(token != NULL) {
		request_line_tokens[index] = malloc(strlen(token) * sizeof(char));
		strcpy(request_line_tokens[index++], token);
		token = strtok(NULL, " ");
	}

	strcpy(request_line_tokens[1], request_line_tokens[1] + 7);
	if(strchr(request_line_tokens[1], '/') == NULL)
		return -1;



	/*
	 * Eroare de sintaxa la mentionarea versiunii de HTTP.
	 */
	if(strstr(buffer, "HTTP/1.0") == NULL)
		return -2;

	if(!strncmp(buffer, "GET", 3) || !strncmp(buffer, "POST", 4) ||
	        !strncmp(buffer, "PUT", 3) || !strncmp(buffer, "HEAD", 4) ||
	        !strncmp(buffer, "DELETE", 6) || !strncmp(buffer, "LINK", 4) ||
	        !strncmp(buffer, "UNLINK", 6))
		return 1;

	/*
	 * Utilizarea unei metode neimplementate de protocolul http.
	 */
	return 0;
}

void parse_request(char* buffer, HTTPRequest *request) {

	/*
	 * Fiecare rand dintr-o cerere HTTP are la final newline (\n).
	 * Din cererea HTTP, trebuie sa extrag mai multe informatii: hostul unde
	 * se afla resursa ceruta, calea catre resursa ceruta, portul pe care
	 * ar trebui sa ma conectez la host daca acesta este mentionat si daca
	 * raspunsul poate fi stocat in cache sau nu.
	 * Intr-o cerere HTTP, prima linie specifica mereu metoda, calea către
	 * resursă și versiunea de HTTP.
	 */
	char* find_newline = strchr(buffer, '\n');
	/*
	 * Aici, extrag prima linie din cadrul cererii, pentru a o parsa mai
	 * departe. Prima linie este cuprinsa intre inceputul cererii (buffer-ului)
	 * si primul newline.
	 */
	int newline_position = find_newline - buffer;
	char* request_line = (char*) calloc(newline_position, sizeof(char));
	strncpy(request_line, buffer, newline_position);
	printf("newline_position %d\n", newline_position);
	printf("request_line %s\n", request_line);

	request->request = (char*)calloc(strlen(request_line), sizeof(char));
	strcpy(request->request, request_line);
	/*
	 * Impart prima linie in tokeni, pentru a extrage hostul este reprezentat
	 * de al doilea token.
	 */
	char* token =  strtok(request_line, " ");
	char* request_line_tokens[3];
	int index = 0;
	while(token != NULL) {
		request_line_tokens[index] = malloc(strlen(token) * sizeof(char));
		strcpy(request_line_tokens[index++], token);
		token = strtok(NULL, " ");
	}

	/*
	 * O cale absoluta incepe mereu cu http://.
	 */
	if(strstr(request_line_tokens[1], "http://")) {
		/*
		 * O cale absoluta este de forma http://host:port/cale_catre_resursa.
		 * In continuare, extrag hostul, eliminand din cale primele 7 caractere
		 * reprezentate de http:// si apoi caracterele incepand cu primul / de
		 * dupa host.
		 */
		char* auxiliary = strdup(request_line_tokens[1]);
		strcpy(auxiliary, auxiliary + 7);
		char* auxiliary_position = strchr(auxiliary, '/');
		int separator_position = auxiliary_position - auxiliary;
		char* hostname_aux = strdup(auxiliary);
		/*
		 * In cazul in care in URL se specifica un port, acesta va fi separat
		 * de host prin :. In cazul in care nu se specifica vreun port,
		 * cel implicit este 80.
		 */
		char* port = strchr(hostname_aux, ':');
		char* hostname;

		if(port) {
			int hostname_separator = port - hostname_aux;
			hostname = (char*) calloc(hostname_separator, sizeof(char));
			strncpy(hostname, hostname_aux, hostname_separator);
			/*
			 * port contine :port, deci vreau sa elimin : din acest sir de
			 * caractere.
			 */
			strcpy(port, port + 1);
			request->port = atoi(port);
		}
		else {
			request->port = 80;
			hostname = strdup(hostname_aux);
			strcpy(hostname + separator_position, hostname + strlen(hostname));
		}
		strcpy(auxiliary, auxiliary + separator_position);
		request->hostname = calloc(strlen(hostname), sizeof(char));
		strcpy(request->hostname, hostname);
		request->resource_path = calloc(strlen(auxiliary), sizeof(char));
		strcpy(request->resource_path, auxiliary);
		printf("cale absoluta\n");
	}
	else {
		/*
		 * O cale relativa contine pe prima linie calea catre resursa, iar pe
		 * a doua hostul.
		 */
		char* host_auxiliary = strstr(buffer, "Host");
		char* find_newline2 = strchr(host_auxiliary, '\n');
		int newline_position2 = find_newline2 - host_auxiliary;
		char* host_line = (char*) calloc(newline_position2, sizeof(char));
		strncpy(host_line, host_auxiliary, newline_position2);
		char* find_space = strchr(host_line, ' ');
		int space_position = find_space - host_line;
		strcpy(host_line, host_line + space_position + 1);
		request->hostname = calloc(strlen(host_line), sizeof(char));
		strcpy(request->hostname, host_line);
		request->resource_path = calloc(strlen(request_line_tokens[1]),
		 sizeof(char));
		strcpy(request->resource_path, request_line_tokens[1]);
		printf("cale relativa\n");
	}

	/*
	 * Conform rfc, "If the request is GET or HEAD and a prior response is
	 *cached, the proxy may use the cached message if it passes any
	 *restrictions in the Expires header field.", doar raspunsurile de la
	 *cererile GET si HEAD pot fi pastrate in cache, atata timp cat nu exista
	 *alte restrictii, precum Pragma: no-cache. "When the "no-cache" directive
	 *is present in a request message, an application should forward the
	 *request toward the origin server even if it has a cached copy of what is
	 *being requested."
	 */

	if((strstr(buffer, "GET") || strstr(buffer, "HEAD")) &&
	        strstr(buffer, "Pragma: no-cache") == NULL) {
		request->toCache = 1;
		printf("can cache\n");
	}
	else
		request->toCache = 0;

}

int search_cache(FileMapSet* set, char* request) {
	int i;

	for(i = 0; i < set->size; i++) {
		if(!strcmp(set->elements[i].request_line, request))
			return i;
	}

	return -1;
}

int main(int argc, char *argv[]) {
	int listening_socket, client_socket, port_number, received, i;
	unsigned int client_address_length;
	FileMapSet* set = createFileMapSet(1);

	struct sockaddr_in server_address, client_address;
	char buffer[MAXLEN];

	fd_set read_fds;	//multimea de citire folosita in select()
	fd_set tmp_fds;	//multime folosita temporar
	int fdmax;		//valoare maxima file descriptor din multimea read_fds

	if(argc < 2) {
		fprintf(stderr, "Usage : %s <port_proxy> \n", argv[0]);
		exit(1);
	}

	//golim multimea de descriptori de citire (read_fds) si multimea tmp_fds
	FD_ZERO(&read_fds);
	FD_ZERO(&tmp_fds);

	listening_socket = socket(AF_INET, SOCK_STREAM, 0);
	if(listening_socket < 0)
		error("-10 : Eroare la apel socket\n");

	memset((char *) &server_address, 0, sizeof(server_address));
	server_address.sin_family = AF_INET;
	server_address.sin_addr.s_addr = INADDR_ANY;
	port_number = atoi(argv[1]);
	server_address.sin_port = htons(port_number);

	if(bind(listening_socket, (struct sockaddr *) &server_address,
	        sizeof(struct sockaddr)) < 0)
		error("-10 : Eroare la apel bind\n");

	if(listen(listening_socket, 5) == -1)
		error("-10 : Eroare la apel listen\n");

	//adaugam noul file descriptor (socketul pe care se asculta conexiuni) in multimea read_fds
	FD_SET(listening_socket, &read_fds);
	//adaugam file descriptorul specific stdin (pentru citirea de la tastatura)
	FD_SET(0, &read_fds);
	fdmax = listening_socket;

	// main loop
	while(1) {
		tmp_fds = read_fds;

		if(select(fdmax + 1, &tmp_fds, NULL, NULL, NULL) == -1)
			error("-10 : Eroare la apel select\n");


		for(i = 0; i <= fdmax; i++) {
			if(FD_ISSET(i, &tmp_fds)) {
				if(i == listening_socket) {
					// a venit ceva pe socketul inactiv(cel cu listen) = o noua conexiune
					// actiunea serverului: accept()
					client_address_length = sizeof(client_address);
					if((client_socket = accept(listening_socket, (struct sockaddr *)&client_address,
					                           &client_address_length)) == -1)
						error("-10 : Eroare la apel accept\n");
					else {
						//adaug noul socket intors de accept() la multimea descriptorilor de citire
						FD_SET(client_socket, &read_fds);
						if(client_socket > fdmax)
							fdmax = client_socket;
					}
					printf("Noua conexiune de la %s, port %d, socket_client %d\n ",
					       inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port),
					       client_socket);
				}
				else {
					// am primit date pe unul din socketii cu care vorbesc cu clientii
					//actiunea serverului: recv()
					memset(buffer, 0, MAXLEN);
					if((received = recv(i, buffer, sizeof(buffer), 0)) <= 0) {
						if(received == 0) {
							//conexiunea s-a inchis neasteptat din cauza unei posibile erori
							printf("selectserver: socket %d hung up\n", i);
						}
						else
							error("-10 : Eroare la apel recv\n");
						close(i);
						FD_CLR(i, &read_fds); // scoatem din multimea de citire socketul pe care
					}
					else { //recv intoarce > 0
						printf("Am primit de la clientul de pe socketul %d, mesajul: %s\n", i, buffer);
						HTTPRequest request;
						memset(&request, 0, sizeof(request));
						int is_valid_request = check_request(buffer);

						switch(is_valid_request) {
						case 0:
							send(i, "HTTP/1.0 501 Not Implemented\n",
							     strlen("HTTP/1.0 501 Not Implemented\n"), 0);
							close(i);
							FD_CLR(i, &read_fds);
							break;
						case -1:
							send(i, "HTTP/1.0 400 Bad request\n", strlen("HTTP/1.0 400 Bad request\n"), 0);
							close(i);
							FD_CLR(i, &read_fds);
							break;
						case -2:
							send(i, "HTTP/1.0 505 HTTP Version Not Supported\n",
							     strlen("HTTP/1.0 505 HTTP Version Not Supported\n"), 0);
							close(i);
							FD_CLR(i, &read_fds);
							break;
						case 1:

							parse_request(buffer, &request);

							/*
							 * Mai intai, verificam daca in trecut am mai primit
							 * aceeasi cerere si a fost permisa stocarea
							 * raspunsului in cache. Daca cererea nu permite
							 * stocarea raspunsului in cache, atunci nu are rost
							 * sa caut in el.
							 */
							int cached = -1;
							if(request.toCache)
								cached = search_cache(set, request.request);
							int can_cache_answer = 0;
							int read_from;
							char recvbuf[MAXLEN];
							/*
							 * Daca am gasit in cache cererea, atunci vom
							 * trimite la client, continutul fisierului asociat
							 * cererii.
							 */
							if(cached >= 0 && request.toCache) {
								//read_from = fileno();
								rewind(set->elements[cached].file);
								while(!feof(set->elements[cached].file)) {
									if(fread(recvbuf, 1, sizeof(recvbuf), set->elements[cached].file) == 0) break;
									send(i, recvbuf, strlen(recvbuf), 0);
								}
								printf("IN CACHE\n");
							}
							else {
								struct hostent *host = gethostbyname(request.hostname);

								if(!host) {
									printf("Invalid host! Check again!\n");
									close(i);
									FD_CLR(i, &read_fds);
									continue;
								}

								struct in_addr **addr_list = (struct in_addr **) host->h_addr_list;
								char* p = strdup(inet_ntoa(*addr_list[0]));

								struct sockaddr_in servaddr;
								int server_socket;

								if((server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
									printf("Eroare la  creare socket.\n");
									close(server_socket);
									close(i);
									FD_CLR(i, &read_fds);
									continue;
								}

								/* formarea adresei serverului */
								memset(& servaddr, 0, sizeof(servaddr));
								servaddr.sin_family = AF_INET;
								servaddr.sin_port = htons(request.port);

								if(inet_aton(p, &servaddr.sin_addr) <= 0) {
									printf("Adresa IP invalida.\n");
									close(server_socket);
									close(i);
									FD_CLR(i, &read_fds);
									continue;
								}

								/*  conectare la server  */
								if(connect(server_socket, (struct sockaddr *) & servaddr,
								           sizeof(servaddr)) < 0) {
									printf("Eroare la conectare\n");
									close(server_socket);
									close(i);
									FD_CLR(i, &read_fds);
									continue;
								}

								printf("Trimit: %s", buffer);
								sprintf(buffer, "%s\n", buffer);
								send(server_socket, buffer, strlen(buffer), 0);
								read_from = server_socket;

								printf("NOT IN CACHE\n");
								int nbytes = Readline(read_from, recvbuf, MAXLEN - 1);

								recvbuf[nbytes] = 0;
								FileMap new_file_map;
								/*
								 * In enuntul temei se specifica faptul ca se
								 * introduc in cache doar raspunsurile cu codul
								 * 200 OK.
								 */
								if(strstr(recvbuf, "200 OK")) {
									new_file_map.request_line = calloc(strlen(request.request),
									                                   sizeof(char));
									strcpy(new_file_map.request_line, request.request);
									new_file_map.file = tmpfile();

									add(set, new_file_map);
									fwrite(recvbuf, 1, sizeof(recvbuf), set->elements[set->size - 1].file);
									can_cache_answer = 1;
								}

								printf("Am primit: %s", recvbuf);
								send(i, recvbuf, strlen(recvbuf), 0);
								memset(recvbuf, 0, MAXLEN);
								while((nbytes = Readline(read_from, recvbuf, MAXLEN - 1)) > 0) {

									printf("%s", recvbuf);
									/*
									 * In enunt se specifica faptul ca daca
									 * in raspuns se gasesc campurile Pragma:
									 * no-cache sau Cache-control: private,
									 * atunci raspunsul primit de la server nu
									 * va fi stocat in cache, ci doar va fi
									 * transmis la client.
									 */
									if(strstr(recvbuf, "Pragma: no-cache") ||
									        strstr(recvbuf, "Cache-control: private"))
										can_cache_answer = 0;

									if(can_cache_answer)
										fwrite(recvbuf, 1, sizeof(recvbuf), set->elements[set->size - 1].file);
									send(i, recvbuf, sizeof(recvbuf), 0);
									memset(recvbuf, 0, MAXLEN);

								}
								if(!can_cache_answer)
									delete_last(set);
								close(server_socket);
								close(read_from);
							}

							break;
						}
						close(i);
						FD_CLR(i, &read_fds);


					}
				}
			}
		}

	}
}
