/** \file flute.c \brief FLUTE sender and receiver
 *
 *  $Author: peltotal $ $Date: 2007/02/28 08:58:01 $ $Revision: 1.83 $
 *
 *  MAD-FLUTELIB: Implementation of FLUTE protocol.
 *  Copyright (c) 2003-2007 TUT - Tampere University of Technology
 *  main authors/contacts: jani.peltotalo@tut.fi and sami.peltotalo@tut.fi
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  In addition, as a special exception, TUT - Tampere University of Technology
 *  gives permission to link the code of this program with the OpenSSL library (or
 *  with modified versions of OpenSSL that use the same license as OpenSSL), and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify this file, you may extend this exception to your version
 *  of the file, but you are not obligated to do so. If you do not wish to do so,
 *  delete this exception statement from your version.
*/

#include <stdlib.h>
#include <assert.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#include <string.h>
#include <stdio.h>

#ifdef _MSC_VER
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
#include <io.h>
#include <direct.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

#include "../alclib/alc_session.h"
#include "../alclib/alc_tx.h"
#include "../alclib/alc_rx.h"

#include "flute.h"
#include "fdt_gen.h"
#include "receiver.h"
#include "sender.h"
#include "uri.h"
#include "padding_encoding.h"
#include "mad_zlib.h"
#include "mad_md5.h"
#include "display.h"
#include "stsid.h"

#ifdef USE_FILE_REPAIR
#include "http_file_repair.h"
#include "flute_file_repair.h"
#include "apd.h"
#include "env.h"

#endif

/**
* This is a private function which names all uncompleted objects.
*
* @param receiver pointer to structure containing receiver information
*
* @return 0 in success, -1 in error cases
*
*/

int name_incomplete_objects(flute_receiver_t *receiver) {

	trans_obj_t *to = NULL;
	file_t *file = NULL;
	file_t *next_file = NULL;
	uri_t *uri = NULL;
	char *filepath = NULL;
	char *tmp = NULL;
	char* session_basedir = NULL;
	char *ptr;
	int point; 
	int ch = '/';
	int i;
	char fullpath[MAX_PATH_LENGTH];
	char filename[MAX_PATH_LENGTH];
	int retval;
	char *pad_tmp_filename = NULL;
	char *tmp_filename = NULL;

	session_basedir = get_session_basedir(receiver->s_id);
	to = get_session_obj_list(receiver->s_id);

	while(to != NULL) {
		next_file = receiver->fdt->file_list;
		while(next_file != NULL) {
			file = next_file;

			if(file->toi == to->toi) {
				break;
			}

			next_file = file->next;
		}

		if(file->encoding != NULL && strcmp(file->encoding, "gzip") == 0) {
			printf("Cannot rename content encoded incomplete object.\n");
			to = to->next;
			continue;
		}

		uri = parse_uri(file->location, strlen(file->location));
		filepath = get_uri_host_and_path(uri);

		if(!(tmp = (char*)calloc((strlen(filepath) + 1), sizeof(char)))) {
			printf("Could not alloc memory for tmp-filepath!\n");
			fflush(stdout);

			free(filepath);
			free_uri(uri);
			return -1;
		}

		memcpy(tmp, filepath, strlen(filepath));
		ptr = strchr(tmp, ch);

		memset(fullpath, 0, MAX_PATH_LENGTH);
		memcpy(fullpath, session_basedir, strlen(session_basedir));

		i = 0;

		if(ptr != NULL) {
			while(ptr != NULL) {
				i++;
				point = (int)(ptr - tmp);

				memset(filename, 0, MAX_PATH_LENGTH);		
#ifdef _MSC_VER
				memcpy((fullpath + strlen(fullpath)), "\\", 1);
#else
				memcpy((fullpath + strlen(fullpath)), "/", 1);
#endif
				memcpy((fullpath + strlen(fullpath)), tmp, point);

				memcpy(filename, (tmp + point + 1), (strlen(tmp) - (point + 1)));		
#ifdef _MSC_VER
				if(mkdir(fullpath) < 0) {					
#else		
				if(mkdir(fullpath, S_IRWXU) < 0) {
#endif
					if(errno != EEXIST) {
						printf("mkdir failed: cannot create directory %s (errno=%i)\n", fullpath, errno);
						fflush(stdout);

						free(tmp);
						free(filepath);
						free_uri(uri);
						return -1;
					}
				}

				strcpy(tmp, filename);
				ptr = strchr(tmp, ch);
			}
#ifdef _MSC_VER
			memcpy((fullpath + strlen(fullpath)), "\\", 1);
#else
			memcpy((fullpath + strlen(fullpath)), "/", 1);
#endif
			memcpy((fullpath + strlen(fullpath)), filename, strlen(filename));
		}
		else{
#ifdef _MSC_VER
			memcpy((fullpath + strlen(fullpath)), "\\", 1);
#else
			memcpy((fullpath + strlen(fullpath)), "/", 1);
#endif
			memcpy((fullpath + strlen(fullpath)), filepath, strlen(filepath));
		}

		if(!(tmp_filename = (char*)calloc((strlen(to->tmp_filename) + 1), sizeof(char)))) {
			printf("Could not alloc memory for tmp_filename!\n");

			free(tmp);
			free(filepath);
			free_uri(uri);
			return -1;    
		}

		if(file->encoding == NULL) {
			memcpy(tmp_filename, to->tmp_filename, strlen(to->tmp_filename));
			free_object(to, get_alc_session(receiver->s_id), 1);
		}
		else if(strcmp(file->encoding, "pad") == 0) {

			if(!(pad_tmp_filename = (char*)calloc((strlen(to->tmp_filename) + 1), sizeof(char)))) {
				printf("Could not alloc memory for pad_tmp_filename!\n");

				free(tmp);
				free(filepath);
				free_uri(uri);
				return -1;    
			}

			memcpy(pad_tmp_filename, to->tmp_filename, strlen(to->tmp_filename));

			ptr = strstr(to->tmp_filename, PAD_SUFFIX);
			memcpy(tmp_filename, to->tmp_filename, (ptr - to->tmp_filename));

			free_object(to, get_alc_session(receiver->s_id), 1);

			retval = padding_decoder(pad_tmp_filename, (int)file->content_len);

			if(retval == -1) {
				free(tmp_filename);
				free(pad_tmp_filename);	
				free(tmp);
				free(filepath);
				free_uri(uri);
				return -1;             
			}

			free(pad_tmp_filename);
		}	

		if(rename(tmp_filename, fullpath) < 0) {

			if(errno == EEXIST) {
				retval = remove(fullpath);

				if(retval == -1) {    
					printf("errno: %i\n", errno);
					fflush(stdout);
				}

				if(rename(tmp_filename, fullpath) < 0) {
					printf("rename() error1: %s\n", tmp_filename);
					fflush(stdout);
				}
			}
			else {
				printf("rename() error2: %s\n", tmp_filename);
				fflush(stdout);
			}
		}

		free(tmp);
		free(filepath);
		free_uri(uri);
		free(tmp_filename);

		to = get_alc_session(receiver->s_id)->obj_list;
	}

	return 0;
}

/**
* This is a private function which adds missing bytes information with receiver reporting.
*
* @param report pointer to structure containing receiver information
* @param first first missing byte
* @param last last missing byte
*
*/

void add_missing_block(flute_receiver_report_t *report, unsigned long long first, 
                       unsigned long long last) {
    missing_block_t *block = malloc(sizeof(missing_block_t));
    block->first = first;
    block->last = last;
    block->next = report->mb_list;

    report->mb_list = block;
}

/**
* This is a private function which copies file name with receiver reporting.
*
* @param file_name name of the file
* @param r pointer to structure containing receiver information
* @param toi transport object identifier
* 
*/

void copy_file_name(char **file_name, flute_receiver_t *r, unsigned long long toi) {
    file_t *file;

    file = find_file_with_toi(r->fdt, toi);

    *file_name = malloc(strlen(file->location)+1);
    strcpy(*file_name, file->location);
}

/**
* This is a private function which copies MD5 with receiver reporting.
*
* @param md5 MD5 of the file 
* @param r pointer to structure containing receiver information
* @param toi transport object identifier
* 
*/

void copy_md5(char **md5, flute_receiver_t *r, unsigned long long toi) {
    file_t *file;

    file = find_file_with_toi(r->fdt, toi);

    *md5 = malloc(strlen(file->md5)+1);
    strcpy(*md5, file->md5);
}


/**
* This is a private function which builds the receiver report.
*
* @param a arguments structure where command line arguments are parsed
* @param r pointer to structure containing receiver information 
* @param report stores receiver report.
* 
*/

void build_report(arguments_t *a, flute_receiver_t *r, flute_receiver_report_t **report) {
    trans_obj_t *obj_list;
    flute_receiver_report_t *report_row;
	unsigned long long position ;
    unsigned long long to_data_left;
    trans_block_t *tb;
    unsigned int i;

    if(report == NULL) {
        return;
    }

    assert(a != NULL);
    assert(r != NULL);

    *report = NULL;
    obj_list = get_session_obj_list(r->s_id);
    while(obj_list != NULL) {
        if(!object_completed(obj_list)) {
            // Not completed file found
            position = 0;
            to_data_left = obj_list->len;
            tb = obj_list->block_list;

            report_row = malloc(sizeof(flute_receiver_report_t));
            copy_file_name(&(report_row->file_name), r, obj_list->toi);
            copy_md5(&(report_row->md5), r, obj_list->toi);
            report_row->mb_list = NULL;
            report_row->next = *report;
            *report = report_row;

            if(a->alc_a.verbosity >= 4) {
                printf("%s received incomplete\n", report_row->file_name);
            }

            for(i = 0; i < obj_list->bs->N; ++i) {

                // This is the len of a generic undecoded block
                unsigned long long block_len = obj_list->es_len*tb->k;

                // This is the len of the next undecoded block
                unsigned long long len = to_data_left < block_len ? to_data_left : block_len;

                if(!block_ready_to_decode(tb)) {
                    // missed bytes from position to position+len;
                    add_missing_block(report_row, position, position+len);
                    if(a->alc_a.verbosity >= 4) {
#ifdef _MSC_VER
                        printf("Missing bytes from %I64x to %I64x\n", position, position+len);
#else
                        printf("Missing bytes from %llu to %llu\n", position, position+len);
#endif
                    }
                }

                assert(0 <= position);
                assert(position < obj_list->len+1);
                assert(len <= (obj_list->len-position));

                position += len;
                to_data_left -= len;

                assert(to_data_left >= 0);

                tb = obj_list->block_list+(i+1);
            }
        }

        obj_list = obj_list->next;
    }
}


/**
* This function returns a random number between zero and max.
*
* @param max maximum number for random number
*
* @return random number between zero and max
*
*/

#ifdef USE_FILE_REPAIR
unsigned int random_number(int max) {
	return (unsigned int)(rand()%max);
}
#endif

int flute_sender(arguments_t *a, int *s_id, unsigned long long *session_size) {

	unsigned short i;
	int j, n;
	int retval = 0;
	int retcode = 0;

	struct sockaddr_in ipv4;
	struct sockaddr_in6 ipv6;

	char addrs[MAX_CHANNELS_IN_SESSION][INET6_ADDRSTRLEN];  /* Mcast addresses on which to send */
	char ports[MAX_CHANNELS_IN_SESSION][MAX_PORT_LENGTH];	   /* Local port numbers  */

	char tmp[5];

	time_t systime;

	unsigned long long curr_time;

#ifdef USE_FILE_REPAIR
	char flute_fdt_file[MAX_PATH_LENGTH];
	char fullpath[MAX_PATH_LENGTH];
	FILE *fp;
#endif

	char *sdp_buf = NULL;
	FILE *sdp_fp;
	struct stat sdp_file_stats;
	int nbytes;

#ifdef _MSC_VER
	HANDLE handle_sender_file_table_output_thread;
	unsigned int sender_file_table_output_thread_id;
	int addr_size;
#else
	pthread_t sender_file_table_output_thread_id;
	int join_retval;
#endif

	flute_sender_t sender;

	*session_size = 0;

	if(strcmp(a->sdp_file, "") != 0) {

		if(stat(a->sdp_file, &sdp_file_stats) == -1) {
			printf("Error: %s is not valid file name\n", a->sdp_file);
			fflush(stdout);
			memset(a->sdp_file, 0, MAX_PATH_LENGTH);
			return -1;
		}

		/* Allocate memory for buf */
		if(!(sdp_buf = (char*)calloc((sdp_file_stats.st_size + 1), sizeof(char)))) {
			printf("Could not alloc memory for sdp buffer!\n");
			fflush(stdout);
			return -1;
		}

		if((sdp_fp = fopen(a->sdp_file, "rb")) == NULL) {
			printf("Error: unable to open sdp_file %s\n", a->sdp_file);
			fflush(stdout);
			free(sdp_buf);
			return -1;
		}

		nbytes = fread(sdp_buf, 1, sdp_file_stats.st_size, sdp_fp); 

		if(parse_sdp_file(a, addrs, ports, sdp_buf) == -1) {
			free(sdp_buf);
			return -1;
		}

		free(sdp_buf);
	}
	else {
		if(a->alc_a.addr_family == PF_INET) {

			for(j = 0; j < a->alc_a.nb_channel; j++) {
				memset(addrs[j], 0, INET6_ADDRSTRLEN);
				ipv4.sin_addr.s_addr = htonl(ntohl(inet_addr(a->alc_a.addr)) + j);
				sprintf(addrs[j], "%s", inet_ntoa(ipv4.sin_addr));

				memset(ports[j], 0, MAX_PORT_LENGTH);
				sprintf(ports[j], "%i", (atoi(a->alc_a.port) + j));
			}
		}
		else if(a->alc_a.addr_family == PF_INET6) {

#ifdef _MSC_VER
			addr_size = sizeof(struct sockaddr_in6);
			WSAStringToAddress((char*)a->alc_a.addr, AF_INET6, NULL, (struct sockaddr*)&ipv6, &addr_size);
#else 
			inet_pton(AF_INET6, a->alc_a.addr, &ipv6.sin6_addr);
#endif

			for(j = 0; j < a->alc_a.nb_channel; j++) {

				memset(addrs[j], 0, INET6_ADDRSTRLEN);

#ifdef _MSC_VER
				addr_size = sizeof(addrs[j]);
				WSAAddressToString((struct sockaddr*)&ipv6, sizeof(struct sockaddr_in6),
					NULL, addrs[j], &addr_size);
#else
				inet_ntop(AF_INET6, &ipv6.sin6_addr, addrs[j], sizeof(addrs[j]));
#endif

				memset(ports[j], 0, MAX_PORT_LENGTH);
				sprintf(ports[j], "%i", (atoi(a->alc_a.port) + j));

				if(j < (a->alc_a.nb_channel - 1)) {
					if(increase_ipv6_address(&ipv6.sin6_addr) == -1) {
						printf("Increasing IPv6 address %s is not possible\n", addrs[j]);
						return -1;
					}
				}
			}
		}
	}

	if(a->alc_a.stop_time != 0) {
		time(&systime);
		curr_time = systime + 2208988800U;

		if(a->alc_a.stop_time <= curr_time) {
			printf("Session end time reached\n");
			fflush(stdout);
			return -3;
		}
	}

	*s_id = open_alc_session(&a->alc_a);

	if(*s_id < 0) {
		printf("Error opening ALC session\n");
		fflush(stdout);
		return -1;
	}

	for(i = 0; (int)i < a->alc_a.nb_channel; i++) {

		if(a->alc_a.addr_type == 1) {
			retval = add_alc_channel(*s_id, a->alc_a.tsi, ports[i], addrs[0], a->alc_a.intface, a->alc_a.intface_name);
		}
		else {
			retval = add_alc_channel(*s_id, a->alc_a.tsi, ports[i], addrs[i], a->alc_a.intface, a->alc_a.intface_name);
		}

		if(retval == -1) {    
			close_alc_session(*s_id);
			return -1;
		}	
	}

	/* Generate fdt file first */
	if(strcmp(a->fdt_file, "") == 0) {

		memset(a->fdt_file, 0, MAX_PATH_LENGTH);
		strcpy(a->fdt_file, "fdt_tsi");

		memset(tmp, 0, 5);

#ifdef _MSC_VER
		sprintf(tmp, "%I64u", a->alc_a.tsi);
#else
		sprintf(tmp, "%llu", a->alc_a.tsi);
#endif

		strcat(a->fdt_file, tmp);
		strcat(a->fdt_file, ".xml");

		retcode = generate_fdt(a->file_path, a->alc_a.base_dir, s_id, a->fdt_file, a->complete_fdt,
			a->alc_a.verbosity);

		if(retcode < 0) {
			close_alc_session(*s_id);
			return -1;
		}
	}

#ifdef USE_FILE_REPAIR
	if(strcmp(a->repair, "") != 0) {

		if((fp = fopen(a->repair, "wb")) == NULL) {
			close_alc_session(*s_id);
			return -1;
		}

		memset(fullpath, 0, MAX_PATH_LENGTH);
		memset(flute_fdt_file, 0, MAX_PATH_LENGTH);

		if(getcwd(fullpath, MAX_PATH_LENGTH) != NULL) {

		  if(strcmp(a->alc_a.base_dir, "") == 0) {
		    fprintf(fp, "BaseDir=%s\n", fullpath);
		  }
		  else {
		    fprintf(fp, "BaseDir=%s\n", a->alc_a.base_dir);
		  }
		  
		  if(((a->alc_a.fec_enc_id == SB_SYS_FEC_ENC_ID) && (a->alc_a.fec_inst_id == REED_SOL_FEC_INST_ID))) {
		    fprintf(fp, "FECRatio=%i\n", a->alc_a.fec_ratio);
		  }
		  

		  if(strcmp(a->file_path, "") != 0) {
		    
		    memcpy(flute_fdt_file, fullpath, strlen(fullpath));
#ifdef _MSC_VER
		    strcat(flute_fdt_file, "\\");
#else
		    strcat(flute_fdt_file, "/");
#endif
		    strcat(flute_fdt_file, a->fdt_file);
		  }
		  else {
		    strcat(flute_fdt_file, a->fdt_file);
		  }
		  fprintf(fp, "FDTFile=%s\n", flute_fdt_file);
		}
		
		fclose(fp);
	}
#endif

	sender.fdt = NULL;
	sender.s_id = *s_id;

	/* Create Display thread */

	if(a->file_table_output == TRUE) {

#ifdef _MSC_VER
		handle_sender_file_table_output_thread =
			(HANDLE)_beginthreadex(NULL, 0, (void*)sender_file_table_output_thread,
			(void*)&sender, 0, &sender_file_table_output_thread_id);

		if(handle_sender_file_table_output_thread == NULL) {
			perror("flute_sender(): _beginthread");
			close_alc_session(*s_id);
			return -1;
		}

#else
		if(pthread_create(&sender_file_table_output_thread_id, NULL, sender_file_table_output_thread,
			(void*)&sender) != 0) {
				perror("flute_sender(): pthread_create");
				close_alc_session(*s_id);
				return -1;
		}


#endif
	}

	/***** FDT based send *****/

	retval = sender_in_fdt_based_mode(a, &sender);

	/* If A flag packets must not be included into the session size, use this */
	*session_size = get_session_sent_bytes(*s_id);

	if(a->send_session_close_packets == 1) {

		if(retval != -1) {

			if(a->alc_a.verbosity > 0) {
				printf("Sending session close packets\n");
				fflush(stdout);
			}

			/* Let's send three session close packets for the base channel */

			for(n = 0; n < 3; n++) {
				retcode = send_session_close_packet(*s_id);
				if(retcode == -1) {
					break;
				}
			}
		}
	}

	set_session_state(*s_id, SExiting);

	if(((a->alc_a.cc_id == RLC) || ((a->alc_a.cc_id == Null) && (a->alc_a.nb_channel != 1)))) {
#ifdef _MSC_VER
		WaitForSingleObject(get_alc_session(*s_id)->handle_tx_thread, INFINITE);
		CloseHandle(get_alc_session(*s_id)->handle_tx_thread);
#else
		join_retval = pthread_join(get_alc_session(*s_id)->tx_thread_id, NULL);
		assert(join_retval == 0);
		pthread_detach(get_alc_session(*s_id)->tx_thread_id);
#endif
	}

	if(a->file_table_output == TRUE) {
#ifdef _MSC_VER
		WaitForSingleObject(handle_sender_file_table_output_thread, INFINITE);
		CloseHandle(handle_sender_file_table_output_thread);
#else
		join_retval = pthread_join(sender_file_table_output_thread_id, NULL);
		assert(join_retval == 0);
		pthread_detach(sender_file_table_output_thread_id);
#endif
	}

	/* If A flag packets have to be included into the session size, use this */
	/**session_size = get_session_sent_bytes(*s_id);*/

	if(sender.fdt != NULL) {
		FreeFDT(sender.fdt);
	}

	close_alc_session(*s_id);

	return retval;
}

int flute_receiver(arguments_t *a, int *s_id) {
    return flute_receiver_report(a, s_id, NULL);
}

int flute_receiver_report(arguments_t *a, int *s_id, flute_receiver_report_t **report) {

	unsigned short i = 0;
	int j;
	int retval = 0;

	char wildcard_token[MAX_PATH_LENGTH];
	char *file_uri = NULL;
	int file_nb = 0;

	struct sockaddr_in ipv4;
	struct sockaddr_in6 ipv6;

	char addrs[MAX_CHANNELS_IN_SESSION][INET6_ADDRSTRLEN];  /* Mcast addresses */
	char ports[MAX_CHANNELS_IN_SESSION][MAX_PORT_LENGTH];         /* Local port numbers  */

	time_t systime;
	BOOL is_printed = FALSE;

	unsigned long long curr_time;

#ifdef _MSC_VER
	HANDLE handle_fdt_thread;
	unsigned int fdt_thread_id;
	//HANDLE handle_receiver_file_table_output_thread;
	//unsigned int receiver_file_table_output_thread_id;
	//HANDLE handle_lct_thread;
	//unsigned int lct_thread_id;
	int addr_size;
#else
	pthread_t fdt_thread_id;
	pthread_t receiver_file_table_output_thread_id;
	pthread_t lct_thread_id;
	int join_retval;
#endif

	char *sdp_buf = NULL;
	FILE *sdp_fp;
	struct stat sdp_file_stats;
	int nbytes;

	flute_receiver_t receiver;

	env_t* env;
	char* env_buf = NULL;
	FILE* env_fp;
	struct stat env_file_stats;
	int env_nbytes;

	stsid_t* stsid;
	char* stsid_buf = NULL;
	FILE* stsid_fp;
	struct stat stsid_file_stats;
	int stsid_nbytes;

#ifdef USE_FILE_REPAIR
	int n;
	char *file_repair_sdp_buf = NULL;
	serviceURI_t *tmp_serviceURI;
	int waitTime;
	apd_t *apd;
	unsigned int nb_of_http_servers;
	unsigned int http_server_index;
	int repairing_needed = 0;

	file_t *file = NULL;
	char *apd_buf = NULL;
	FILE *apd_fp;
	struct stat apd_file_stats;      
	int apd_nbytes;

	CURL *curl = NULL;
	CURLcode code;
	long responseCode;
	chunk_t chunk;
	char errorBuffer[CURL_ERROR_SIZE];
#endif

#ifdef _MSC_VER
	if(mkdir(a->alc_a.base_dir) < 0) {					
#else		
	if(mkdir(a->alc_a.base_dir, S_IRWXU) < 0) {
#endif
		if(errno != EEXIST) {
			printf("mkdir failed: cannot create directory %s (errno=%i)\n", a->alc_a.base_dir, errno);
			fflush(stdout);
			return -1;
		}
	}

	if(strcmp(a->sdp_file, "") != 0) {

		if(stat(a->sdp_file, &sdp_file_stats) == -1) {
			printf("Error: %s is not valid file name\n", a->sdp_file);
			fflush(stdout);
			return -1;
		}

		/* Allocate memory for buf */
		if(!(sdp_buf = (char*)calloc((sdp_file_stats.st_size + 1), sizeof(char)))) {
			printf("Could not alloc memory for sdp buffer!\n");
			fflush(stdout);
			return -1;
		}

		if((sdp_fp = fopen(a->sdp_file, "rb")) == NULL) {
			printf("Error: unable to open sdp_file %s\n", a->sdp_file);
			fflush(stdout);
			free(sdp_buf);
			return -1;
		}

		nbytes = fread(sdp_buf, 1, sdp_file_stats.st_size, sdp_fp); 

		if(parse_sdp_file(a, addrs, ports, sdp_buf) == -1) {
			free(sdp_buf);
			return -1;
		}

		free(sdp_buf);

		if(a->alc_a.nb_channel == 0) {
			printf("Error: No acceptable channels found in SDP.");
			fflush(stdout);
			return -1;
		}    
	}
	else {
		if(((a->alc_a.cc_id == Null) && (a->alc_a.nb_channel != 1))) {

			if(a->alc_a.addr_family == PF_INET) {
				for(j = 0; j < a->alc_a.nb_channel; j++) {
					memset(addrs[j], 0, INET_ADDRSTRLEN);
					ipv4.sin_addr.s_addr = htonl(ntohl(inet_addr(a->alc_a.addr)) + j);
					sprintf(addrs[j], "%s", inet_ntoa(ipv4.sin_addr));

					memset(ports[j], 0, MAX_PORT_LENGTH);
					sprintf(ports[j], "%i", (atoi(a->alc_a.port) + j));
				}
			}
			else if(a->alc_a.addr_family == PF_INET6) {

#ifdef _MSC_VER
				addr_size = sizeof(struct sockaddr_in6);
				WSAStringToAddress((char*)a->alc_a.addr, AF_INET6, NULL, (struct sockaddr*)&ipv6, &addr_size);
#else 
				inet_pton(AF_INET6, a->alc_a.addr, &ipv6.sin6_addr);
#endif

				for(j = 0; j < a->alc_a.nb_channel; j++) {
					memset(addrs[j], 0, INET6_ADDRSTRLEN);

#ifdef _MSC_VER
					addr_size = sizeof(addrs[j]);
					WSAAddressToString((struct sockaddr*)&ipv6, sizeof(struct sockaddr_in6),
						NULL, addrs[j], &addr_size);
#else
					inet_ntop(AF_INET6, &ipv6.sin6_addr, addrs[j], sizeof(addrs[j]));
#endif

					memset(ports[j], 0, MAX_PORT_LENGTH);
					sprintf(ports[j], "%i", (atoi(a->alc_a.port) + j));

					if(j < (a->alc_a.nb_channel - 1)) {
						if(increase_ipv6_address(&ipv6.sin6_addr) == -1) {
							printf("Increasing IPv6 address %s is not possible\n", addrs[j]);
							return -1;
						}
					}
				}
			}
		}
		else {
			memset(addrs[0], 0, INET_ADDRSTRLEN);
			memset(ports[0], 0, MAX_PORT_LENGTH);

			memcpy(addrs[0], a->alc_a.addr, strlen(a->alc_a.addr));
			memcpy(ports[0], a->alc_a.port, strlen(a->alc_a.port));
		}
	}

	if(a->alc_a.stop_time != 0) {
		time(&systime);
		curr_time = systime + 2208988800U;

		if(a->alc_a.stop_time <= curr_time) {
			printf("Session end time reached\n");
			fflush(stdout);
			return -1;
		}
	}

	// Start ROUTE session from command line indicated LCT Channel (TSI)
	*s_id = open_alc_session(&a->alc_a);

	if(*s_id < 0) {
		printf("Error opening ALC session\n");
		fflush(stdout);
		return -1;
	}

	if(a->alc_a.start_time != 0) {
		while(1) {

			time(&systime);
			curr_time = systime + 2208988800U;

			if((a->alc_a.start_time - 3) > curr_time) {

				if(!is_printed) {
					printf("Waiting for session start time...\n");
					fflush(stdout);
					is_printed = TRUE;
				}
			}
			else {
				break;
			}

			if(get_session_state(*s_id) == SExiting) {
				close_alc_session(*s_id);
				return -5;
			}
		}
	}

	if(a->alc_a.cc_id == Null) {

		for(i = 0; (int)i < a->alc_a.nb_channel; i++) {  // ONLY ONE LCT CHANNEL at the start (TSI = 0) to get SLS

			if(a->alc_a.addr_type == 1) { // Unicast
				retval = add_alc_channel(*s_id, a->alc_a.tsi, ports[i], addrs[0], a->alc_a.intface, a->alc_a.intface_name);
			}
			else { // Multicast
				retval = add_alc_channel(*s_id, a->alc_a.tsi, ports[i], addrs[i], a->alc_a.intface, a->alc_a.intface_name);
			}

			if(retval == -1) {
				close_alc_session(*s_id);
				return -1;
			}
		}
	}
	else if(a->alc_a.cc_id == RLC) {

		retval = add_alc_channel(*s_id, a->alc_a.tsi, ports[0], addrs[0], a->alc_a.intface, a->alc_a.intface_name);

		if(retval == -1) {
			close_alc_session(*s_id);
			return -1;	
		}
	}

	if(a->rx_object) {
		retval = receiver_in_object_mode(s_id, a);
		receiver.fdt = NULL;
	}
	else { // Automatic 'File' FDT Mode
		// Initialize the structures
		for (i = 0; i < MAX_CHANNELS_IN_SESSION; i++) {
			receiver.file_uri_table[i] = NULL;
		}

		receiver.wildcard_token = NULL;

		file_uri = strtok(a->file_path, ",");
		// Start filling the structures
		while (file_uri != NULL) {

			if (strchr(file_uri, '*') != NULL) {

				memset(wildcard_token, 0, MAX_PATH_LENGTH);

				if ((file_uri[0] == '*') && (strlen(file_uri) == 1)) {
					printf("Only *something*, something* and *something are valid values in wild card mode!\n");
					fflush(stdout);
					close_alc_session(*s_id);
					return -1;
				}
				else if ((file_uri[0] == '*') && (file_uri[(strlen(file_uri) - 1)] == '*')) {
					memcpy(wildcard_token, (file_uri + 1), (strlen(file_uri) - 2));
				}
				else if (file_uri[0] == '*') {
					memcpy(wildcard_token, (file_uri + 1), (strlen(file_uri) - 1));
				}
				else if (file_uri[(strlen(file_uri) - 1)] == '*') {
					memcpy(wildcard_token, file_uri, (strlen(file_uri) - 2));
				}
				else {
					printf("Only *something*, something* and *something are valid values in wild card mode!\n");
					fflush(stdout);
					close_alc_session(*s_id);
					return -1;
				}

				receiver.wildcard_token = wildcard_token;
			}
			else {
				receiver.file_uri_table[file_nb] = file_uri;
			}

			file_nb++;
			file_uri = strtok(NULL, ",");
		}

		receiver.fdt = NULL;
		receiver.s_id = *s_id;
		receiver.rx_automatic = a->rx_automatic;
		receiver.accept_expired_fdt_inst = a->alc_a.accept_expired_fdt_inst;
		receiver.verbosity = a->alc_a.verbosity;

		/* Create FDT receiving thread */
		if (a->alc_a.verbosity > 0) {
			printf("Creating FDT thread to receive incoming packets\n");
			fflush(stdout);
		}
#ifdef _MSC_VER
		handle_fdt_thread =
			(HANDLE)_beginthreadex(NULL, 0, (void*)fdt_thread, (void*)&receiver, 0, &fdt_thread_id);
		if (handle_fdt_thread == NULL) {
			printf("Error: flute_receiver, _beginthread\n");
			fflush(stdout);
			close_alc_session(*s_id);
			return -1;
		}
#else
		//MALEK EL KHATIB 07.05.2014...JUST A COMMENT: IT COMES HERE TO START RECEIVING FDT INSTANCES
		if ((pthread_create(&fdt_thread_id, NULL, fdt_thread, (void*)&receiver)) != 0) {
			printf("Error: flute_receiver, pthread_create\n");
			fflush(stdout);
			close_alc_session(*s_id);
			return -1;
		}
#endif

		/* Create Display thread */
		/*
		if (a->alc_a.verbosity > 0) {
			printf("Creating Display thread\n");
			fflush(stdout);
		}
		if (((a->rx_automatic) && (a->file_table_output))) {

#ifdef _MSC_VER
			handle_receiver_file_table_output_thread =
				(HANDLE)_beginthreadex(NULL, 0, (void*)receiver_file_table_output_thread,
					(void*)&receiver, 0, &receiver_file_table_output_thread_id);

			if (handle_receiver_file_table_output_thread == NULL) {
				perror("flute_receiver(): _beginthread");
				close_alc_session(*s_id);
				return -1;
			}
#else
			if (pthread_create(&receiver_file_table_output_thread_id, NULL, receiver_file_table_output_thread,
				(void*)&receiver) != 0) {
				perror("flute_receiver(): pthread_create");
				close_alc_session(*s_id);
				return -1;
			}
#endif
		}
		*/
		//MALEK EL KHATIB 08.05.2014...JUST A COMMENT: IT COMES HERE TO START RECEIVING files
		if (a->alc_a.verbosity > 0) {
			printf("Start Receiving Files ");
		}
		if (((a->rx_automatic) || (strcmp(a->file_path, "") != 0))) {
			if (a->alc_a.verbosity > 0) {
				printf("in FDT based mode\n");
			}
			fflush(stdout);
			retval = receiver_in_fdt_based_mode(a, &receiver);

			set_fdt_instance_parsed(receiver.s_id);
			// Exits upon getting SLS as there is no while loop, returns after getting that one EFDT and file.
		}
		else {
			if (a->alc_a.verbosity > 0) {
				printf("in UI based mode\n");
			}
			retval = receiver_in_ui_mode(a, &receiver);
		}


		/* Let's get the S-TSID listed in "envelope.xml" */
		/* open envelope file and read it to its structure */
		char* session_basedir = NULL;
		session_basedir = get_session_basedir(receiver.s_id);

		char tmp_filename[MAX_PATH_LENGTH];
		memset(tmp_filename, 0, MAX_PATH_LENGTH);
		sprintf(tmp_filename, "%s/%s", session_basedir, "envelope.xml");

		if (stat(tmp_filename, &env_file_stats) == -1) { // Check if the file exists
			printf("Error: %s is not valid file name\n", tmp_filename);
			fflush(stdout);
			close_alc_session(*s_id);

			if (receiver.fdt != NULL) {
				FreeFDT(receiver.fdt);
			}

			return -1;
		}

		/* Allocate memory for buf */
		if (!(env_buf = (char*)calloc((env_file_stats.st_size + 1), sizeof(char)))) {
			printf("Could not alloc memory for buffer!\n");
			fflush(stdout);
			close_alc_session(*s_id);

			if (receiver.fdt != NULL) {
				FreeFDT(receiver.fdt);
			}

			return -1;
		}

		if ((env_fp = fopen(tmp_filename, "rb")) == NULL) {  
			printf("Error: unable to open file %s\n", tmp_filename);
			fflush(stdout);
			free(env_buf);
			close_alc_session(*s_id);

			if (receiver.fdt != NULL) {
				FreeFDT(receiver.fdt);
			}

			return -1;
		}

		// Read the envelope.xml file
		env_nbytes = fread(env_buf, 1, env_file_stats.st_size, env_fp);

		if (env_nbytes <= 0) {
			free(env_buf);
			fclose(env_fp);
			close_alc_session(*s_id);

			if (receiver.fdt != NULL) {
				FreeFDT(receiver.fdt);
			}

			return -1;
		}

		fclose(env_fp);

		printf("Decoding MIME envelope\n"); fflush(stdout);

		env = decode_env_payload(env_buf);
		free(env_buf);

		if (a->alc_a.verbosity == 4) {
			printf("Envelope received with %d items\n", env->nb_of_items);
			fflush(stdout);
			Printenv(env);
		}

		/* open S-TSID file and read it to its structure */
		item_t* next_item;
		item_t* item;

		memset(tmp_filename, 0, MAX_PATH_LENGTH);
		next_item = env->item_list;

		while (next_item != NULL) {  // Go through list of SLS files
			item = next_item;

			if (!strcmp(item->contentType, "application/route-s-tsid+xml")) {
				sprintf(tmp_filename, "%s/%s", session_basedir, item->metadataURI);
				//sprintf(tmp_filename, "%s/%s", session_basedir, "S-TSID-Example-20220708.xml");
			}
			next_item = item->next;
		}
		if (a->alc_a.verbosity == 4) {
			printf("STSID at: %s\n", tmp_filename);
			fflush(stdout);
		}

		if (stat(tmp_filename, &stsid_file_stats) == -1) {
			printf("Error: %s is not valid file name\n", tmp_filename);
			fflush(stdout);
			close_alc_session(*s_id);

			if (receiver.fdt != NULL) {
				FreeFDT(receiver.fdt);
			}

			return -1;
		}

		/* Allocate memory for buf */
		if (!(stsid_buf = (char*)calloc((stsid_file_stats.st_size + 1), sizeof(char)))) {
			printf("Could not alloc memory for buffer!\n");
			fflush(stdout);
			close_alc_session(*s_id);

			if (receiver.fdt != NULL) {
				FreeFDT(receiver.fdt);
			}

			return -1;
		}

		if ((stsid_fp = fopen(tmp_filename, "rb")) == NULL) {
			printf("Error: unable to open file %s\n", tmp_filename);
			fflush(stdout);
			free(stsid_buf);
			close_alc_session(*s_id);

			if (receiver.fdt != NULL) {
				FreeFDT(receiver.fdt);
			}

			return -1;
		}

		stsid_nbytes = fread(stsid_buf, 1, stsid_file_stats.st_size, stsid_fp);

		if (stsid_nbytes <= 0) {
			free(stsid_buf);
			fclose(stsid_fp);
			close_alc_session(*s_id);

			if (receiver.fdt != NULL) {
				FreeFDT(receiver.fdt);
			}

			return -1;
		}

		fclose(stsid_fp);
		if (a->alc_a.verbosity == 4) {
			printf("XML parse the STSID\n");
			fflush(stdout);
		}
		stsid = decode_stsid_payload(stsid_buf);
		free(stsid_buf);

		if (a->alc_a.verbosity == 4) {
			printf("S-TSID received with %d ROUTE Session(s)\n", stsid->nb_of_rs);
			fflush(stdout);
			Printstsid(stsid);
		}

	}
	
	// NOW LAUNCH LCT CHANNELS (ALC SESSIONS)
	route_t* next_stsid_rs;
	route_t* stsid_rs; 
	lct_ch_t* next_stsid_lct;
	lct_ch_t* stsid_lct;
	fdt_t* lct_fdt;
	file_t* lct_file;
	file_t* next_file;
	alc_session_t* s;
	alc_channel_t* ch;

	unsigned long long buflen = 0;
	int content_enc_algo = -1;

	int updated;
	char* buf = NULL;
	unsigned int rs_cnt = 0;
	
	// Point to first items of structures
	next_stsid_rs = stsid->rs_list;
	next_stsid_lct = stsid->rs_list->lct_list;
	
	// SAVE this code for backup: Multi-ALC-SESSION (with THREADs)
	while (next_stsid_rs != NULL) {
		stsid_rs = next_stsid_rs;

		printf("RS dIpAddr: %s\tdPort: %s\tLCT channels: %d\n", stsid_rs->dIpAddr, stsid_rs->dPort, stsid_rs->nb_of_ls); fflush(stdout);
		for(rs_cnt = 0; rs_cnt < stsid_rs->nb_of_ls; rs_cnt++) {	// Start LCT Channels listed per ROUTE SESSION
			stsid_lct = next_stsid_lct;

			// OPEN LCT channel with TSI from TSID
			*s_id = read_alc_session(&a->alc_a, stsid_lct->tsi);
			s = get_alc_session(*s_id);
			//printf("session id: %d\n", *s_id);
			//fflush(stdout);
			s->max_channel = MAX_CHANNELS_IN_SESSION - 1;

			// Add LCT Channel to ROUTE Session
			if (stsid_rs->dIpAddr != NULL) retval = add_alc_channel(s->s_id, stsid_lct->tsi, stsid_rs->dPort, stsid_rs->dIpAddr, a->alc_a.intface, a->alc_a.intface_name);
			else retval = add_alc_channel(s->s_id, stsid_lct->tsi, ports[0], addrs[0], a->alc_a.intface, a->alc_a.intface_name);
			//printf("added ALC channel: %d\n", retval);
			//fflush(stdout);

			if (retval == -1) {
				printf("Could not add LCT channel, CLOSING SESSION\n");
				fflush(stdout);
				close_alc_session(*s_id);
				return -1;
			}

			ch = s->ch_list[0];	// This code has 1:1 relation betweeen CHANNEL and ALC SESSION
			//printf("New Channel ch_id: %d\n", ch->ch_id);
			//fflush(stdout);

			s->ls = stsid_lct;  // Tie each session's channel to S-TSID channel listing
			s->ls->fdt = (fdt_t*)calloc(1, sizeof(fdt_t));	// Create memory for this channel FDT
			s->ls->fdt->file_list = (file_t*)calloc(1, sizeof(file_t));

			lct_fdt = (fdt_t*)calloc(1, sizeof(fdt_t));	// Create local copy of the S-TSID FDT
			lct_file = (file_t*)calloc(1, sizeof(file_t));

			// Add next FDT with default value from S-TSID		
			if (lct_fdt != NULL) {
				//printf("fill in fdt\n");
				//fflush(stdout);

				lct_fdt->expires = stsid_lct->expires;
				//fdt->version = stsid_lct->efdtVersion;
				//fdt->maxExpiresDelta = stsid_lct->maxExpiresDelta;
				//fdt->maxTransportSize = stsid_lct->maxTransportSize;
				lct_fdt->type = stsid_lct->type;
				lct_fdt->file_list = lct_file;  // Start with Initialization Segment file
				lct_fdt->complete = stsid_lct->complete;
				lct_fdt->fec_enc_id = stsid_lct->fec_enc_id;
				lct_fdt->fec_inst_id = stsid_lct->fec_inst_id;
				lct_fdt->finite_field = stsid_lct->finite_field;
				lct_fdt->nb_of_es_per_group = stsid_lct ->nb_of_es_per_group;
				lct_fdt->max_sb_len = stsid_lct->max_sb_len;
				lct_fdt->es_len = stsid_lct->es_len;
				lct_fdt->max_nb_of_es = stsid_lct->max_nb_of_es;
				lct_fdt->nb_of_files = stsid_lct->nb_of_files;
				lct_fdt->encoding = stsid_lct->encoding;
				
				//printf("fill in lct_file\n");
				//fflush(stdout);
				lct_file->toi = stsid_lct->toi;
				lct_file->status = stsid_lct->status;
				lct_file->transfer_len = stsid_lct->maxTransportSize;
				lct_file->content_len = stsid_lct->content_len;
				lct_file->location = stsid_lct->location;
				lct_file->md5 = stsid_lct->md5;
				lct_file->type = stsid_lct->type;
				lct_file->encoding = stsid_lct->encoding;
				lct_file->expires = stsid_lct->expires;

				lct_file->fec_enc_id = stsid_lct->fec_enc_id;
				lct_file->fec_inst_id = stsid_lct->fec_inst_id;
				lct_file->finite_field = stsid_lct->finite_field;
				lct_file->nb_of_es_per_group = stsid_lct->nb_of_es_per_group;
				lct_file->max_sb_len = stsid_lct->max_sb_len;
				lct_file->es_len = stsid_lct->es_len;
				lct_file->max_nb_of_es = stsid_lct->max_nb_of_es;
			}

			s->fdt_instance_id = (long)stsid_lct->toi;
		
			// Add another FDT for new found file
			set_fdt_instance_id(s->s_id, s->fdt_instance_id);
			set_received_instance(s, s->fdt_instance_id);  // FDT is already in S-TSID

			// Load FDT			
			updated = load_fdt(s->ls->fdt, lct_fdt);
			//printf("updated: %d\n", updated);
			//fflush(stdout);

			if (updated < 0) {
				printf("First FDT not updated, error...\n");
				fflush(stdout);
			}
			else if (updated == 1) {
				if (s->verbosity == 4) {
					printf("First FDT updated, file description(s) complemented\n");
					fflush(stdout);
				}
			}
			else if (updated == 2) {
				if (s->verbosity == 4) {
					//printf("First FDT updated, new file description(s) added\n");
					//fflush(stdout);
					//PrintFDT(s->ls->fdt, s->s_id);
					//PrintFDT(fdt, s->s_id);
					//PrintFDT(ls->fdt, s->s_id);
					PrintFDT(stsid_lct->fdt, s->s_id);
				}

				//next_file = s->ls->fdt->file_list;
				//next_file = ls->fdt->file_list;
				next_file = stsid_lct->fdt->file_list;  // Reference the S-TSID File list to load local copy

				while (next_file != NULL) {
					lct_file = next_file;

					if (lct_file->status == 0) {

						if (lct_file->encoding == NULL) {
							content_enc_algo = 0;
						}
						else {
							if (strcmp(lct_file->encoding, "pad") == 0) {
								content_enc_algo = PAD;
							}
	#ifdef USE_ZLIB
							else if (strcmp(lct_file->encoding, "gzip") == 0) {
								content_enc_algo = GZIP;
							}
	#endif
							else {
								printf("Content-Encoding: %s not supported\n", lct_file->encoding);
								fflush(stdout);
								lct_file->status = 2;
								next_file = lct_file->next;
								continue;
							}
						}

						retval = set_wanted_object(s->s_id, lct_file->toi, lct_file->transfer_len,
							lct_file->es_len,
							lct_file->max_sb_len,
							lct_file->fec_inst_id,
							lct_file->fec_enc_id,
							lct_file->max_nb_of_es, content_enc_algo,
							lct_file->finite_field, lct_file->nb_of_es_per_group
						);

						if (retval < 0) {
							// Memory error
						}
						else {
							lct_file->status = 1;
						}
					}

					next_file = lct_file->next;

				}
			}

			set_fdt_instance_parsed(s->s_id);

			//FreeFDT(lct_fdt);

			// Create LCT Channel receiving threads
	#ifdef _MSC_VER
			s->handle_lct_thread =
				(HANDLE)_beginthreadex(NULL, 0, (void*)channel_file_mode_thread, ch, 0, &s->lct_thread_id);
			if (s->handle_lct_thread == NULL) {
				printf("Error: flute_session: LCT Thread, _beginthread\n");
				fflush(stdout);
				close_alc_session(s->s_id);
				return -1;
			}
	#else
			//MALEK EL KHATIB 07.05.2014...JUST A COMMENT: IT COMES HERE TO START RECEIVING FDT INSTANCES
			if (pthread_create(&lct_thread_id, NULL, channel_file_mode_thread, ch) != 0) {
				printf("Error: flute_receiver, pthread_create\n");
					fflush(stdout);
					close_alc_session(s->s_id);
					return -1;
			}
			//pthread_join
	#endif
			if (a->alc_a.verbosity == 4) {
				printf("Creating File based ALC receiving thread %d to receive incoming packets\n", s->lct_thread_id);
					fflush(stdout);
			}

	#ifdef _MSC_VER
			Sleep(1);
	#else
			usleep(1000);
	#endif

			next_stsid_lct = stsid_lct->next;

		}

		next_stsid_rs = stsid_rs->next;
	}

	s = get_alc_session(receiver.s_id);
	if (s->verbosity == 4) {
		printf("ADDED ALL CHANNELS\n");
		fflush(stdout);
	}

	// Monitor FDT Thread for updates to files (SLS)
	while (get_session_state(receiver.s_id) == SActive) {

		if (receiver.fdt->file_list != NULL) {
			// Recover updated SLS
			if (a->alc_a.verbosity == 4) {
				printf("Updating files in first LCT Channel\n"); fflush(stdout);
			}
			retval = receiver_in_fdt_based_mode(a, &receiver);

		}

#ifdef _MSC_VER
		Sleep(5);
#else
		usleep(5000);
#endif
		continue;
	}

	
	if (a->alc_a.verbosity > 0) {
		printf("Build Report\n");
		fflush(stdout);
	}
	build_report(a, &receiver, report);

	if(retval == -3) {
		if(a->alc_a.verbosity > 0) {
			printf("Sender closed the session\n");
			fflush(stdout);
		}
	}

	//MALEK EL KHATIB 07.05.2014...JUST A COMMENT: IT COMES HERE ONLY IF THE RECEIVING PROCESS IS STOPPED
	set_session_state(*s_id, SExiting);

	
#ifdef _MSC_VER
	if(get_alc_session(*s_id)->handle_rx_thread != NULL) {
		WaitForSingleObject(get_alc_session(*s_id)->handle_rx_thread, INFINITE);
		CloseHandle(get_alc_session(*s_id)->handle_rx_thread);
		get_alc_session(*s_id)->handle_rx_thread = NULL;
	}

	if(!a->rx_object) {
		WaitForSingleObject(handle_fdt_thread, INFINITE);
		CloseHandle(handle_fdt_thread);
	}

	//if(((a->rx_automatic) && (a->file_table_output))) {
	//	WaitForSingleObject(handle_receiver_file_table_output_thread, INFINITE);
	//	CloseHandle(handle_receiver_file_table_output_thread); 
	//}
#else
	if(get_alc_session(*s_id)->rx_thread_id != 0) {
		join_retval = pthread_join(get_alc_session(*s_id)->rx_thread_id, NULL);
		assert(join_retval == 0);
		pthread_detach(get_alc_session(*s_id)->rx_thread_id);
		get_alc_session(*s_id)->rx_thread_id = 0;
	}

	if(!a->rx_object) {
		join_retval = pthread_join(fdt_thread_id, NULL);
		assert(join_retval == 0);
		pthread_detach(fdt_thread_id);
	}

	//if(((a->rx_automatic) && (a->file_table_output))) {
	//	join_retval = pthread_join(receiver_file_table_output_thread_id, NULL);
	//	assert(join_retval == 0);
	//	pthread_detach(receiver_file_table_output_thread_id);
	//}
#endif
	
	remove_alc_channels(*s_id);
	

#ifdef USE_FILE_REPAIR

	if(receiver.fdt != NULL) {
		file = receiver.fdt->file_list;
	}

	if(file != NULL) {      
		if(a->rx_automatic) {
			/* If we found one file entry from the fdt that is not downloaded completely we */
			/* should do file repairing */

			while(1) {

				if(file->status != 2) {
					repairing_needed = 1;
					break;
				}

				if(file->next == NULL) {
					break;
				}
				else {
					file = file->next;
				}
			}
		}
		else if(receiver.wildcard_token != NULL) {

			/* If we found file entries from the fdt that match to the wildcard token and */
			/* is not downloaded completely we should do file repairing. */

			while(1) {

				if(strstr(file->location, receiver.wildcard_token) != NULL) {
					if(file->status == 1) {
						repairing_needed = 1;
					}
				}

				if(file->next == NULL) {
					break;
				}
				else {
					file = file->next;
				}
			}
		}
		else {

			/* If we found wanted files defined in file_uri_table which are not downloaded */
			/* completely repairing is needed */

			for(i = 0; i < FILE_URI_TABLE_SIZE; i++) {  
				if(receiver.file_uri_table[i] == NULL) {
					continue;
				}
				else {
					repairing_needed = 1;
					break;
				}
			}
		}
	}

	if((strcmp(a->repair, "") != 0) && repairing_needed) {

		curl = curl_easy_init();

		if(curl == NULL) {
			printf("Failed to create CURL connection\n");
			close_alc_session(*s_id);

			if(receiver.fdt != NULL) {
				FreeFDT(receiver.fdt);
			}

			return -1; 
		}

		code = curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);

		if(code != CURLE_OK) {
			printf("Failed to set error buffer [%d]\n", code);
    		close_alc_session(*s_id);
			
			if(receiver.fdt != NULL) {
				FreeFDT(receiver.fdt);
			}

			curl_easy_cleanup(curl);
			return -1; 
		}
		
		code = curl_easy_setopt(curl, CURLOPT_USERAGENT, "mbms-rel6-FLUTE-repair/0.1");

		if(code != CURLE_OK) {
			printf("Failed to set user agent [%s]\n", errorBuffer);
    		close_alc_session(*s_id);

			if(receiver.fdt != NULL) {
				FreeFDT(receiver.fdt);
			}

			curl_easy_cleanup(curl);
			return -1; 
		}

		/* Let's do file repairing */

		retval = 0;

		if(a->alc_a.verbosity > 0) {
			printf("Starting file repair procedure...\n");
		}

		/* Let's get the config from "apd.xml" */

		if(stat(a->repair, &apd_file_stats) == -1) {
			printf("Error: %s is not valid file name\n", a->repair);
			fflush(stdout);
			close_alc_session(*s_id);

			if(receiver.fdt != NULL) {
				FreeFDT(receiver.fdt);
			}

			curl_easy_cleanup(curl);
			return -1;
		}

		/* Allocate memory for buf */
		if(!(apd_buf = (char*)calloc((apd_file_stats.st_size + 1), sizeof(char)))) {
			printf("Could not alloc memory for buffer!\n");
			fflush(stdout);
			close_alc_session(*s_id);

			if(receiver.fdt != NULL) {
				FreeFDT(receiver.fdt);
			}

			curl_easy_cleanup(curl);
			return -1;
		}

		if((apd_fp = fopen(a->repair, "rb")) == NULL) {
			printf("Error: unable to open file %s\n", a->repair);
			fflush(stdout);
			free(apd_buf);
			close_alc_session(*s_id);

			if(receiver.fdt != NULL) {
				FreeFDT(receiver.fdt);
			}

			curl_easy_cleanup(curl);
			return -1;
		}

		apd_nbytes = fread(apd_buf, 1, apd_file_stats.st_size, apd_fp);

		if(apd_nbytes <= 0) {
			free(apd_buf);
			fclose(apd_fp);
			close_alc_session(*s_id);
	
			if(receiver.fdt != NULL) {
				FreeFDT(receiver.fdt);
			}

			curl_easy_cleanup(curl);
			return -1;
		}

		fclose(apd_fp);

		apd = decode_apd_config(apd_buf);
		free(apd_buf);

		if(apd->bmFileRepair->sessionDescriptionURI != NULL) {

			/* Let's get SDP description for the multicast repair session */

			code = curl_easy_setopt(curl, CURLOPT_URL, apd->bmFileRepair->sessionDescriptionURI);

			if(code != CURLE_OK) {
				printf("Failed to set URL [%s]\n", errorBuffer);
				close_alc_session(*s_id);

				if(receiver.fdt != NULL) {
					FreeFDT(receiver.fdt);
				}

				curl_easy_cleanup(curl);
				FreeAPD(apd);
				return -1;
			}

			code = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_buffer);

			if(code != CURLE_OK) {
				printf("Failed to set writer [%s]\n", errorBuffer);
       			close_alc_session(*s_id);

				if(receiver.fdt != NULL) {
					FreeFDT(receiver.fdt);
				}
	
				curl_easy_cleanup(curl);
				FreeAPD(apd);
				return -1;
			}

			chunk.data = NULL;
			chunk.size = 0;

			code = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);

			if(code != CURLE_OK) {
				printf("Failed to set write data [%s]\n", errorBuffer);
       			close_alc_session(*s_id);

				if(receiver.fdt != NULL) {
					FreeFDT(receiver.fdt);
				}

				curl_easy_cleanup(curl);
				FreeAPD(apd);
				return -1;
			}

			code = curl_easy_perform(curl);

			if(code != CURLE_OK) {
				printf("Failed to get '%s' [%s]\n", apd->bmFileRepair->sessionDescriptionURI, errorBuffer);
				close_alc_session(*s_id);

				if(receiver.fdt != NULL) {
					FreeFDT(receiver.fdt);
				}

				curl_easy_cleanup(curl);
				FreeAPD(apd);
				return -1;
			}
			
			code = curl_easy_getinfo(curl , CURLINFO_HTTP_CODE , &responseCode);

			if(code != CURLE_OK) {
				printf("Failed to get http response code [%s]\n", errorBuffer);
				close_alc_session(*s_id);

				if(receiver.fdt != NULL) {
					FreeFDT(receiver.fdt);
				}
			
				curl_easy_cleanup(curl);
				FreeAPD(apd);
				return -1;
			}

			if(responseCode == 200 && chunk.size != 0) {
				if(a->alc_a.verbosity > 0) {
					printf("Starting FLUTE file repair...\n");
				}

				retval = flute_file_repair(&receiver, a, chunk.data);

				if(retval == -3) {
					if(a->alc_a.verbosity > 0) {
						printf("Sender closed the session\n");
						fflush(stdout);
					}
				}

				free(chunk.data);
			}
		}
		else {

			/* if NULL then we should do ptp repair */

			while(1) {
				http_server_index = 0;
				nb_of_http_servers = 0;

				/* Let's get the amount of available serviceURIs */

				tmp_serviceURI = apd->postFileRepair->serviceURI_List;

				if(tmp_serviceURI == NULL) {
					if(a->alc_a.verbosity > 0) {
						printf("Number of HTTP servers available: %i\n", nb_of_http_servers);
						printf("HTTP file repair procedure failed.\n");
					}
					break;
				}
				else {
					while(1) {
						nb_of_http_servers++;
						if(tmp_serviceURI->next == NULL) {                              
							break;
						}   
						else {
							tmp_serviceURI = tmp_serviceURI->next;
						}
					}

					/* Let's choose randomly one of the servers */

					http_server_index = 1 + random_number(nb_of_http_servers);

					if(a->alc_a.verbosity > 0) {
						printf("Number of HTTP servers available: %i\n", nb_of_http_servers);
					}

					tmp_serviceURI = apd->postFileRepair->serviceURI_List;

					for(n = 1;; n++) {
						if(n == (int)http_server_index) {
							printf("Using: %s\n", tmp_serviceURI->URI);
							break;
						}
						else {
							tmp_serviceURI = tmp_serviceURI->next;
						}
					}

					/* Let's wait offsetTime + randomTime */

					waitTime = apd->postFileRepair->offsetTime;

					waitTime += random_number(apd->postFileRepair->randomTimePeriod + 1);

					if(a->alc_a.verbosity > 0) {
						printf("waitTime: %i\n", waitTime);               
					}

	#ifdef _MSC_VER
					Sleep(1000*waitTime);
	#else
					sleep(waitTime);
	#endif

					if(a->alc_a.verbosity > 0) {
						printf("Starting HTTP file repair...\n");
					}

					file_repair_sdp_buf = http_file_repair(&receiver, a->open_file, &retval, curl, tmp_serviceURI->URI);

					/* if file_repair_sdp_buf != NULL we should do multicast file repair */

					if(file_repair_sdp_buf != NULL) {
						break;
					}

					/* If retval == 0 HTTP repair has succeeded, if not we should try another server */

					if(retval == 0) {
						break;
					}
					else {
						/*Let's remove the used http server from the list so that it is not used anymore */

						if(tmp_serviceURI->prev != NULL) {
							tmp_serviceURI->prev->next = tmp_serviceURI->next;
						}
						else {
							apd->postFileRepair->serviceURI_List = tmp_serviceURI->next;								
						}
						if(tmp_serviceURI->next != NULL) {
							tmp_serviceURI->next->prev = tmp_serviceURI->prev;
						}
						free(tmp_serviceURI);
					}
				}
			}

			/* if file_repair_sdp_buf != NULL we should do multicast file repair */

			if(file_repair_sdp_buf != NULL) {

				if(a->alc_a.verbosity > 0) {
					printf("Starting FLUTE file repair after 302 Redirect response...\n");
				}

				retval = flute_file_repair(&receiver, a, file_repair_sdp_buf);

				if(retval == -3) {
					if(a->alc_a.verbosity > 0) {
						printf("Sender closed the session\n");
						fflush(stdout);
					}
				}
			}  
		}

		FreeAPD(apd);
		curl_easy_cleanup(curl);
	}
#endif /* USE_FILE_REPAIR */

	if(a->name_incomplete_objects) {
		/* name incomplete objects, so that all received data is used */
		retval = name_incomplete_objects(&receiver);
	}

	if(a->alc_a.cc_id == RLC) {
		if(a->alc_a.verbosity > 0) {
			printf("%i packets were lost in the session\n", get_alc_session(*s_id)->lost_packets);
			fflush(stdout);
		}
	}

	if(receiver.fdt != NULL) {
		FreeFDT(receiver.fdt);
	}

	close_alc_session(*s_id);

	return retval;
}

void free_receiver_report(flute_receiver_report_t *report) {
    flute_receiver_report_t *iterReport = report;

    while (iterReport != NULL) {
        missing_block_t *iterMB = iterReport->mb_list;
        while(iterMB != NULL) {
            missing_block_t *temp = iterMB->next;
            free (iterMB);
            iterMB = temp;
        }

        free (report->file_name);
        free (report->md5);
        iterReport = iterReport->next;
    }
}

unsigned long long flute_session_size(arguments_t *a, int *s_id) {

	BOOL old_flag = a->alc_a.calculate_session_size;
	int old_flag2 = a->alc_a.verbosity;
	int old_flag3 = a->alc_a.nb_tx;
	int old_flag4 = a->alc_a.cc_id;
	int myRet;
	unsigned long long session_size;

	a->alc_a.calculate_session_size = TRUE;
	a->alc_a.verbosity = 0;
	a->alc_a.cc_id = Null;

	if(a->cont) {
		printf("Session size can't be calculated due to continuous Tx mode.\n");
		a->alc_a.calculate_session_size = old_flag;
		a->alc_a.verbosity = old_flag2;
		a->alc_a.nb_tx = old_flag3;
		a->alc_a.cc_id = old_flag4;
		return 0;
	}

	myRet = flute_sender(a, s_id, &session_size);

	if(myRet < 0) {
		printf("Session size can't be calculated because myRet < 0.\n");
		printf("Error: myRet: %d\n", myRet);

		a->alc_a.calculate_session_size = old_flag;
		a->alc_a.verbosity = old_flag2;
		a->alc_a.nb_tx = old_flag3;
		a->alc_a.cc_id = old_flag4;
		return 0;
	}

	a->alc_a.calculate_session_size = old_flag;
	a->alc_a.verbosity = old_flag2;
	a->alc_a.nb_tx = old_flag3;
	a->alc_a.cc_id = old_flag4;

	return session_size;
}

void set_flute_session_state(int s_id, enum alc_session_states state) {

	set_session_state(s_id, state);
}

void set_all_flute_sessions_state(enum alc_session_states state) {

	set_all_sessions_state(state); 
}

void set_flute_session_base_rate(int s_id, int base_tx_rate) {
	update_session_tx_rate(s_id, base_tx_rate); 
}

int start_up_flute(void) {

#ifdef _MSC_VER
	WSADATA wsd;
	int wsreturn;
#endif

#ifdef _MSC_VER

	wsreturn = WSAStartup(MAKEWORD(2, 2), &wsd);

	if(wsreturn != 0) {
		printf("WSAStartup() failed\n");
		return wsreturn;
	}

#endif

	initialize_stsid_parser();
	initialize_env_parser();
	initialize_fdt_parser();
	initialize_efdt_parser();
	initialize_session_handler();
	initialize_lct_header();

	return 0;
}

void shut_down_flute(arguments_t *anArguments) {

#ifdef _MSC_VER
	WSACleanup();
#endif

	release_session_handler();
	release_fdt_parser();
	release_efdt_parser();
	release_env_parser();
	release_stsid_parser();
	release_lct_header();


	if(anArguments == NULL) {
		return;
	}

	if(anArguments->log_fd != -1) {
		close(anArguments->log_fd);
	}

	if(strcmp(anArguments->sdp_file, "") != 0) {
		sf_free(anArguments->src_filt);
		free(anArguments->src_filt);
		sdp_message_free(anArguments->sdp);
	}
}

void shut_down_flute2(void) {

#ifdef _MSC_VER
	WSACleanup();
#endif

	release_session_handler();
	release_fdt_parser();
	release_efdt_parser();
	release_env_parser();
	release_stsid_parser();
	release_lct_header();

}
