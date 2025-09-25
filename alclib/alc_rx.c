/** \file alc_rx.c \brief ALC level receiving
*
*  $Author: peltotal $ $Date: 2007/02/28 08:58:00 $ $Revision: 1.146 $
*
*  MAD-ALCLIB: Implementation of ALC/LCT protocols, Compact No-Code FEC,
*  Simple XOR FEC, Reed-Solomon FEC, and RLC Congestion Control protocol.
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
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#define _TIMESPEC_DEFINED // trigger skip of redefinition in pthread.h
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <math.h>
#include <assert.h>
#include <limits.h>

#ifdef _MSC_VER
//********** was removed, check Git
#include <pthread.h>
#include <winsock2.h>
#include <process.h>
#include <io.h>

#else
#include <pthread.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/time.h>
#endif

#include "defines.h"
#include "alc_rx.h"
#include "alc_channel.h"
#include "alc_session.h"
#include "mad_rlc.h"
#include "lct_hdr.h"
#include "null_fec.h"
#include "xor_fec.h"
#include "rs_fec.h"
#include "utils.h"
#include "transport.h"
#include "alc_list.h"
#include "../flutelib/stsid.h"
#include "../flutelib/flute.h"

//Malek El Khatib 08.08.2014
//Start
#include "../flutelib/flute_defines.h"
//unsigned short nb_of_symb_to_decode_simult = 1; /*<If es_len is too small, it is better to consider the whole payload as one es given that they are consecutive es>*/
//End

//struct packetBuffer circularPacketBuffer[circularBufferLength];
struct PacketBufferLinkedListType *packetBufferLinkedListRoot = NULL;
struct PacketBufferLinkedListType *packetBufferLinkedHead = NULL;


static unsigned long readPtr = 0;
static unsigned long writePtr = 0;
static unsigned int lastReadESI = 0;
static unsigned long long lastReadTOI = 0;

static long fullness = 0;

static unsigned int tunedIn = 0;
pthread_mutex_t bufferLock;

extern unsigned int workingPort;

/**
 * LCT Header variables semaphore
 */

#ifdef _MSC_VER
RTL_CRITICAL_SECTION lct_header_variables_semaphore;
CONDITION_VARIABLE packet_ready;
#else
static pthread_mutex_t lct_header_variables_semaphore = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static BOOL packet_ready = FALSE;
#endif

/**
 * This is a private function, which locks the LCT Header.
 *
 */

void lock_lct_header(void) {
#ifdef _MSC_VER
	EnterCriticalSection(&lct_header_variables_semaphore);
#else
	pthread_mutex_lock(&lct_header_variables_semaphore);
#endif
}

/**
 * This is a private function, which unlocks the LCT Header.
 *
 */

void unlock_lct_header(void) {
#ifdef _MSC_VER
	LeaveCriticalSection(&lct_header_variables_semaphore);
#else
	pthread_mutex_unlock(&lct_header_variables_semaphore);
#endif
}

void initialize_lct_header(void) {
#ifdef _MSC_VER
	InitializeCriticalSection(&lct_header_variables_semaphore);
	InitializeConditionVariable(&packet_ready); 
#else
#endif
}

void release_lct_header(void) {
#ifdef _MSC_VER
	DeleteCriticalSection(&lct_header_variables_semaphore);
#else
#endif
}


/**
* This is a private function which search and replaces strings.
*
*   str_replace(haystack, haystacksize, oldneedle, newneedle) --
*   Search haystack and replace all occurences of oldneedle with newneedle.
*   Resulting haystack contains no more than haystacksize characters (including the '\0').
*   If haystacksize is too small to make the replacements, do not modify haystack at all.
*
*  RETURN VALUES
*  str_replace() returns haystack on success and NULL on failure.
*  Failure means there was not enough room to replace all occurences of oldneedle.
*  Success is returned otherwise, even if no replacement is made.
*
*/

// char* str_replace(char* haystack, size_t haystacksize, const char* oldneedle, const char* newneedle);

// ------------------------------------------------------------------
// Implementation of function
// ------------------------------------------------------------------
#define SUCCESS (char *)haystack
#define FAILURE (void *)NULL

static BOOL locate_forward(char** needle_ptr, char* read_ptr, const char* needle, const char* needle_last);
static BOOL locate_backward(char** needle_ptr, char* read_ptr, const char* needle, const char* needle_last);

char* str_replace(char* haystack, size_t haystacksize, const char* oldneedle, const char* newneedle) {
	size_t oldneedle_len = strlen(oldneedle);
	size_t newneedle_len = strlen(newneedle);
	char* oldneedle_ptr;    // locates occurences of oldneedle
	char* read_ptr;         // where to read in the haystack
	char* write_ptr;        // where to write in the haystack
	const char* oldneedle_last =  // the last character in oldneedle
		oldneedle +
		oldneedle_len - 1;

	// Case 0: oldneedle is empty
	if (oldneedle_len == 0)
		return SUCCESS;     // nothing to do; define as success

	// Case 1: newneedle is not longer than oldneedle
	if (newneedle_len <= oldneedle_len) {
		// Pass 1: Perform copy/replace using read_ptr and write_ptr
		for (oldneedle_ptr = (char*)oldneedle,
			read_ptr = haystack, write_ptr = haystack;
			*read_ptr != '\0';
			read_ptr++, write_ptr++)
		{
			*write_ptr = *read_ptr;
			BOOL found = locate_forward(&oldneedle_ptr, read_ptr, oldneedle, oldneedle_last);
			if (found) {
				// then perform update
				write_ptr -= oldneedle_len;
				memcpy(write_ptr + 1, newneedle, newneedle_len);
				write_ptr += newneedle_len;
			}
		}
		*write_ptr = '\0';
		return SUCCESS;
	}

	// Case 2: newneedle is longer than oldneedle
	else {
		size_t diff_len =       // the amount of extra space needed 
			newneedle_len -     // to replace oldneedle with newneedle
			oldneedle_len;      // in the expanded haystack

		// Pass 1: Perform forward scan, updating write_ptr along the way
		for (oldneedle_ptr = (char*)oldneedle,
			read_ptr = haystack, write_ptr = haystack;
			*read_ptr != '\0';
			read_ptr++, write_ptr++)
		{
			BOOL found = locate_forward(&oldneedle_ptr, read_ptr, oldneedle, oldneedle_last);
			if (found) {
				// then advance write_ptr
				write_ptr += diff_len;
			}
			if (write_ptr >= haystack + haystacksize)
				return FAILURE; // no more room in haystack
		}

		// Pass 2: Walk backwards through haystack, performing copy/replace
		for (oldneedle_ptr = (char*)oldneedle_last;
			write_ptr >= haystack;
			write_ptr--, read_ptr--)
		{
			*write_ptr = *read_ptr;
			BOOL found = locate_backward(&oldneedle_ptr, read_ptr, oldneedle, oldneedle_last);
			if (found) {
				// then perform replacement
				write_ptr -= diff_len;
				memcpy(write_ptr, newneedle, newneedle_len);
			}
		}
		return SUCCESS;
	}
}

// locate_forward: compare needle_ptr and read_ptr to see if a match occured
// needle_ptr is updated as appropriate for the next call
// return true if match occured, false otherwise
static inline BOOL locate_forward(char** needle_ptr, char* read_ptr, const char* needle, const char* needle_last) {
	if (**needle_ptr == *read_ptr) {
		(*needle_ptr)++;
		if (*needle_ptr > needle_last) {
			*needle_ptr = (char*)needle;
			return TRUE;
		}
	}
	else
		*needle_ptr = (char*)needle;
	return FALSE;
}

// locate_backward: compare needle_ptr and read_ptr to see if a match occured
// needle_ptr is updated as appropriate for the next call
// return true if match occured, false otherwise
static inline BOOL locate_backward(char** needle_ptr, char* read_ptr, const char* needle, const char* needle_last) {
	if (**needle_ptr == *read_ptr) {
		(*needle_ptr)--;
		if (*needle_ptr < needle) {
			*needle_ptr = (char*)needle_last;
			return TRUE;
		}
	}
	else
		*needle_ptr = (char*)needle_last;
	return FALSE;
}

long newBufferFullness()
{
    return fullness;
}

struct packetBuffer *getEmptyBufferSlot()
{
    struct PacketBufferLinkedListType *newpb = malloc(sizeof(struct PacketBufferLinkedListType));
    if(newpb == NULL)
    {
        fprintf(stdout,"********** Error: Could not allocate for linked list\n");
        fprintf(stdout,"********** Error: Could not allocate for linked list\n");
        exit(-1);
    }
    
    newpb->nextpb = NULL;
    
    if(packetBufferLinkedHead == NULL)
    {
        if(packetBufferLinkedListRoot != NULL)
        {
            fprintf(stdout,"********** Error: Unhandled packet list exception\n");
            fprintf(stdout,"********** Error: Unhandled packet list exception\n");
            exit(-1);
        }
        packetBufferLinkedListRoot = newpb;
        packetBufferLinkedHead = newpb;
    }
    else
    {
        packetBufferLinkedHead->nextpb = newpb;
        packetBufferLinkedHead = packetBufferLinkedHead->nextpb;
    }

    return &newpb->pb;

    #if 0
    
    int index = 0;
	if(newBufferFullness() == circularBufferLength - 1)	//Dont let the pointers point to same thing again
	{
        fprintf(stdout,"********** WRITE: Could not find slot, buffer full\n");
        fprintf(stdout,"********** WRITE: Could not find slot, buffer full\n");
		return (struct packetBuffer *) NULL;
	}

    for(index = 0 ; index < circularBufferLength ; index++)
        if(circularPacketBuffer[index].occupied == FALSE)
            return &circularPacketBuffer[index];
        
    fprintf(stdout,"********** WRITE: Should never be here\n");
    fprintf(stdout,"********** WRITE: Should never be here\n");
    return (struct packetBuffer *) NULL;
    #endif
}

    
int newWriteToBuffer(struct packetBuffer buffer)
{
// Luke Fay	unsigned long savedWptr;
	
	pthread_mutex_lock(&bufferLock);
    
	if(newBufferFullness() == circularBufferLength - 1)	//Dont let the pointers point to same thing again
	{
		if(workingPort == 4001 || workingPort == 4003)
		{
			FILE * tempff = fopen("bufferLog.txt","a");
			fprintf(tempff,"********** WRITE: Could not write, buffer full\n");
			fprintf(stdout,"********** WRITE: Could not write, buffer full\n");
			fclose(tempff);
		}
		pthread_mutex_unlock(&bufferLock);
		return -1;
	}
	
	/*
	if(workingPort == 4001 || workingPort == 4003)
	{
        FILE * sendMergeFile;
        FILE * pktFile;
        static int first = 1;
        char packetFiles[200];
		static int first = 0;
		sprintf(packetFiles,"Merge/Pkt%.4d_%.4d.mp4",buffer.toi,buffer.esi);
        if(first)
            sendMergeFile = fopen("sendMerge.mp4","w");
        else
            sendMergeFile = fopen("sendMerge.mp4","a");

        first = 0;

        fwrite(buffer.buffer,1,buffer.length,sendMergeFile);
        fclose(sendMergeFile);
        pktFile = fopen(packetFiles,"w");
        fwrite(buffer.buffer,1,buffer.length,pktFile);
        fclose(pktFile);
	}
    */
	struct packetBuffer *emptySlot = getEmptyBufferSlot();

    emptySlot->occupied = TRUE;
    emptySlot->toi = buffer.toi;
    emptySlot->esi = buffer.esi;
	emptySlot->length = buffer.length;
    emptySlot->buffer = malloc(buffer.length);
    if(emptySlot->buffer == NULL)
    {
        fprintf(stdout,"********** Error: Could not allocate buffer\n");
        fprintf(stdout,"********** Error: Could not allocate buffer\n");
        exit(-1);
    }
    memcpy(emptySlot->buffer,buffer.buffer,buffer.length);

    fullness++;
	
	/*
	if(workingPort == 4001 || workingPort == 4003)
	{       
	#define mmy emptySlot->buffer
    
    static unsigned long long stoi = 0;
    static unsigned long long stsi = 0;
    static unsigned int ssbn = 0;
    static unsigned int sesi = 0;
		FILE * tempff = fopen("bufferLog.txt","a");
    
		fprintf(tempff,"WRITE: fullness %d, toi %llu, tsi %llu, sbn %u, esi %u, len %d, bytes %2x %2x %2x %2x %2x %2x %2x %2x",newBufferFullness(),buffer.toi,buffer.tsi,buffer.sbn,buffer.esi,buffer.length,mmy[0],mmy[1],mmy[2],mmy[3],mmy[4],mmy[5],mmy[6],mmy[7]);
        if(buffer.toi != stoi || buffer.tsi != stsi || buffer.sbn != ssbn || (buffer.esi - sesi) > 1)
            fprintf(tempff,"<==========================");
        fprintf(tempff,"\n");
        fclose(tempff);
        sesi = buffer.esi;
        ssbn = buffer.sbn;
        stsi = buffer.tsi;
        stoi = buffer.toi;
	}
	*/
	
	pthread_mutex_unlock(&bufferLock);
	
	return 0;
}

struct packetBuffer getNextBufferIndex()
{
    struct PacketBufferLinkedListType *esiStart = NULL;
    struct PacketBufferLinkedListType *oneBefore = NULL;
    struct PacketBufferLinkedListType *current = packetBufferLinkedListRoot;
    struct packetBuffer savedPB = {0,0,0,0,0,0,0};
    unsigned long long smallestTOI = ULLONG_MAX;
    int esiFound = FALSE;
    
    for(current = packetBufferLinkedListRoot ;  ; )
    {
        if(current->pb.toi < smallestTOI)
        {
            smallestTOI = current->pb.toi;
            esiStart = current;
        }
        
        if(current->nextpb == NULL)
            break;

        oneBefore = current;
        current = current->nextpb;
    }

    if(esiStart == NULL)
    {
        fprintf(stdout,"********** Error: Could not find smallest TOI in list\n");
        fprintf(stdout,"********** Error: Could not find smallest TOI in list\n");
        exit(-1);
    }
    
    if(smallestTOI != lastReadTOI)
        lastReadESI = 0;
    
    for(current = esiStart ;  ; )
    {
        if(current->pb.toi == smallestTOI && current->pb.esi == lastReadESI)
        {
            esiFound = TRUE;
            break;
        }
        
        if(current->nextpb == NULL)
            break;
        
        oneBefore = current;
        current = current->nextpb;
    }
    
    if(esiFound != TRUE)
        return savedPB;
        
    lastReadTOI = smallestTOI;
    lastReadESI++;

    if(oneBefore == NULL && current != packetBufferLinkedListRoot)
    {
        fprintf(stdout,"********** Error: Test failed, one before is NULL while we are not at head\n");
        fprintf(stdout,"********** Error: Test failed, one before is NULL while we are not at head\n");
        exit(-1);
    }

    if(current != packetBufferLinkedListRoot && current != packetBufferLinkedHead)
    {
        oneBefore->nextpb = current->nextpb;
    }
    else
    {
        if(current == packetBufferLinkedListRoot)
        {
            packetBufferLinkedListRoot = packetBufferLinkedListRoot->nextpb;
        }
        if(current == packetBufferLinkedHead)
        {
            packetBufferLinkedHead = oneBefore;
            if(oneBefore != NULL)
            {
                packetBufferLinkedHead->nextpb = NULL;
            }
        }
    }

    savedPB = current->pb;

    free(current);

    return savedPB;

    #if 0
        if(circularPacketBuffer[index].occupied == TRUE)
        {
            if(circularPacketBuffer[index].toi < smallestTOI)
            {
                smallestTOI = circularPacketBuffer[index].toi;
                startIndex = index;
            }
        }
        
    if(smallestTOI != lastReadTOI)
        lastReadESI = 0;
        
    for(index = startIndex ; index < circularBufferLength ; index++)
        if(circularPacketBuffer[index].occupied == TRUE && circularPacketBuffer[index].toi == smallestTOI)
        {
            if(circularPacketBuffer[index].esi == lastReadESI)
            {
                targetIndex = index;
                esiFound = TRUE;
                break;
            }
        }

    if(esiFound != TRUE)
        return circularBufferLength + 1;
        
    lastReadTOI = smallestTOI;
    lastReadESI++;

    return targetIndex;
    #endif

}

struct packetBuffer newReadFromBuffer()
{
	struct packetBuffer buffer;
	
	pthread_mutex_lock(&bufferLock);
	
	if(newBufferFullness() == 0)
	{		
		pthread_mutex_unlock(&bufferLock);
		return (struct packetBuffer){0,0,0,0,0};
	}

    buffer = getNextBufferIndex();
    if(buffer.length == 0)
    {       
        pthread_mutex_unlock(&bufferLock);
        return buffer;
    }
    
	/*
	if(workingPort == 4001 || workingPort == 4003)
	{
        static unsigned long long savedTOI = 0;
        static unsigned int savedESI = 0;

#define mmy buffer.buffer
		FILE * tempff = fopen("bufferLogRead.txt","a");
		fprintf(tempff,"***AD: fullness %d, toi %llu, esi %u, bytes %2x %2x %2x %2x %2x %2x %2x %2x",fullness,buffer.toi,buffer.esi,mmy[0],mmy[1],mmy[2],mmy[3],mmy[4],mmy[5],mmy[6],mmy[7]);
        if(savedTOI != buffer.toi || (buffer.esi - savedESI) > 1)
            fprintf(tempff,"<==========");
        fprintf(tempff,"\n");
        fclose(tempff);
        savedTOI = buffer.toi;
        savedESI = buffer.esi;
	}
	*/

	fullness --;
	
	pthread_mutex_unlock(&bufferLock);
	
	return buffer;
}

#if 0
int getBufferFullness()
{
	if(writePtr >= readPtr)
		return writePtr - readPtr;
	else
	{
		return (circularBufferLength - readPtr) + writePtr;
	}
}

 
int writeToBuffer(struct packetBuffer buffer)
{
	unsigned long savedWptr;
	pthread_mutex_lock(&bufferLock);
	
	if(getBufferFullness() == circularBufferLength - 1)	//Dont let the pointers point to same thing again
	{
		if(workingPort == 4001 || workingPort == 4003)
		{
			FILE * tempff = fopen("bufferLog.txt","a");
			fprintf(tempff,"********** WRITE: Could not write, buffer full\n");
			fprintf(stdout,"********** WRITE: Could not write, buffer full\n");
			fclose(tempff);
		}
		pthread_mutex_unlock(&bufferLock);
		return -1;
	}
	
	if(workingPort == 4001 || workingPort == 4003)
	{
        FILE * sendMergeFile;
        if(writePtr == 0 && readPtr == 0)
            sendMergeFile = fopen("sendMerge.mp4","w");
        else
            sendMergeFile = fopen("sendMerge.mp4","a");

        fwrite(buffer.buffer,1,buffer.length,sendMergeFile);
        fclose(sendMergeFile);
            
	}
	
	circularPacketBuffer[writePtr].length = buffer.length;
    memcpy(circularPacketBuffer[writePtr].buffer,buffer.buffer,buffer.length);

	savedWptr = writePtr;
	writePtr ++;

	if(writePtr == circularBufferLength)writePtr = 0;
	
	if(workingPort == 4001 || workingPort == 4003)
	{
        
        static int lhdrlen = 0;
        static unsigned long long ltsi = 0; /* TSI */
        static unsigned long long ltoi = 0; /* TOI */
        static unsigned int lsbn = 0;
        static unsigned int lesi = 0;
	#define mmy circularPacketBuffer[savedWptr].buffer
		FILE * tempff = fopen("bufferLog.txt","a");
    
		fprintf(tempff,"WRITE: wptr %d, rptr %d, fullness %d, hdrlen %d, tsi %llu, toi %llu, sbn %u, esi %u, bytes %2x %2x %2x %2x %2x %2x %2x %2x",savedWptr,readPtr,getBufferFullness(),hdrlen,tsi,toi,sbn,esi,mmy[0],mmy[1],mmy[2],mmy[3],mmy[4],mmy[5],mmy[6],mmy[7]);
if(tsi != ltsi || toi != ltoi || (sbn - lsbn) > 1 || lhdrlen != hdrlen || (esi - lesi) > 1)
    fprintf(tempff," ==> %d %d %d %d %d <============",lhdrlen != hdrlen,tsi != ltsi,toi != ltoi, (sbn - lsbn) > 1, (esi - lesi) > 1);
lhdrlen = hdrlen;
ltsi = tsi;
ltoi = toi;
lsbn = sbn;
lesi = esi;


fprintf(tempff,"\n");

fclose(tempff);
	}
	
	pthread_mutex_unlock(&bufferLock);
	
	return 0;
}

struct packetBuffer readFromBuffer()
{
	struct packetBuffer buffer;
	unsigned long savedWptr;
	
	pthread_mutex_lock(&bufferLock);
	
	if(getBufferFullness() == 0)
	{		
		pthread_mutex_unlock(&bufferLock);
		return (struct packetBuffer){0,0};
	}
	
	buffer = circularPacketBuffer[readPtr];
	savedWptr = readPtr;
	readPtr ++;

	if(readPtr == circularBufferLength)readPtr = 0;
	
	if(workingPort == 4001 || workingPort == 4003)
	{
#define mmy circularPacketBuffer[savedWptr].buffer
		//FILE * tempff = fopen("bufferLog.txt","a");
		//fprintf(tempff,"***AD: wptr %d, rptr %d, fullness %d, bytes %2x %2x %2x %2x %2x %2x %2x %2x\n",writePtr,savedWptr,getBufferFullness(),mmy[0],mmy[1],mmy[2],mmy[3],mmy[4],mmy[5],mmy[6],mmy[7]);
		//fclose(tempff);
	}
	
	pthread_mutex_unlock(&bufferLock);
	
	return buffer;
}

#endif

/**
* This is a private function which parses and analyzes an ALC packet.
*
* @param data pointer to the ALC packet
* @param len length of packet
* @param ch pointer to the channel
*
* @return status of packet [WAITING_FDT = 5, OK = 4, EMPTY_PACKET = 3, HDR_ERROR = 2,
*                          MEM_ERROR = 1, DUP_PACKET = 0]
*
*/

int parse_packet(char *data, int len, int *hdrlenp, unsigned long long *toip, unsigned long long *tsip, unsigned int *sbnp, unsigned int *esip, alc_channel_t *ch) {

//	int retval = 0;
	int het = 0;
	int hel = 0;
	int exthdrlen = 0;
	unsigned int word = 0;	
	short fec_enc_id = 0; 
	unsigned long long ull = 0;
//	unsigned long long block_len = 0;
//	unsigned long long pos = 0;

	/* LCT header upto CCI */

	def_lct_hdr_t *def_lct_hdr = NULL; 

	/* remaining LCT header fields*/


	/* EXT_FDT */

	unsigned short flute_version = 0; /* V */
	int fdt_instance_id = 0; /* FDT Instance ID */

	/* EXT_CENC */

	unsigned char content_enc_algo = 0; /* CENC */
	unsigned short reserved = 0; /* Reserved */ 

	/* EXT_FTI */

	unsigned long long transfer_len = 0; /* L */
	unsigned char finite_field = 0; /* m */
	unsigned char nb_of_es_per_group = 0; /* G */
	unsigned short es_len = 0; /* E */
	unsigned short sb_len = 0;
	unsigned int max_sb_len = 0; /* B */
	unsigned short max_nb_of_es = 0; /* max_n */
	int fec_inst_id = 0; /* FEC Instance ID */

	/* FEC Payload ID */

//	trans_obj_t *trans_obj = NULL;
//	trans_block_t *trans_block = NULL;
//	trans_unit_t *trans_unit = NULL;
//	trans_unit_t *tu = NULL;
//	trans_unit_t *next_tu = NULL;
//	wanted_obj_t *wanted_obj = NULL;

//	char *buf = NULL;

//	char filename[MAX_PATH_LENGTH];
//	double rx_percent = 0;

	unsigned short j = 0;
	unsigned short nb_of_symbols = 0;
    int hdrlen = 0;
    unsigned long long toi = 0;
    unsigned long long tsi = 0;
    unsigned int sbn = 0;
    unsigned int esi = 0;


	if(len < (int)(sizeof(def_lct_hdr_t))) {
		printf("analyze_packet: packet too short %d\n", len);
		fflush(stdout);
		return HDR_ERROR;
	}

	def_lct_hdr = (def_lct_hdr_t*)data;

	*(unsigned short*)def_lct_hdr = ntohs(*(unsigned short*)def_lct_hdr);

	hdrlen += (int)(sizeof(def_lct_hdr_t));
	printf("LCT header length: %i\n", hdrlen);

	if(def_lct_hdr->version != ALC_VERSION) {
		printf("1 ALC version: %i not supported!\n", def_lct_hdr->version);
		fflush(stdout);	
		return HDR_ERROR;
	}


	if (def_lct_hdr->psi != 2) {
		// Original field was reserved at 0 which indicates FLUTE, not A/331 ROUTE
		printf("PSI field not indicating Source Flow!, found %i\n", def_lct_hdr->psi);
		fflush(stdout);
		return HDR_ERROR;
	}

	if(def_lct_hdr->reserved != 0) {
		printf("Reserved field not zero!, found %i\n", def_lct_hdr->reserved);
		fflush(stdout);
		return HDR_ERROR;
	}

//	if(def_lct_hdr->flag_t != 0) {
//		printf("Sender Current Time not supported!\n");
//		fflush(stdout);
//		return HDR_ERROR;
//	}

//	if(def_lct_hdr->flag_r != 0) {
//		printf("Expected Residual Time not supported!\n");
//		fflush(stdout);
//		return HDR_ERROR;
//	}

	if(def_lct_hdr->flag_a == 1) {
		printf("Close Session Flag seen\n");
		fflush(stdout);
		return HDR_ERROR;
	}

	if(def_lct_hdr->flag_c != 0) {
		printf("Only 32 bits CCI-field supported!\n");
		fflush(stdout);
		return HDR_ERROR;
	}
	else {
		if(def_lct_hdr->cci != 0) {

			if(ch->s->cc_id == RLC) {

			}
		}
	}

	if(def_lct_hdr->flag_h == 1) {

		if(def_lct_hdr->flag_s == 0) { /* TSI 16 bits */
			word = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
			hdrlen += 4;

			tsi = (word & 0xFFFF0000) >> 16;

			if(tsi != ch->s->tsi) {
				//printf("Packet to wrong session: wrong TSI: %llu\n", tsi);
				fflush(stdout);
				return HDR_ERROR;
			}
		}
		else if(def_lct_hdr->flag_s == 1) { /* TSI 48 bits */

			ull = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
			tsi = ull << 16;
			hdrlen += 4;

			word = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
			hdrlen += 4;

			tsi += (word & 0xFFFF0000) >> 16;

			if(tsi != ch->s->tsi) {
				//printf("Packet to wrong session: wrong TSI: %llu\n", tsi);
				fflush(stdout);
				return HDR_ERROR;
			}
		}

		if(def_lct_hdr->flag_a == 1) { // Close Session Flag
			printf("Close Session Flag seen");
		}

		if(def_lct_hdr->flag_o == 0) { /* TOI 16 bits */
			toi = (word & 0x0000FFFF);
		}
		else if(def_lct_hdr->flag_o == 1) { /* TOI 48 bits */

			ull = (word & 0x0000FFFF);
			toi = ull << 32;

			toi += ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
			hdrlen += 4;
		}
		else {
			printf("Only 16, 32, 48 or 64 bits TOI-field supported!\n");
			fflush(stdout);
			return HDR_ERROR;
		}
		/*else if(def_lct_hdr->flag_o == 2) {			
		}
		else if(def_lct_hdr->flag_o == 3) {
		}*/
	}
	else {
		if(def_lct_hdr->flag_s == 1) { /* TSI 32 bits */
			tsi = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
			hdrlen += 4;

			if(tsi != ch->s->tsi) {
				//printf("Packet to wrong session: wrong TSI: %llu\n", tsi);
				fflush(stdout);
				return HDR_ERROR;
			}
		}
		else {
			printf("Transport Session Identifier not present!\n");
			fflush(stdout);
			return HDR_ERROR;
		}

		if(def_lct_hdr->flag_a == 1) { // Close Session Flag
			printf("Close Session Flag seen");
		}

		if(def_lct_hdr->flag_o == 0) { /* TOI 0 bits */

			if(def_lct_hdr->flag_b != 1) { // Close Object Flag
				printf("Transport Object Identifier not present!\n");
				fflush(stdout);
				return HDR_ERROR;
			}
			else {
				return EMPTY_PACKET;
			}
		}
		else if(def_lct_hdr->flag_o == 1) { /* TOI 32 bits */
			toi = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
			hdrlen += 4;
		}
		else if(def_lct_hdr->flag_o == 2) { /* TOI 64 bits */

			ull = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
			toi = ull << 32;
			hdrlen += 4;

			toi += ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
			hdrlen += 4;
		}
		else {
			printf("Only 16, 32, 48 or 64 bits TOI-field supported!\n");
			fflush(stdout);
			return HDR_ERROR;
		}
		/*else if(def_lct_hdr->flag_o == 3) {
		}*/
	}
    
	fec_enc_id = def_lct_hdr->codepoint;

	if(!(fec_enc_id == COM_NO_C_FEC_ENC_ID || fec_enc_id == RS_FEC_ENC_ID ||
		fec_enc_id == SB_SYS_FEC_ENC_ID || fec_enc_id == SIMPLE_XOR_FEC_ENC_ID)) {
			printf("FEC Encoding ID: %i is not supported!\n", fec_enc_id);
			fflush(stdout);
			return HDR_ERROR;
	}

	if(def_lct_hdr->hdr_len > (hdrlen >> 2)) {

		/* LCT header extensions(EXT_FDT, EXT_CENC, EXT_FTI, EXT_AUTH, EXT_NOP)
		go through all possible EH */

		exthdrlen = def_lct_hdr->hdr_len - (hdrlen >> 2);

		while(exthdrlen > 0) {

			word = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
			hdrlen += 4;
			exthdrlen--;

			het = (word & 0xFF000000) >> 24;

			if(het < 128) {
				hel = (word & 0x00FF0000) >> 16;
			}

			switch(het) {

			case EXT_FDT:

				flute_version = (word & 0x00F00000) >> 20;
				fdt_instance_id = (word & 0x000FFFFF);

				if(flute_version != FLUTE_VERSION) {
					printf("FLUTE version: %i is not supported\n", flute_version);
					return HDR_ERROR;
				}

				break;

			case EXT_CENC:

				content_enc_algo = (word & 0x00FF0000) >> 16;
				reserved = (word & 0x0000FFFF);

				if(reserved != 0) {
					printf("Bad CENC header extension!\n");
					return HDR_ERROR;
				}

#ifdef USE_ZLIB
				if((content_enc_algo != 0) && (content_enc_algo != ZLIB)) {
					printf("Only NULL or ZLIB content encoding supported with FDT Instance!\n");
					return HDR_ERROR;
				}
#else
				if(content_enc_algo != 0) {
					printf("Only NULL content encoding supported with FDT Instance!\n");
					return HDR_ERROR;
				}
#endif

				break;

			case EXT_FTI:

				if(hel != 4) {
					printf("Bad FTI header extension, length: %i\n", hel);
					return HDR_ERROR;
				}

				transfer_len = ((unsigned long long)(word & 0x0000FFFF) << 16);

				transfer_len += ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
				hdrlen += 4;
				exthdrlen--;

				word = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
				hdrlen += 4;
				exthdrlen--;

				if(fec_enc_id == RS_FEC_ENC_ID) {
					finite_field = (word & 0xFF000000) >> 24;
					nb_of_es_per_group = (word & 0x00FF0000) >> 16;

					/*if(finite_field < 2 || finite_field >16) {
					printf("Finite Field parameter: %i not supported!\n", finite_field);
					return HDR_ERROR;
					}*/
				}
				else {
					fec_inst_id = ((word & 0xFFFF0000) >> 16);

					if((fec_enc_id == COM_NO_C_FEC_ENC_ID || fec_enc_id == SIMPLE_XOR_FEC_ENC_ID)
						&& fec_inst_id != 0) {
							printf("Bad FTI header extension.\n");
							return HDR_ERROR;
					}
					else if(fec_enc_id == SB_SYS_FEC_ENC_ID && fec_inst_id != REED_SOL_FEC_INST_ID) {
						printf("FEC Encoding %i/%i is not supported!\n", fec_enc_id, fec_inst_id);
						return HDR_ERROR;
					}
				}

				if(((fec_enc_id == COM_NO_C_FEC_ENC_ID) || (fec_enc_id == SIMPLE_XOR_FEC_ENC_ID) 
					||(fec_enc_id == SB_LB_E_FEC_ENC_ID) || (fec_enc_id == COM_FEC_ENC_ID))){

						es_len = (word & 0x0000FFFF);

						max_sb_len = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
						hdrlen += 4;
						exthdrlen--;
				}
				else if(((fec_enc_id == RS_FEC_ENC_ID) || (fec_enc_id == SB_SYS_FEC_ENC_ID))) {

					es_len = (word & 0x0000FFFF);

					word = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));

					max_sb_len = ((word & 0xFFFF0000) >> 16);
					max_nb_of_es = (word & 0x0000FFFF);
					hdrlen += 4;
					exthdrlen--;
				}
				break;

			case EXT_AUTH:
				/* ignore */
				hdrlen += (hel-1) << 2;
				exthdrlen -= (hel-1);
				break;

			case EXT_NOP:
				/* ignore */
				hdrlen += (hel-1) << 2;
				exthdrlen -= (hel-1);
				break;

			case EXT_TIME:
				/* ignore */
				hdrlen += (hel-1) << 2;
				exthdrlen -= (hel-1);
				break;

			default:

				printf("Unknown LCT Extension header, het: %i\n", het);
				return HDR_ERROR;
				break;
			}
		}
	}

	if((hdrlen >> 2) != def_lct_hdr->hdr_len) {
		/* Wrong header length */
		printf("analyze_packet: packet header length %d, should be %d\n", (hdrlen >> 2),
			def_lct_hdr->hdr_len);
		return HDR_ERROR;
	}

	/* Check if we have an empty packet without FEC Payload ID */
	if(hdrlen == len) {
		return EMPTY_PACKET;		
	}

	if(toi == 0) {
		if(is_received_instance(ch->s, fdt_instance_id)) {
			return DUP_PACKET;
		}
		else {
		}
	}

	if((fec_enc_id == COM_NO_C_FEC_ENC_ID) || (fec_enc_id ==  COM_FEC_ENC_ID)) {

		if(len < hdrlen + 4) {
			printf("analyze_packet: packet too short %d\n", len);
			return HDR_ERROR;
		}

		//Malek El Khatib 08.08.2014
		//Start
		//word = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
		//sbn = (word >> 16);
		//esi = (word & 0xFFFF);
		esi = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
		//End
		hdrlen += 4;
	}
	else if(fec_enc_id == RS_FEC_ENC_ID) {
		word = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));

		sbn = (word >> finite_field);
		esi = (word & ((1 << finite_field) - 1));

		/* finite_field is not used furthermore, default value used in fec.c (#define GF_BITS  8 in fec.h) */

		hdrlen += 4;
	}
	else if(((fec_enc_id == SB_LB_E_FEC_ENC_ID) || (fec_enc_id == SIMPLE_XOR_FEC_ENC_ID))) {
		if (len < hdrlen + 8) {
			printf("analyze_packet: packet too short %d\n", len);
			return HDR_ERROR;
		}

		sbn = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
		hdrlen += 4;
		esi = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
		hdrlen += 4;

	}
	else if(fec_enc_id == SB_SYS_FEC_ENC_ID) {
		if (len < hdrlen + 8) {
			printf("analyze_packet: packet too short %d\n", len);
			return HDR_ERROR;
		}

		sbn = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));

		hdrlen += 4;
		word = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
		sb_len = (word >> 16);
		esi = (word & 0xFFFF);
		hdrlen += 4;
	}

	/* TODO: check if instance_id is set --> EXT_FDT header exists in packet */

	if(len - hdrlen != 0)
        ;
	else { /* We have an empty packet with FEC Payload ID */
		return EMPTY_PACKET;	
	}

    *hdrlenp = hdrlen;
    *toip = toi;
    *tsip = tsi;
    *sbnp = sbn;
    *esip = esi;


	return OK;
}

/**
* This is a private function which parses and analyzes an ALC packet.
*
* @param data pointer to the ALC packet
* @param len length of packet
* @param ch pointer to the channel
*
* @return status of packet [REPAIR = 7, NEW_TOI = 6, WAITING_FDT = 5, OK = 4, 
*							EMPTY_PACKET = 3, HDR_ERROR = 2, MEM_ERROR = 1, DUP_PACKET = 0]
*
*/

int analyze_packet(char *data, int len, unsigned long long *toir, alc_channel_t *ch) {

	int retval = 0;
	int het = 0;
	int hel = 0;
	int exthdrlen = 0;
	unsigned int word = 0;	
	short fec_enc_id = 0; 
	short atsc_codepoint = ATSC_RESERVED;
	unsigned long long ull = 0;
	unsigned long long block_len = 0;
	unsigned long long pos = 0;

	/* LCT header upto CCI */

	def_lct_hdr_t *def_lct_hdr = NULL; 

	/* remaining LCT header fields*/

	/* EXT_FDT */

	unsigned short flute_version = 0; /* V */
	int fdt_instance_id = 0; /* FDT Instance ID */

	/* EXT_CENC */

	unsigned char content_enc_algo = 0; /* CENC */
	unsigned short reserved = 0; /* Reserved */ 

	/* EXT_FTI */

	unsigned long long transfer_len = 0; /* L */
	unsigned char finite_field = 0; /* m */
	unsigned char nb_of_es_per_group = 0; /* G */
	unsigned short es_len = 0; /* E */
	unsigned short sb_len = 0;
	unsigned int max_sb_len = 0; /* B */
	unsigned short max_nb_of_es = 0; /* max_n */
	int fec_inst_id = 0; /* FEC Instance ID */

	/* FEC Payload ID */
	unsigned int start_offset = 0; /* SOURCE FLOW */


	trans_obj_t *trans_obj = NULL;
	trans_block_t *trans_block = NULL;
	trans_unit_t *trans_unit = NULL;
	trans_unit_t *tu = NULL;
	trans_unit_t *next_tu = NULL;
	wanted_obj_t *wanted_obj = NULL;

	BOOL close_object = FALSE;
	char *buf = NULL;

	char filename[MAX_PATH_LENGTH];
	double rx_percent = 0;

	unsigned short j = 0;
	unsigned short nb_of_symbols = 0;

	int hdrlen = 0;			/* length of whole FLUTE/ALC/LCT header */
	unsigned long long cci = 0; /* CCI */
	unsigned long long tsi = 0; /* TSI */
	unsigned long long toi = 0; /* TOI */
	unsigned int sbn = 0;
	unsigned int esi = 0;

	lock_lct_header();
    
	//printf("Begin analyze packet in channel %d\n", ch->ch_id); fflush(stdout);
	
	if(len < (int)(sizeof(def_lct_hdr_t))) {	// integer is of length 32 bits
		printf("analyze_packet: packet too short %d\n", len);
		fflush(stdout);
		unlock_lct_header();

		return HDR_ERROR;
	}

	def_lct_hdr = (def_lct_hdr_t*)data;

	// Critical to quickly read LCT Header to discern further processing.
	// Read 32 bit LCT flag header
	*(unsigned long*)def_lct_hdr = ntohl(*(unsigned long*)def_lct_hdr);
	
	hdrlen += (int)(sizeof(def_lct_hdr_t));
	atsc_codepoint = def_lct_hdr->codepoint;

	if(def_lct_hdr->version != ALC_VERSION) {
		printf("ALC version: %i not supported!\n", def_lct_hdr->version);
		fflush(stdout);	
		unlock_lct_header();

		return HDR_ERROR;
	}

	if (def_lct_hdr->psi != 2) {
		// First test for Source Flow
		//printf("PSI field not indicating Source Flow!, found %i\n", def_lct_hdr->psi);
		//fflush(stdout);
		// Then test for Repair Flow
		if (def_lct_hdr->psi != 0) {
			printf("PSI field not indicating Source Flow or Repair Flow!, found %i\n", def_lct_hdr->psi);
			fflush(stdout);

			unlock_lct_header();

			return HDR_ERROR;
		}

	}

	if(def_lct_hdr->reserved != 0) {
		printf("Reserved field not zero!, found %i\n", def_lct_hdr->reserved);
		fflush(stdout);
		unlock_lct_header();

		return HDR_ERROR;
	}

//	if(def_lct_hdr->flag_t != 0) {
//		printf("Sender Current Time not supported!\n");
//		fflush(stdout);
// 	    unlock_lct_header();
// 
//		return HDR_ERROR;
//	}

//	if(def_lct_hdr->flag_r != 0) {
//		printf("Expected Residual Time not supported!\n");
//		fflush(stdout);
// 	    unlock_lct_header();
// 
//		return HDR_ERROR;
//	}

	if(def_lct_hdr->flag_a == 1) { // Close Session Flag is seen
		printf("Close Session Flag seen\n");
		fflush(stdout);
		ch->s->state = SAFlagReceived;
	}

	if(def_lct_hdr->flag_c != 0) {
		printf("Only 32 bits CCI-field supported!\n");
		fflush(stdout);
		unlock_lct_header();

		return HDR_ERROR;
	}
	else {
		if(def_lct_hdr->cci != 0) {

			if(ch->s->cc_id == RLC) {
				printf("Conjection Control is RLC\n");
				fflush(stdout);
				retval = mad_rlc_analyze_cci(ch->s, (rlc_hdr_t*)(data + 4));

				if(retval < 0) {
					unlock_lct_header();

					return HDR_ERROR;
				}
			}
		}
	}

	if(def_lct_hdr->flag_h == 1) { // If half-word flag

		if(def_lct_hdr->flag_s == 0) { /* TSI 16 bits */
			word = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
			hdrlen += 4;

			tsi = (word & 0xFFFF0000) >> 16;

			if(tsi != ch->s->tsi) {
				printf("Packet to wrong session: wrong TSI: %llu\n", tsi);
				fflush(stdout);
				unlock_lct_header();

				return EMPTY_PACKET;
			}
		}
		else if(def_lct_hdr->flag_s == 1) { /* TSI 48 bits */

			ull = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
			tsi = ull << 16;
			hdrlen += 4;

			word = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
			hdrlen += 4;

			tsi += (word & 0xFFFF0000) >> 16;

			if(tsi != ch->s->tsi) {
				printf("Packet to wrong session: wrong TSI: %llu\n", tsi);
				fflush(stdout);
				unlock_lct_header();

				return EMPTY_PACKET;
			}
		}

		if(def_lct_hdr->flag_a == 1) { // Close Session Flag is seen
			ch->s->state = SAFlagReceived;
			printf("Close Session Flag seen\n");
			fflush(stdout);
		}

		if(def_lct_hdr->flag_o == 0) { /* TOI 16 bits */
			toi = (word & 0x0000FFFF);
		}
		else if(def_lct_hdr->flag_o == 1) { /* TOI 48 bits */

			ull = (word & 0x0000FFFF);
			toi = ull << 32;

			toi += ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
			hdrlen += 4;
		}
		else {
			printf("Only 16, 32, 48 or 64 bits TOI-field supported!\n");
			fflush(stdout);
			unlock_lct_header();

			return HDR_ERROR;
		}
		/*else if(def_lct_hdr->flag_o == 2) {			
		}
		else if(def_lct_hdr->flag_o == 3) {
		}*/
	}
	else { // else it is a full 32 bit word

		if(def_lct_hdr->flag_s == 1) { /* TSI 32 bits */

			// Read 32 bit (TSI)
			tsi = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
			hdrlen += 4;

			if(tsi != ch->s->tsi) {
				if (ch->s->verbosity == 4) {
					printf("Packet to different session, see TSI: %llu\tworking in TSI: %d\n", tsi, ch->ch_id);
					fflush(stdout);
				}

				unlock_lct_header();

				return EMPTY_PACKET;
			}
			else {
				if (ch->s->verbosity == 4) {
					printf("Packet to current session, see TSI: %llu\tworking in TSI: %d\n", tsi, ch->ch_id);
					fflush(stdout);
				}
			}
		}
		else {
			printf("Transport Session Identifier not present!\n");
			fflush(stdout);

			unlock_lct_header();

			return HDR_ERROR;
		}

		if(def_lct_hdr->flag_a == 1) { // Close Session Flag is seen
			ch->s->state = SAFlagReceived;
			if (ch->s->verbosity == 4) {
				printf("Close Session Flag seen\n");
				fflush(stdout);
			}
			// Trigger memory clearing
#ifdef USE_RETRIEVE_UNIT
		ch->s->last_given = NULL;	// If closing segment, signal the last transport unit
#endif
		}

		if (def_lct_hdr->flag_b == 1) { // Close Object Flag is seen, do not gate close object on zero TOI bits
			if (ch->s->verbosity == 4) {
				printf("Close Object Flag seen\n");
				fflush(stdout);
			}
			// Trigger memory clearing
			close_object = TRUE;
#ifdef USE_RETRIEVE_UNIT
		ch->s->last_given = NULL;	// If closing segment, signal the last transport unit
#endif
		}

		if(def_lct_hdr->flag_o == 0) { /* TOI 0 bits */

			if(def_lct_hdr->flag_b != 1) { // Close Object Flag is not seen
				printf("Transport Object Identifier Close Object Flag not present!\n");
				fflush(stdout);

				unlock_lct_header();

				return HDR_ERROR;
			}
			else {
				printf("Transport Object Identifier Close Object Flag present!\n");
				fflush(stdout);
				// Trigger memory clearing
				close_object = TRUE;
#ifdef USE_RETRIEVE_UNIT
				ch->s->last_given = NULL;	// If closing segment, signal the last transport unit
#endif

				unlock_lct_header();

				return EMPTY_PACKET;
			}
		}
		else if(def_lct_hdr->flag_o == 1) { /* TOI 32 bits */

			// Read 32 bit TOI
			toi = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
			hdrlen += 4;

		}
		else if(def_lct_hdr->flag_o == 2) { /* TOI 64 bits */

			ull = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
			toi = ull << 32;
			hdrlen += 4;

			toi += ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
			hdrlen += 4;

		}
		else {
			printf("Only 16, 32, 48 or 64 bits TOI-field supported!\n");
			fflush(stdout);

			unlock_lct_header();

			return HDR_ERROR;
		}
		/*else if(def_lct_hdr->flag_o == 3) {
		}*/
	}
    
	if ((!tsi) == ch->ch_id) {
		printf("Different Channel ");
		fflush(stdout);

		unlock_lct_header();

		return EMPTY_PACKET;	// Quickly exit packet inspection if packet is not for this LCT Channel
	}

	*toir = toi; 


	// Once complete reading LCT HEADER, get wanted object
	if (toi != FDT_TOI && tsi == ch->ch_id) {
		if (ch->s->verbosity == 4) {
			printf("See object %llu in session %d\n", toi, ch->s->s_id);
			fflush(stdout);
		}

		// Register desired object TOI
		wanted_obj = get_wanted_object(ch->s, toi);

		if (wanted_obj == NULL) {
			// Malek El Khatib
			//printf("wanted_obj is NULL in Session %d\n", ch->s->s_id); fflush(stdout);
			// Start 12.11.2014
			// Always rebuffer since if fdt is sent at beginning, a packet might be dropped      Malek El Khatib 16.07.2014
			//if (ch->s->rx_fdt_instance_list == NULL || ch->s->waiting_fdt_instance == TRUE || sendFDTAfterObj == TRUE) {
			if (ch->s->waiting_fdt_instance == TRUE) {
				//printf("MalekElKhatib: Packet rebuffering for toi %llu in TSI: %lli  waitingFDT %d\n", toi, tsi, ch->s->waiting_fdt_instance);
				//fflush(stdout);

				unlock_lct_header();

				return WAITING_FDT;
			}
			else if (ch->s->s_id > 0 && atsc_codepoint >= 7) {	// Skip to fill out new FDT for media
				//printf("MALEK_WANTED PACKET IS DROPPED2\n"); fflush(stdout);
				//printf("Packet is real time %d in Session %d\n", ch->s->ls->rt, ch->s->s_id);
				//fflush(stdout);

				start_offset = 0;

				goto alc_object;
			}
			else if (ch->s->s_id > 0) {
				if (ch->s->verbosity == 4) {
					printf("EFDT based file %llu found in Session %u\n", toi, ch->s->s_id);
					fflush(stdout);
				}

				// Continue reading LCT Header Extensions

			}
			else {	// Ignore, in the middle of other packets
				unlock_lct_header();

				return OK;
			}

		}
		else {
			if (ch->s->verbosity == 4) {
				printf("Recall wanted object parameters\n");
				fflush(stdout);
			}
			es_len = wanted_obj->es_len;
			max_sb_len = wanted_obj->max_sb_len;
			max_nb_of_es = wanted_obj->max_nb_of_es;
			fec_enc_id = wanted_obj->fec_enc_id;
			transfer_len = wanted_obj->transfer_len;
			content_enc_algo = wanted_obj->content_enc_algo;
		}

		/*
		if (fec_enc_id == RS_FEC_ENC_ID) {
			//printf("See Reed Solomon FEC\n"); fflush(stdout);
			finite_field = wanted_obj->finite_field;
			nb_of_es_per_group = wanted_obj->nb_of_es_per_group;
		}
		else {
			//printf("FEC instance ID is set\n"); fflush(stdout);
			fec_inst_id = wanted_obj->fec_inst_id;
		}
		*/
	}


	// Luke Fay: setting FEC encoding for A/331 if EXT_FTI is used
	//fec_enc_id = def_lct_hdr->codepoint;
	fec_enc_id = COM_NO_C_FEC_ENC_ID;

	if(!(fec_enc_id == COM_NO_C_FEC_ENC_ID || fec_enc_id == RAPTOR_FEC_ENC_ID || fec_enc_id == RS_FEC_ENC_ID ||
		fec_enc_id == SB_SYS_FEC_ENC_ID || fec_enc_id == SIMPLE_XOR_FEC_ENC_ID) && (def_lct_hdr->psi < 2)) {  
		// Source Packet Indicator is not set AND FEC encoding ID is not known...then...
		printf("FEC Encoding ID: %i is not supported!\n", fec_enc_id);
		fflush(stdout);

		unlock_lct_header();

		return HDR_ERROR;
	}

	// Read LCT HEADER EXTENSIONS !!!!
	if(def_lct_hdr->hdr_len > (hdrlen >> 2)) { 
		if (ch->s->verbosity == 4) {
			printf("Reading LCT Header extensions in toi: %llu in tsi: %llu\n", toi, tsi);
			fflush(stdout);
		}

		/* LCT header extensions(EXT_FDT, EXT_CENC, EXT_FTI, EXT_AUTH, EXT_NOP)
		go through all possible Extension Headers */

		exthdrlen = def_lct_hdr->hdr_len - (hdrlen >> 2);

		if (ch->s->verbosity == 4) {
			printf("Reading LCT Header length %u, Extended Header Length %u\n", def_lct_hdr->hdr_len, exthdrlen);
			fflush(stdout);
		}

		while(exthdrlen > 0) {

			word = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
			hdrlen += 4;  // There is at least one more 32 bit header (4 bytes)
			exthdrlen--;  // Start with one extended header being processed

			het = (word & 0xFF000000) >> 24;

			if(het < 128) {
				hel = (word & 0x00FF0000) >> 16;
			}
			
			if (ch->s->verbosity == 4) {
				printf("Extended LCT Header type: %d\n", het);
				fflush(stdout);
			}

			switch(het) {  // Switch on which type of extended header is present

			case EXT_FDT:

				flute_version = (word & 0x00F00000) >> 20;
				fdt_instance_id = (word & 0x000FFFFF);

				if((!flute_version) == (FLUTE_VERSION || ROUTE_VERSION)) {	// Version 1 or Version 2
					printf("ROUTE version: %i is not supported\n", flute_version);
					fflush(stdout);
					unlock_lct_header();

					return HDR_ERROR;
				}

				break;

			case EXT_CENC:

				content_enc_algo = (word & 0x00FF0000) >> 16;
				reserved = (word & 0x0000FFFF);

				if(reserved != 0) {
					printf("Bad CENC header extension!\n");
					fflush(stdout);
					unlock_lct_header();

					return HDR_ERROR;
				}

#ifdef USE_ZLIB
				if((content_enc_algo != 0) && (content_enc_algo != ZLIB)) {
					printf("Only NULL or ZLIB content encoding supported with FDT Instance!\n");
					fflush(stdout);
					unlock_lct_header();

					return HDR_ERROR;
				}
#else
				if(content_enc_algo != 0) {
					printf("Only NULL content encoding supported with FDT Instance!\n");
					unlock_lct_header();

					return HDR_ERROR;
				}
#endif

				break;

			case EXT_FTI:

				if(hel != 4) {
					printf("Bad FTI header extension, length: %i\n", hel);
					fflush(stdout);
					unlock_lct_header();

					return HDR_ERROR;
				}

				transfer_len = ((unsigned long long)(word & 0x0000FFFF) << 16);
				//transfer_len = (((unsigned long long)word & 0x0000FFFF) << 32);

				transfer_len += ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
				hdrlen += 4;
				exthdrlen--;

				word = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
				hdrlen += 4;
				exthdrlen--;

				if(fec_enc_id == RS_FEC_ENC_ID) {
					finite_field = (word & 0xFF000000) >> 24;
					nb_of_es_per_group = (word & 0x00FF0000) >> 16;

					/*if(finite_field < 2 || finite_field >16) {
					printf("Finite Field parameter: %i not supported!\n", finite_field);
					unlock_lct_header();

					return HDR_ERROR;
					}*/
				}
				else {
					fec_inst_id = ((word & 0xFFFF0000) >> 16);

					if((fec_enc_id == COM_NO_C_FEC_ENC_ID || fec_enc_id == SIMPLE_XOR_FEC_ENC_ID)
						&& fec_inst_id != 0) {
							printf("Bad FTI header extension.\n");
							fflush(stdout);
							unlock_lct_header();

							return HDR_ERROR;
					}
					else if(fec_enc_id == SB_SYS_FEC_ENC_ID && fec_inst_id != REED_SOL_FEC_INST_ID) {
						printf("FEC Encoding %i/%i is not supported!\n", fec_enc_id, fec_inst_id);
						fflush(stdout);
						unlock_lct_header();

						return HDR_ERROR;
					}
				}

				if(((fec_enc_id == COM_NO_C_FEC_ENC_ID) || (fec_enc_id == SIMPLE_XOR_FEC_ENC_ID) 
					||(fec_enc_id == SB_LB_E_FEC_ENC_ID) || (fec_enc_id == COM_FEC_ENC_ID))){

						es_len = (word & 0x0000FFFF);

						max_sb_len = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
						hdrlen += 4;
						exthdrlen--;
				}
				else if(((fec_enc_id == RS_FEC_ENC_ID) || (fec_enc_id == SB_SYS_FEC_ENC_ID))) {

					es_len = (word & 0x0000FFFF);

					word = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));

					max_sb_len = ((word & 0xFFFF0000) >> 16);
					max_nb_of_es = (word & 0x0000FFFF);
					hdrlen += 4;
					exthdrlen--;
				}

				if ((es_len || max_sb_len) == 0) {
					// Some logic to get through check below
					es_len = (unsigned short)(len - hdrlen - 4);
					max_sb_len = (unsigned int)transfer_len;
				}
				if (ch->s->verbosity == 4) {
					printf("EXT_FTI transfer length: %lli\t, FEC Instance ID: %i\tEncoded symbol length: %i\tMax Source block length %i\n", transfer_len, fec_inst_id, es_len, max_sb_len);
					fflush(stdout);
				}

				break;

			case EXT_AUTH:
				/* ignore */
				hdrlen += (hel-1) << 2;
				exthdrlen -= (hel-1);
				break;

			case EXT_NOP:
				/* ignore */
				hdrlen += (hel-1) << 2;
				exthdrlen -= (hel-1);
				break;

			case EXT_TIME:
				/* ignore */
				hdrlen += (hel-1) << 2;
				exthdrlen -= (hel-1);
				break;

			case EXT_NTP:
				/* 64 bit version of ROUTE Presentation NTP Time  */
				if (hel != 4) {
					printf("Bad EXT_NTP header extension, length: %i\n", hel);
					fflush(stdout);
					unlock_lct_header();

					return HDR_ERROR;
				}

				//ntp_time = (word << 32);

				//ntp_time += ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
				//hdrlen += 4;
				//exthdrlen--;
				
				/* ignore */
				hdrlen += (hel - 1) << 2;
				exthdrlen -= (hel - 1);

				break;

			case EXT_TOL:
				/* 24 bit version of Transport Object Length */
				transfer_len = (word & 0x00FFFFFF);

				// Some logic to get through check below
				es_len = (unsigned short)(len - hdrlen - 4);
				max_sb_len = (unsigned int)transfer_len;
				if (ch->s->verbosity == 4) {
					printf("EXT_TOL transfer length: %lli\t, FEC Instance ID: %i\tEncoded symbol length: %i\tMax Source block length %i\n", transfer_len, fec_inst_id, es_len, max_sb_len);
					fflush(stdout);
				}
				break;

			case EXT_TOLL:
				/* 48 bit version of Transport Object Length */
				if (hel != 2) {
					printf("Bad EXT_TOLL header extension, length: %i\n", hel);
					fflush(stdout);
					unlock_lct_header();

					return HDR_ERROR;
				}

				transfer_len = (((unsigned long long)word & 0x0000FFFF) << 32);

				transfer_len += ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
				hdrlen += 4;
				exthdrlen--;

				// Some logic to get through check below
				es_len = (unsigned short)(len - hdrlen - 4);
				max_sb_len = (unsigned int)transfer_len;
				//max_sb_len = (unsigned int)ch->s->ls->fdt->max_sb_len;
				if (ch->s->verbosity == 4) {
					printf("EXT_TOLL transfer length: %lli\t, FEC Instance ID: %i\tEncoded symbol length: %i\tMax Source block length %i\n", transfer_len, fec_inst_id, es_len, max_sb_len);
					fflush(stdout);
				}

				break;

			default:
				printf("Unknown LCT Extension header, het: %i\n", het);
				fflush(stdout);

				//unlock_lct_header();

				//return HDR_ERROR;
				break;
			}
		}
	}


	if((hdrlen >> 2) != def_lct_hdr->hdr_len) {
		/* Wrong header length */
		printf("analyze_packet: packet header length %d, should be %d\n", (hdrlen >> 2), def_lct_hdr->hdr_len);
		fflush(stdout);

		unlock_lct_header();

		return HDR_ERROR;
	}

	
	//printf("LCT header length with Header Extensions: %i\n", hdrlen);
	//fflush(stdout);
	// Extended LCT Header complete, now get FEC Payload ID ( OR SOURCE FLOW start_offset )

	/* Check if we have an empty packet without FEC Payload ID */
	if(hdrlen == len) {
		printf("HEADER ONLY ");
		fflush(stdout);

		unlock_lct_header();

		return EMPTY_PACKET;		
	}

	if (toi == FDT_TOI && tsi == ch->ch_id) {
		if (is_received_instance(ch->s, fdt_instance_id) && atsc_codepoint >= 7) {
			if (ch->s->verbosity == 4) {
				printf("Duplicate FDT %u\n", fdt_instance_id);
				fflush(stdout);
			}
			unlock_lct_header();
	
			return DUP_PACKET;
		}
		else {
			ch->s->waiting_fdt_instance = TRUE;
		}
	}

	if((fec_enc_id == COM_NO_C_FEC_ENC_ID) || (fec_enc_id ==  COM_FEC_ENC_ID) || def_lct_hdr->psi == 2) {

		if(len < hdrlen + 4) {
			printf("analyze_packet: packet too short %d\n", len);
			fflush(stdout);

			unlock_lct_header();

			return HDR_ERROR;
		}

		//Malek El Khatib 08.08.2014
		//Start
		word = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));

		if (def_lct_hdr->psi == 2) {  // SOURCE FLOW
			start_offset = (word);

			sbn = 0;
			esi = start_offset;
		}
		else { // REPAIR FLOW
			sbn = (word >> 24);
			esi = (word & 0x00FFFFFF);
			//esi = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
			if (ch->s->verbosity == 4) {
				printf("REPAIR FLOW FEC Payload ID: Source Block #: %u\t Encoding Symbol ID: %u\n", sbn, esi);
				fflush(stdout);
			}
		}
		//End
		hdrlen += 4;  // Add length of FEC Payload ID
	}
	else if(fec_enc_id == RAPTOR_FEC_ENC_ID) {
		word = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));

		sbn = (word >> finite_field);				// Source Block Number
		esi = (word & ((1 << finite_field) - 1));  // Encoding Symbol ID
		//sbn = ((word & 0xFFFF0000) >> 16);		// Source Block Number
		//esi = (word & 0x0000FFFF);				// Encoding Symbol ID

		hdrlen += 4;
	}
	else if (fec_enc_id == RS_FEC_ENC_ID) {
		word = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));

		//sbn = (word >> finite_field);				// Source Block Number
		//esi = (word & ((1 << finite_field) - 1));  // Encoding Symbol ID
		sbn = ((word & 0xFFFF0000) >> 16);		// Source Block Number
		esi = (word & 0x0000FFFF);				// Encoding Symbol ID

		// finite_field is not used furthermore, default value used in fec.c (#define GF_BITS  8 in fec.h) 
		max_sb_len = 32;

		hdrlen += 4;
	}
	else if(((fec_enc_id == SB_LB_E_FEC_ENC_ID) || (fec_enc_id == SIMPLE_XOR_FEC_ENC_ID))) {
		if (len < hdrlen + 8) {
			printf("analyze_packet: packet too short %d\n", len);
			fflush(stdout);

			unlock_lct_header();

			return HDR_ERROR;
		}

		sbn = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
		hdrlen += 4;
		esi = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
		hdrlen += 4;

	}
	else if(fec_enc_id == SB_SYS_FEC_ENC_ID) {
		if (len < hdrlen + 8) {
			printf("analyze_packet: packet too short %d\n", len);
			fflush(stdout);

			unlock_lct_header();

			return HDR_ERROR;
		}

		sbn = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
		hdrlen += 4;
		word = ntohl(*(unsigned int*)((char*)def_lct_hdr + hdrlen));
		sb_len = (word >> 16);
		esi = (word & 0xFFFF);
		hdrlen += 4;
	}

	alc_object:
	if (ch->s->verbosity == 4) {
		printf("ALC: collect objects\n");
		fflush(stdout);
	}

	// COLLECT OBJECT UNITS
	/* TODO: check if instance_id is set --> EXT_FDT header exists in packet */
	lct_ch_t* lsl = ch->s->ls;
	//lct_ch_t* lsl = ch->ls;
	uint32_t tag;
	
	if (start_offset == 0 && tsi == ch->ch_id && def_lct_hdr->psi == 2) {	// Look for 1st packet in Source Flows
	//if (tsi == ch->ch_id && def_lct_hdr->psi == 2) {	// Look for 1st packet in Source Flows
		if (ch->s->verbosity == 4) {
			printf("Start offset = 0 double check TOI %lld\tTransfer LEN %lld\tES_LEN %d\tSource blocks %d\n", toi, transfer_len, es_len, max_sb_len);
			fflush(stdout);
		}

		// correct the transfer length to signaled length in LCT Extended Header in first packet
		update_wanted_object(ch->s->s_id, toi, transfer_len, es_len, max_sb_len, fec_inst_id, fec_enc_id, max_nb_of_es, content_enc_algo, finite_field, nb_of_es_per_group);

		//ch->s->fdt_instance_id = (int)toi;
		//ch->fdt_instance_id = (long)toi;
#ifdef USE_RETRIEVE_UNIT
		ch->s->last_given = NULL;	// If starting new segment, signal the last transport unit
#endif
		// Add another FDT for new found file
		set_fdt_instance_id(ch->s->s_id, (unsigned int)toi);
		
		if (is_received_instance(ch->s, (unsigned int)toi)) { // If we already have that TOI, skip downloading
		//if (is_received_instance(ch->s, ch->s->fdt_instance_id)) { // If we already have that TOI, skip downloading
		//if (is_received_instance(ch->s, ch->fdt_instance_id)) { // If we already have that TOI, skip downloading
			if (ch->s->verbosity == 4) {
				printf("Already have FDT for TOI: %u\n", ch->s->fdt_instance_id);
				fflush(stdout);
			}

		}
		//else if (ch->s->tsi != 0) {
		//else if (ch->ch_id != 0) {
		else if (lsl != NULL) {
		//else if (ch->ch_id != 0 && lsl->rt == TRUE) { // Add another File to the real time stream FDT
			// Initialize next FDT with read value from LCT Extension Header	
			if (ch->s->verbosity > 1) {
				printf("TOI: %u in TSI: %llu\n", ch->s->fdt_instance_id, ch->s->tsi);
				fflush(stdout);
			}

			// Update the S-TSID
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

			char* session_basedir = NULL;

			if (atsc_codepoint == NRT_FILE_MODE) {
				if (ch->s->verbosity > 2) {
					printf("NRT FILE Found with ID %llu\n", toi);
					fflush(stdout);
				}

				/* Let's get the S-TSID listed in "envelope.xml" */
				/* open envelope file and read it to its structure */
				session_basedir = get_session_basedir(ch->s->s_id);

				char tmp_filename[MAX_PATH_LENGTH];
				memset(tmp_filename, 0, MAX_PATH_LENGTH);
				sprintf(tmp_filename, "%s/%s", session_basedir, "envelope.xml");

				if (stat(tmp_filename, &env_file_stats) == -1) { // Check if the file exists
					printf("Error: %s is not valid file name\n", tmp_filename);
					fflush(stdout);
				}

				/* Allocate memory for buf */
				if (!(env_buf = (char*)calloc((env_file_stats.st_size + 1), sizeof(char)))) {
					printf("Could not alloc memory for buffer!\n");
					fflush(stdout);
				}

				if ((env_fp = fopen(tmp_filename, "rb")) == NULL) {
					printf("Error: unable to open file %s\n", tmp_filename);
					fflush(stdout);
					free(env_buf);
				}

				// Read the envelope.xml file
				env_nbytes = fread(env_buf, 1, env_file_stats.st_size, env_fp);

				if (env_nbytes <= 0) {
					free(env_buf);
					fclose(env_fp);
				}

				fclose(env_fp);

				env = decode_env_payload(env_buf);
				free(env_buf);

				if (ch->s->verbosity == 4) {
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

					if (!strcmp(item->contentType, "application/route-s-tsid+xml") || !strcmp(item->contentType, "application/s-tsid")) {
						sprintf(tmp_filename, "%s/%s", session_basedir, item->metadataURI);
						//sprintf(tmp_filename, "%s/%s", session_basedir, "S-TSID-Example-20220708.xml");
					}
					next_item = item->next;
				}
				if (ch->s->verbosity == 4) {
					printf("STSID at: %s\n", tmp_filename);
					fflush(stdout);
				}

				if (stat(tmp_filename, &stsid_file_stats) == -1) { // Check if the file exists
					printf("Error: %s is not valid file name\n", tmp_filename);
					fflush(stdout);
				}

				/* Allocate memory for buf */
				if (!(stsid_buf = (char*)calloc((stsid_file_stats.st_size + 1), sizeof(char)))) {
					printf("Could not alloc memory for buffer!\n");
					fflush(stdout);
				}

				if ((stsid_fp = fopen(tmp_filename, "rb")) == NULL) {
					printf("Error: unable to open file %s\n", tmp_filename);
					fflush(stdout);
				}

				stsid_nbytes = fread(stsid_buf, 1, stsid_file_stats.st_size, stsid_fp);

				if (stsid_nbytes <= 0) {
					free(stsid_buf);
					fclose(stsid_fp);
				}

				fclose(stsid_fp);
				if (ch->s->verbosity == 4) {
					printf("XML parse the STSID\n");
					fflush(stdout);
				}
				stsid = decode_stsid_payload(stsid_buf);
				free(stsid_buf);

				if (ch->s->verbosity == 4) {
					printf("S-TSID received for file %s\n", stsid->rs_list->lct_list->location);
					fflush(stdout);
					//Printstsid(stsid);
				}
			}


			fdt_t* lct_fdt;
			file_t* lct_file;
			file_t* next_file;
			int updated;

			char str[16];
			char stra[16];
			char* rplcval;
				
			// Also add FDT for the next file
			lct_fdt = (fdt_t*)calloc(1, sizeof(fdt_t));
			lct_file = (file_t*)calloc(1, sizeof(file_t));

			if (lct_fdt != NULL) {
				//printf("fill in lct_efdt\n");
				//fflush(stdout);

				lct_fdt->expires = lsl->fdt->expires; //  ls->expires;
				//lct_fdt->version = lsl->efdtVersion;
				//lct_fdt->maxExpiresDelta = lsl->maxExpiresDelta;
				//lct_fdt->maxTransportSize = lsl->maxTransportSize;
				lct_fdt->type = lsl->fdt->type; //  ls->type;
				lct_fdt->file_list = lct_file;  // Start with Initialization Segment file
				lct_fdt->complete = lsl->fdt->complete;
				lct_fdt->fec_enc_id = lsl->fdt->fec_enc_id;
				lct_fdt->fec_inst_id = lsl->fdt->fec_inst_id;
				lct_fdt->finite_field = lsl->fdt->finite_field;
				lct_fdt->nb_of_es_per_group = lsl->fdt->nb_of_es_per_group;
				lct_fdt->max_sb_len = lsl->fdt->max_sb_len;
				lct_fdt->es_len = lsl->fdt->es_len;
				lct_fdt->max_nb_of_es = lsl->fdt->max_nb_of_es;
				lct_fdt->nb_of_files = lsl->fdt->nb_of_files;
				lct_fdt->encoding = lsl->fdt->encoding;

				//printf("fill in lct_file\n");
				//fflush(stdout);
				if (atsc_codepoint > 7) {
					lct_file->toi = toi + 1;
					lct_file->location = lsl->location;
				}
				else {	// NRT files or Duplicate files do not increment TOI
					lct_file->toi = toi;
					lct_file->location = stsid->rs_list->lct_list->location;
				}
				lct_file->status = lsl->status;
				lct_file->transfer_len = lsl->transfer_len; // lsl->maxTransportSize;
				lct_file->content_len = lsl->content_len;
				//lct_file->location = lsl->location;
				lct_file->md5 = lsl->md5;
				//lct_file->type = lsl->type;
				lct_file->type = lsl->fecOTI;
				lct_file->encoding = lsl->encoding;
				lct_file->expires = lsl->expires;

				lct_file->fec_enc_id = lsl->fec_enc_id;
				lct_file->fec_inst_id = lsl->fec_inst_id;
				lct_file->finite_field = lsl->finite_field;
				lct_file->nb_of_es_per_group = lsl->nb_of_es_per_group;
				lct_file->max_sb_len = lsl->max_sb_len;
				lct_file->es_len = lsl->es_len;
				lct_file->max_nb_of_es = lsl->max_nb_of_es;
			}

			// Update new File URI with found TOI
			//if (lsl->toi != toi && lsl->rt) { // Source Flow realtime streaming file mode increments TOI
			if (lsl->toi != toi) { // Source Flow realtime streaming file mode increments TOI
				sprintf(stra, "%llu", ch->s->tsi);
				sprintf(str, "%llu", lct_file->toi);
				if (lsl->fileTemplate != NULL) {
					sprintf(lct_file->location, "%s", lsl->fileTemplate);  // This is from AFDT and copyied to lct_file.location
					rplcval = str_replace(lct_file->location, 64, "$TOI$", str);  // This goes to lct_file.location
					rplcval = str_replace(lct_file->location, 64, "$Time$", str);  // Spec violation tolerance
				}
				else if (lct_file->location != NULL) {
					//printf("using existing filename %s\n", lct_file->location);
					//fflush(stdout);
					rplcval = "1";
				}
				else {
					sprintf(lct_file->location, "%s", "$TSI$_$TOI$");  // If fileTemplate is not in AFDT, just use TOI
					rplcval = str_replace(lct_file->location, 64, "$TSI$", stra);  // This goes to lct_file.location
					rplcval = str_replace(lct_file->location, 64, "$TOI$", str);  // This goes to lct_file.location
				}
				if (!rplcval) {
					printf("Not enough room to replace $TOI$ with `%llu'\n", lct_file->toi);
					fflush(stdout);
				}
				if (ch->s->verbosity == 4) {
					printf("Analyzed Packet File location: %s, TOI: %llu\n", lct_file->location, lct_file->toi);
					fflush(stdout);
				}
			}
			//else {  // NRT files do not increment TOI
			//	lct_file->toi = toi;
			//	if (ch->s->verbosity == 4) {
			//		printf("NRT File location: %s\n", lct_file->location);
			//		fflush(stdout);
			//	}
			//}

			// Update FDT			
			updated = update_fdt(lsl->fdt, lct_fdt);

			if (updated < 0) {
			}
			else if (updated == 1) {
				if (ch->s->verbosity == 4) {
					printf("Analyzed Packet FDT updated, file description(s) complemented\n");
					fflush(stdout);
				}
			}
			else if (updated == 2) {
				if (ch->s->verbosity == 4) {
					printf("Analyzed Packet FDT updated, new file description(s) added\n");
					fflush(stdout);
					//PrintFDT(fdt_instance, *s_id);
					PrintFDT(lsl->fdt, ch->s->s_id);
				}

				next_file = lsl->fdt->file_list;

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

						retval = set_wanted_object(ch->s->s_id, lct_file->toi, lct_file->transfer_len,
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

			set_fdt_instance_parsed(ch->s->s_id);
			set_received_instance(ch->s, (unsigned int)toi);
			//is_received_instance(ch->s, (unsigned int)toi) ? printf("\nsee %llu FDT\n", toi) : printf("\nDO NOT see %llu FDT\n", toi);
			//fflush(stdout);

			if (ch->s->verbosity > 1) {
				printf("Analyzed packet FDT #files: %d\tTOI: %lld\tURI: %s\tXfer Length: %lld\n", lsl->fdt->nb_of_files, lct_file->toi, lct_file->location, lct_file->transfer_len);
				fflush(stdout);
			}

			FreeFDT(lct_fdt);
			//FreeFile(lct_file);	 // Freeing files is part of FreeFDT

		}
		else {
			// Else keep NRT file listing already in the S-TSID
			if (ch->s->verbosity == 4) {
				printf("Keep File listed in S-TSID\n");
				fflush(stdout);
			}

			set_fdt_instance_parsed(ch->s->s_id);
			set_received_instance(ch->s, (unsigned int)toi);

			// Collect wanted object signaled in LCT Extended Header
			set_wanted_object(ch->s->s_id, toi, transfer_len, es_len, max_sb_len, fec_inst_id, fec_enc_id, max_nb_of_es, content_enc_algo, finite_field, nb_of_es_per_group);
			
		}

		// ENTITY MODE switch to process entity header as defined in RFC 7231 (set at beginning of file)
		ch->s->codepoint = atsc_codepoint;

	}
	else if (tsi == ch->ch_id && def_lct_hdr->psi == 2) {  // Start offset != 0 but still in desired channel
		if (ch->s->verbosity == 4) {
			printf("Start offset (%u) != 0 double check TOI %lld\tTransfer LEN %lld\tES_LEN %d\tSB_LEN %d\n", start_offset, toi, transfer_len, es_len, max_sb_len);
			fflush(stdout);
		}

		if (is_received_instance(ch->s, (unsigned int)toi)) { // If we already have that TOI, skip downloading
			if (ch->s->verbosity == 4) {
				printf("Already have FDT for TOI: %u\n", ch->s->fdt_instance_id);
				fflush(stdout);
			}
		}
		else {
			if (ch->s->verbosity == 4) {
				printf("Need new FDT for TOI: %llu\n", toi);
				fflush(stdout);
			}

			unlock_lct_header();

			return WAITING_FDT;
			//return EMPTY_PACKET;
			//start_offset = 0;

			//goto alc_object;
		}

		//if (lsl->rt != TRUE) {	// If NRT file, update wanted object
		if (atsc_codepoint < 7) {	// If NRT file, update wanted object
			// correct the transfer length to signaled length in LCT Extended Header in first packet
			update_wanted_object(ch->s->s_id, toi, transfer_len, es_len, max_sb_len, fec_inst_id, fec_enc_id, max_nb_of_es, content_enc_algo, finite_field, nb_of_es_per_group);
		}

		// else recall object parameters
		wanted_obj = get_wanted_object(ch->s, toi);
		
		if (wanted_obj != NULL) {

			//es_len = wanted_obj->es_len;	// This works for MediaCast vendor
			es_len = len - hdrlen;  // Just make one iteration (no repair in SRC FLOW)
			//max_sb_len = len - hdrlen;
			max_sb_len = wanted_obj->max_sb_len;
			max_nb_of_es = wanted_obj->max_nb_of_es;
			fec_enc_id = wanted_obj->fec_enc_id;
			//transfer_len = wanted_obj->transfer_len;
			content_enc_algo = wanted_obj->content_enc_algo;

			// Update the es_len
			update_wanted_object(ch->s->s_id, toi, transfer_len, es_len, max_sb_len, fec_inst_id, fec_enc_id, max_nb_of_es, content_enc_algo, finite_field, nb_of_es_per_group);
		}
	}
	else {	// psi != 2 and packet is REPAIR FLOW.  Store repair symbols.
		if (ch->s->verbosity == 4) {
			printf("Packet is for Repair flow in LCT channel: %llu\tSource Block %u\tSymbol %u\n", tsi, sbn, esi);
			fflush(stdout);
		}

		ch->s->lost_packets++;	// Used to count how many Repair Symbols are received.

		// Get repair data
#ifdef USE_RETRIEVE_UNIT
		trans_unit = retrieve_unit(ch->s, len - hdrlen);
#else
		trans_unit = create_units(1);
#endif
		if (trans_unit == NULL) {
			if (ch->s->verbosity == 4) {
				printf("REPAIR transport unit is NULL, memory error\n");
				fflush(stdout);
			}
			unlock_lct_header();

			return MEM_ERROR;
		}

		trans_unit->len = (unsigned short)(len - hdrlen);
		memcpy(trans_unit->data, (data + hdrlen), trans_unit->len); // Copy over data from Rx container to transmission unit
		if (ch->s->verbosity == 4) {
			printf("REPAIR copied Symbol %d_%d\n", sbn, esi);
			fflush(stdout);
		}

		// Setup file for Raptor Q repair symbols
		sprintf(filename, "%s/%s", ch->s->base_dir, "data.rq");

		// Prepare storage of repair bits into a file to be used later.
		int fr;		// Repair file pointer
		// Prepare Source Block #, Symbol # header for repair bits
		uint32_t label = ((sbn & 0xff) << 24) | (esi & 0x00ffffff);

		// Luke Fay 
#ifdef _MSC_VER
		if ((fr = open((const char*)filename,
			//_O_WRONLY | _O_CREAT | _O_BINARY | _O_TRUNC, _S_IREAD | _S_IWRITE)) < 0) {
			_O_WRONLY | _O_CREAT | _O_BINARY | _O_APPEND, _S_IWRITE)) < 0) {
			// Luke Fay
#else
		if ((fr = open64((const char*)filename,
			//O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU)) < 0) {
			O_WRONLY | O_CREAT | O_APPEND, S_IRWXU)) < 0) {
			//Luke Fay
#endif
			printf("Error: unable to open REPAIR file %s\n", filename);
			fflush(stdout);

			close(fr);

			unlock_lct_header();

			return MEM_ERROR;
		}
		// Write Source Block #, Symbol # as header for repair bits
		if (write(fr, &label, 4) == -1) {	// Write data to repair file
			printf("REPAIR Header write error, sbn: %u, esi: %u\n", sbn, esi);
			fflush(stdout);

			close(fr);

			unlock_lct_header();

			return MEM_ERROR;
		}
		// Write Repair bits
		if (write(fr, trans_unit->data, (unsigned int)trans_unit->len) == -1) {	// Write data to repair file
			printf("REPAIR write error, tsi: %llu, sbn: %i\n", tsi, sbn);
			fflush(stdout);

			close(fr);

			unlock_lct_header();

			return MEM_ERROR;
		}
		close(fr);

		free(trans_unit->data);
		trans_unit->data = NULL;

		unlock_lct_header();

		//printf("REPAIR FILE: %s\n", filename);
		//fflush(stdout);

		return REPAIR;
	}

	if (len - hdrlen != 0) {  // Find payload length (Received packet length - LCT header) is not zero: Start FEC decoding
		if (ch->s->verbosity == 4) {
			printf("packet length: %d\t Packet header length: %d\n", len, hdrlen);
			printf("transfer len: %lld\tFEC enc id: %d\t FEC inst id: %d\tes_len: %d\t max_sb_len: %d\n", transfer_len, fec_enc_id, fec_inst_id, es_len, max_sb_len);
			fflush(stdout);
		}

		/* check if we have enough information to perform FEC */
		// REPAIR FLOW requires Encoded Symbol count and Source Block numbers, but SOURCE FLOW does not.
		if (def_lct_hdr->psi == 0 && ((transfer_len == 0) || (fec_enc_id == -1) || ((fec_enc_id > 127) && (fec_inst_id == -1)) ||
			(esi == 0) || (sbn == 0))) {
			if (ch->s->verbosity == 4) {
				printf("Not enough information to create Repair Flow Transport Object, TOI: %llu in TSI: %llu\tFEC encoding ID: %i\n", toi, tsi, fec_enc_id);
				fflush(stdout);
			}
			unlock_lct_header();

			return OK;
		}
		// ELSE SOURCE FLOW
		else if (def_lct_hdr->psi == 2 && ((transfer_len == 0) || (fec_enc_id == -1) || ((fec_enc_id > 127) && (fec_inst_id == -1)))) {
		//else if (def_lct_hdr->psi == 2 && ((transfer_len == 0) || ((fec_enc_id > 127) && (fec_inst_id == -1)))) {
			if (ch->s->verbosity == 4) {
				printf("Not enough information to create Source Flow Transport Object, TOI: %llu in TSI: %llu\tFEC encoding ID: %i\n", toi, tsi, fec_enc_id);
				fflush(stdout);
			}
			unlock_lct_header();

			return OK;
		}

		if (ch->s->verbosity == 4) {
			printf("ALC: Correct Source / Repair Flow\n");
			fflush(stdout);
		}

		if (fec_enc_id == RS_FEC_ENC_ID) {
			nb_of_symbols = nb_of_es_per_group;
		}
		else {
			// Let's check how many symbols are in the packet 
			// Encoding Symbol group length = len - hdrlen 
			nb_of_symbols = (unsigned short)ceil(((double)len - hdrlen) / es_len);
		}

		//Malek El Khatib 08.08.2014
		//If payload contains consecutive es, it is better to decode them at once (i.e. without extracting them into seperate tr_units
		int nb_of_iterations = nb_of_symbols;

		if (ch->s->verbosity == 4) {
			printf("The number of SourceBlocks is: %u with %u symbols per source block\n", (unsigned int)ceil(nb_of_symbols / max_sb_len), nb_of_symbols);
			//printf("FDT max_sb_len: %d\n", ch->s->ls->fdt->max_sb_len);
			fflush(stdout);
		}


		if ((fec_enc_id == COM_NO_C_FEC_ENC_ID) && (numEncSymbPerPacket == 0)) //numEncSymbPerPacket = 0 means that it is varying with each packet
		{
			nb_of_iterations = 1;			// <In this case, decode whole payload at once>
			//nb_of_symb_to_decode_simult = nb_of_symbols;
			//printf("The number of symbols per source block is: %u\n", nb_of_symbols);
			//fflush(stdout);
		}


		/* Now we have to go through each symbol */
		if (ch->s->verbosity == 4) {
			printf("Payload: %i\tSymbol Length: %i\t#Symbols: %i\t#Iterations: %i\tFEC EncId: %i\n", len - hdrlen, es_len, nb_of_symbols, nb_of_iterations, fec_enc_id);
			fflush(stdout);
		}


		//for(j = 0; j < nb_of_symbols; j++) {
		for (j = 0; j < nb_of_iterations; j++) {
			//End


			/* Retrieve a transport unit from the session pool  */
			//printf("Retreive transport unit from session %d\n", ch->s->s_id);
			//fflush(stdout);
			 
			// Process Source Block # and Symbol # here if NRT file has repair
			if (ch->ch_id != 0 && ch->s->ls->fecOTI != NULL) {	// This packet has associated repair bits
				char* endptr;

#ifdef _MSC_VER
				unsigned char oti_common[16], oti_scheme[8], z[2], t[4], f[10];

				strncpy(oti_common, ch->s->ls->fecOTI, 16);	// Read 16 characters, 64 bits
				strncpy(f, oti_common, 10);
				uint64_t fx = strtoull(f, &endptr, 16);		// Transfer Length (transfer_len)
				strncpy(t, oti_common + 12, 4);
				uint32_t tx = strtoul(t, &endptr, 16);		// Symbol size (es_len)
				strncpy(oti_scheme, ch->s->ls->fecOTI + 16, 8);	// Read last 8 characters = lower 32 bits
				strncpy(z, oti_scheme, 2);
				uint32_t zx = strtoul(z, &endptr, 16);		// Total # of Source Blocks
#else
				unsigned char oti_common[17], oti_scheme[9], z[3], t[5], f[11];
				
				strncpy(oti_common, ch->s->ls->fecOTI, 16);	// Read 16 characters, 64 bits
				oti_common[16] = '\0';
				strncpy(f, oti_common, 10);
				f[10] = '\0';
				uint64_t fx = strtoull(f, &endptr, 16);		// Transfer Length (transfer_len)
				strncpy(t, oti_common + 12, 4);
				t[4] = '\0';
				uint32_t tx = strtoul(t, &endptr, 16);		// Symbol size (es_len)
				strncpy(oti_scheme, ch->s->ls->fecOTI + 16, 8);	// Read last 8 characters = lower 32 bits
				oti_scheme[8] = '\0';
				strncpy(z, oti_scheme, 2);
				z[2] = '\0';
				uint32_t zx = strtoul(z, &endptr, 16);		// Total # of Source Blocks
#endif
				uint32_t symcnt = (unsigned int)(fx / tx) + 1;		// transfer_len / es_len
				uint32_t IL = (symcnt / zx) + 1;	// length of long source blocks
				uint32_t IS = (symcnt / zx);		// length of short source blocks
				uint32_t JL = symcnt - IS * zx;		// # of long source blocks
				uint32_t JS = zx - JL;				// # of short source blocks
				int esiz = (start_offset / tx);
				int sb_symcnt = 0;
				int sbnz = 0;

				// Refinement
				for(uint32_t i=0; i < zx; i++) {
					if (i < JL) sb_symcnt = IL;
					if (i - JL < JS) sb_symcnt = IS;

					//printf("sb_symcnt %u\n", sb_symcnt);
					//fflush(stdout);
					esiz -= sb_symcnt;

					if (esiz < 0) {
						esiz += sb_symcnt;
						sbnz = i;
						break;
					}
				}
				start_offset += (start_offset / tx) * 4;
				transfer_len += (transfer_len / tx + 1) * 4;
				es_len += 4;

				if (ch->s->verbosity == 4) {
					//printf("TSI %lld, TOI %lld has Start_Offset: %u, Transfer Length: %llu, Max SB Len: %u\n", tsi, toi, start_offset, transfer_len, max_sb_len);
					//printf("FECOTI %s, start %u, es_len %u, sbn %u, esi %u, sb_symcnt %u\n", ch->s->ls->fecOTI, start_offset, es_len, sbnz, esiz, sb_symcnt);
					printf("TransferLen %llu\tStart Offset %u\tsymcnt %u\tsbn %u\tesi %u\tunit len %d\n", transfer_len, start_offset, symcnt, sbnz, esiz, es_len);
					//printf("IL %u\tIS %u\tJL %u\tJS %u\n", IL, IS, JL, JS);
					fflush(stdout);
				}

				// Prepare Source Block #, Symbol # header for repair bits
				tag = ((sbnz & 0xff) << 24) | (esiz & 0x00ffffff);
				//printf("tag: %x\n", tag);
				//fflush(stdout);

			}
			
			//Malek El Khatib 11.08.2014
			//trans_unit = retrieve_unit(ch->s, es_len);
			//printf("ES_LEN: %d\tTRANSFER_LEN: %lld\tLENGTH %d\n", es_len, transfer_len, (unsigned short)MAX_SYMB_LENGTH_IPv4_FEC_ID_0_3_130);
			//fflush(stdout);
			if (es_len > (unsigned short)MAX_SYMB_LENGTH_IPv4_FEC_ID_0_3_130) {	// Trick to work with different senders
				numEncSymbPerPacket = 1;
			}
			if (numEncSymbPerPacket == 0)
			{
				if (ch->s->addr_family == PF_INET)
				{
#ifdef USE_RETRIEVE_UNIT
					trans_unit = retrieve_unit(ch->s, (unsigned short)MAX_SYMB_LENGTH_IPv4_FEC_ID_0_3_130);
#else
					trans_unit = create_units(1);
#endif
				}
				else if (ch->s->addr_family == PF_INET6) //NOT TESTED
#ifdef USE_RETRIEVE_UNIT
					trans_unit = retrieve_unit(ch->s, (unsigned short)MAX_SYMB_LENGTH_IPv6_FEC_ID_0_3_130);
#else
					trans_unit = create_units(1);
#endif
			}
#ifdef USE_RETRIEVE_UNIT
			else trans_unit = retrieve_unit(ch->s, es_len);
#else
			// Create transport unit
			else trans_unit = create_units(1);
#endif

			if (ch->s->verbosity == 4) {
				printf("Retrieved Unit\n");
				fflush(stdout);
			}

			if (trans_unit == NULL) {
				if (ch->s->verbosity == 4) {
					printf("transport unit is NULL, memory error\n");
					fflush(stdout);
				}
				unlock_lct_header();

				return MEM_ERROR;
			}

			// Record symbol location
			trans_unit->esi = esi + j;

			// Record symbol length
			//Malek El Khatib 11.08.2014
			//trans_unit->len = es_len;
			if (numEncSymbPerPacket == 0)
				trans_unit->len = (unsigned short)(len - hdrlen);
			else
				trans_unit->len = es_len;

			//END
			if (ch->s->verbosity == 4) {
				printf("tranport unit length: %d\n", trans_unit->len);
				fflush(stdout);
			}

#ifndef USE_RETRIEVE_UNIT
			/* Alloc memory for incoming TU data */
			if (!(trans_unit->data = (char*)calloc(es_len, sizeof(char)))) {
				printf("Could not alloc memory for transport unit's data!\n");
				fflush(stdout);
				unlock_lct_header();

				return MEM_ERROR;
			}
#endif
			// Slip in Source Block # and Symbol # here if NRT file has repair
			if (ch->ch_id != 0 && ch->s->ls->fecOTI != NULL) {	// This packet has associated repair bits	
				//printf("Trans Unit ESI: %u\tStart offset: %u\n", trans_unit->esi, start_offset);
				//fflush(stdout);
				//trans_unit->esi = start_offset;

				// Copy tag into transport unit memory
				memcpy(trans_unit->data, &tag, 4);
			
				// Copy datagram into transport unit memory
				memcpy(trans_unit->data + 4, (data + hdrlen + j * es_len), trans_unit->len - 4);
			}
			else {
				// Copy datagram into transport unit memory
				memcpy(trans_unit->data, (data + hdrlen + j * es_len), trans_unit->len);
			}
			
			if (ch->s->verbosity == 4) {
				printf("%d memory copied transport unit in TOI: %llu\n", trans_unit->len, toi);
				fflush(stdout);
			}

			/* Check if object already exist */
			if (toi == FDT_TOI) {
				trans_obj = object_exist(fdt_instance_id, ch->s, 0);
			}
			else {	// Data file object
				trans_obj = object_exist(toi, ch->s, 1);
			}


			if (trans_obj == NULL) {

				trans_obj = create_object();

				if (trans_obj == NULL) {
					printf("transport object is NULL, memory error\n");
					fflush(stdout);
					unlock_lct_header();

					return MEM_ERROR;
				}

				if (toi == FDT_TOI) {
					trans_obj->toi = fdt_instance_id;
					trans_obj->content_enc_algo = content_enc_algo;

					if (ch->s->verbosity == 4) {
						printf("FDT TOI: %i\t FEC algorithm: %i\t for filename:%s\n", fdt_instance_id, content_enc_algo, trans_obj->tmp_filename);
						fflush(stdout);
					}
				}
				else {
					trans_obj->toi = toi;

					if (ch->s->rx_memory_mode == 1 || ch->s->rx_memory_mode == 2) { // Memory Mode is Medium(1) or Low(2)

						memset(filename, 0, MAX_PATH_LENGTH);

						if (content_enc_algo == 0) {
							sprintf(filename, "%s/%s", ch->s->base_dir, "object_XXXXXX");
							mktemp(filename);
						}
#ifdef USE_ZLIB
						else if (content_enc_algo == GZIP) {
							sprintf(filename, "%s/%s", ch->s->base_dir, "object_XXXXXX");
							mktemp(filename);
							strcat(filename, GZ_SUFFIX);
						}
#endif
						else if (content_enc_algo == PAD) {
							sprintf(filename, "%s/%s", ch->s->base_dir, "object_XXXXXX");
							mktemp(filename);
							strcat(filename, PAD_SUFFIX);
						}

						/* Alloc memory for tmp_filename */
						if (!(trans_obj->tmp_filename = (char*)calloc(strlen(filename) + 1, sizeof(char)))) {
							printf("Could not alloc memory for tmp_filename!\n");
							fflush(stdout);
							unlock_lct_header();

							return MEM_ERROR;
						}

						memcpy(trans_obj->tmp_filename, filename, strlen(filename));

						// Luke Fay 
#ifdef _MSC_VER
						if ((trans_obj->fd = open((const char*)trans_obj->tmp_filename,
							_O_WRONLY | _O_CREAT | _O_BINARY | _O_TRUNC, _S_IREAD | _S_IWRITE)) < 0) {
							// Luke Fay
#else
						if ((trans_obj->fd = open64((const char*)trans_obj->tmp_filename,
							O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU)) < 0) {
							//Luke Fay
#endif
							printf("Error: unable to open file %s\n", trans_obj->tmp_filename);
							fflush(stdout);
							unlock_lct_header();

							return MEM_ERROR;
						}
					}

					if (ch->s->rx_memory_mode == 2) {	// Memory Mode is Low(2)

						/* when receiver is in low memory mode a tmp file is used to store the data symbols */

						memset(filename, 0, MAX_PATH_LENGTH);
						sprintf(filename, "%s/%s", ch->s->base_dir, "st_XXXXXX");
						mktemp(filename);

						/* Alloc memory for tmp_st_filename */
						if (!(trans_obj->tmp_st_filename = (char*)calloc(strlen(filename) + 1, sizeof(char)))) {
							printf("Could not alloc memory for tmp_st_filename!\n");
							fflush(stdout);
							unlock_lct_header();

							return MEM_ERROR;
						}

						memcpy(trans_obj->tmp_st_filename, filename, strlen(filename));

						// Luke Fay 
#ifdef _MSC_VER
						if ((trans_obj->fd_st = open((const char*)trans_obj->tmp_st_filename,
							_O_RDWR | _O_CREAT | _O_BINARY | _O_TRUNC, _S_IREAD | _S_IWRITE)) < 0) {
							// Luke Fay 
#else
						if ((trans_obj->fd_st = open64((const char*)trans_obj->tmp_st_filename,
							O_RDWR | O_CREAT | O_TRUNC, S_IRWXU)) < 0) {
							// Luke Fay 
#endif
							printf("Error: unable to open file %s\n", trans_obj->tmp_st_filename);
							fflush(stdout);
							unlock_lct_header();

							return MEM_ERROR;
						}
					}

					if (ch->s->verbosity == 4) {
						printf("TOI: %lli\t FEC algorithm: %i\t for filename:%s\n", toi, content_enc_algo, trans_obj->tmp_filename);
						fflush(stdout);
					}
				}

				trans_obj->len = transfer_len;
				trans_obj->fec_enc_id = (unsigned char)fec_enc_id;
				trans_obj->fec_inst_id = (unsigned short)fec_inst_id;
				trans_obj->es_len = es_len;
				trans_obj->max_sb_len = max_sb_len;
				//trans_obj->es_len = len - hdrlen;
				//trans_obj->max_sb_len = len - hdrlen;

				//Malek El Khatib 11.08.2014
				if (ch->s->verbosity == 4) {
					printf("The new object length is: %llu\tSymbols %d\tSource Blocks %d\n", trans_obj->len, trans_obj->es_len, trans_obj->max_sb_len);
					fflush(stdout);
				}
				//End

				/* Let's calculate the blocking structure for this object */

				//trans_obj->bs = compute_blocking_structure(transfer_len, max_sb_len, es_len);
				trans_obj->bs = compute_blocking_structure(trans_obj->len, trans_obj->max_sb_len, trans_obj->es_len);
				if (ch->s->verbosity == 4) {
					printf("Calculated blocking structure with transfer len: %llu\tmax_sb: %d\tes: %d\n", trans_obj->len, trans_obj->max_sb_len, trans_obj->es_len);
					fflush(stdout);
				}

				// For ROUTE SRC_FLOW keep the number of Source blocks ready, as N is only available at Start Offset == 0 -- Luke Fay
				trans_obj->bs->N = nb_of_iterations;

				if (!(trans_obj->block_list = (trans_block_t*)calloc(trans_obj->bs->N, sizeof(trans_block_t)))) {
					printf("Could not alloc memory for transport block list!\n");
					fflush(stdout);
					unlock_lct_header();

					return MEM_ERROR;
				}

				if (toi == FDT_TOI) {
					insert_object(trans_obj, ch->s, 0);
				}
				else {
					insert_object(trans_obj, ch->s, 1);
				}
			}

			// Select correct Source Block
			trans_block = trans_obj->block_list + sbn;

			if (ch->s->verbosity == 4) {
				printf("xunit len %u\t xblock len %u\t xobject len %llu %u %u\n", trans_unit->len, trans_block->n, trans_obj->len, trans_obj->max_sb_len, trans_obj->es_len);
				printf("SBN: %d\t block structure I: %d\tA_large: %d\tA_small %d\n", sbn, trans_obj->bs->I, trans_obj->bs->A_large, trans_obj->bs->A_small);
				printf("# Rx symbols %u, # Rx Units %u, # Rx Blocks %u\n", trans_block->nb_of_rx_symbols, trans_block->nb_of_rx_units, trans_obj->nb_of_ready_blocks);
				fflush(stdout);
			}

			if (trans_block->nb_of_rx_units == 0) {  // If at the start of object reception, calculate # of units to expect
				trans_block->sbn = sbn;

				//Malek El Khatib 11.08.2014
				//trans_block->nb_of_rx_symbols = 0;
				//End
				if (fec_enc_id == COM_NO_C_FEC_ENC_ID) {

					if (sbn < trans_obj->bs->I) {
						trans_block->k = trans_obj->bs->A_large;
					}
					else {
						trans_block->k = trans_obj->bs->A_small;

					}
				}
				else if (fec_enc_id == RAPTOR_FEC_ENC_ID) {

					if (sbn < trans_obj->bs->I) {
						trans_block->k = trans_obj->bs->A_large;
					}
					else {
						trans_block->k = trans_obj->bs->A_small;
					}

				}
				else if (fec_enc_id == SB_SYS_FEC_ENC_ID) {

					trans_block->k = sb_len;
					trans_block->max_k = max_sb_len;
					trans_block->max_n = max_nb_of_es;
				}
				else if (fec_enc_id == SIMPLE_XOR_FEC_ENC_ID) {

					if (sbn < trans_obj->bs->I) {
						trans_block->k = trans_obj->bs->A_large;
					}
					else {
						trans_block->k = trans_obj->bs->A_small;
					}

					trans_block->max_k = max_sb_len;
				}
				else if (fec_enc_id == RS_FEC_ENC_ID) {

					if (sbn < trans_obj->bs->I) {
						trans_block->k = trans_obj->bs->A_large;
					}
					else {
						trans_block->k = trans_obj->bs->A_small;
					}

					trans_block->max_k = max_sb_len;
					trans_block->max_n = max_nb_of_es;

					/*trans_block->finite_field = finite_field;*/
				}
			}

			// Continually update the # of units
			if (fec_enc_id == COM_NO_C_FEC_ENC_ID) {

				if (sbn < trans_obj->bs->I) {
					trans_block->k = trans_obj->bs->A_large;
				}
				else {
					trans_block->k = trans_obj->bs->A_small;
					//trans_block->k = trans_block->nb_of_rx_units + ((transfer_len - start_offset) / trans_unit->len );
				}
			}
			
			if (ch->s->verbosity == 4) {
				//printf("Setting # of units A_large %u\tA_small %u\t # of Rx Symbols %u\n", trans_obj->bs->A_large, trans_obj->bs->A_small, trans_block->nb_of_rx_symbols);
				printf("Setting # of units A_large %u\tA_small %u\t # of Rx Units %u\n", trans_obj->bs->A_large, trans_obj->bs->A_small, trans_block->nb_of_rx_units);
				printf("# Rx symbols: %u, # Rx units %u K: %u\n", trans_block->nb_of_rx_symbols, trans_block->nb_of_rx_units, trans_block->k);
				fflush(stdout);
			}

			// Pull out # of Repair symbols.  Add this to # of already received Source Blocks 
			// Therefore always run Repair if #Source Block + #Repair Block >= #Expected Blocks
			//printf("TSI %llu has %u Repair Symbols\n", ch->s->tsi, ch->s->lost_packets);
			//fflush(stdout);
			trans_block->nb_of_rx_units += ch->s->lost_packets;

			if (!block_ready_to_decode(trans_block)) {
				if (ch->s->verbosity == 4) {
					printf("Block not ready to decode\n");
					fflush(stdout);
				}
				// If #Source Blocks + Repair blocks is not a full file, remove #Repair blocks
				trans_block->nb_of_rx_units -= ch->s->lost_packets;

				trans_unit->offset = start_offset;	// Out of Order delivery support.

				if (insert_unit(trans_unit, trans_block, trans_obj) != 1) {
					//printf("insert unit\n");
					//fflush(stdout);
 
					//Malek El Khatib
					trans_block->nb_of_rx_symbols += nb_of_symbols;
					//End

					if (toi == FDT_TOI || ch->s->rx_memory_mode == 0) {
						if (block_ready_to_decode(trans_block)) {
							trans_obj->nb_of_ready_blocks++;
							
							// PROCESS FDT
							//unlock_lct_header();

							//return OK;
						}
					}
					//cachePacket(toi,tsi,sbn,esi,trans_unit->data,(unsigned int)len - hdrlen);

					if (ch->s->verbosity == 4) {
						printf("Only now after LCT Header analysis is packet cached in buffer for TOI %llu\n", toi);
						fflush(stdout);
					}

					// Update the number of units
					update_blocking_structure(trans_obj->bs, transfer_len, max_sb_len, es_len);

					// Check blocking structure N count -- Luke Fay
					if (trans_obj->bs->N != 1) {
						//printf("Blocking structure computes N = %i with Xfer len %llu, SBN %u and symbol length %u\n", trans_obj->bs->N, transfer_len, max_sb_len, es_len);
						//fflush(stdout);

						trans_obj->bs->N = 1;	// Accouting for last small packets falsely signaling more blocks.
					}

					// if payload in low memory mode 2
					if (toi != FDT_TOI && ch->s->rx_memory_mode == 2) {	// Memory Mode is Low(2)
#if defined (_MSC_VER)
						trans_unit->offset = _lseeki64(trans_obj->fd_st, 0, SEEK_END);
#else
						trans_unit->offset = lseek64(trans_obj->fd_st, 0, SEEK_END);
#endif						
						if (trans_unit->offset == -1) {
							printf("lseek error, toi: %llu\n", toi);
							fflush(stdout);
							set_session_state(ch->s->s_id, SExiting);
							unlock_lct_header();

							return MEM_ERROR;
						}

						if (write(trans_obj->fd_st, trans_unit->data, (unsigned short)trans_unit->len) == -1) {
							printf("write error, toi: %llu, sbn: %i\n", toi, sbn);
							fflush(stdout);
							set_session_state(ch->s->s_id, SExiting);
							unlock_lct_header();

							return MEM_ERROR;
						}

						free(trans_unit->data);
						trans_unit->data = NULL;
					}

					if (ch->s->verbosity == 4) {
						printf("calculate percent object received bytes/len %llu/%llu\toffset %u\n", trans_obj->rx_bytes, trans_obj->len, start_offset);
						fflush(stdout);
					}

					if (((toi == FDT_TOI && ch->s->verbosity == 4) || (toi != FDT_TOI && ch->s->verbosity > 1))) {
						rx_percent = (double)((double)100 *
							((double)(long long)trans_obj->rx_bytes / (double)(long long)trans_obj->len));

						if (((rx_percent >= (trans_obj->last_print_rx_percent + 1)) || (rx_percent == 100))) {
							trans_obj->last_print_rx_percent = rx_percent;
							printf("%.2f%% of object received (TOI=%llu LAYERS=%i)\n", rx_percent,
								toi, ch->s->nb_channel);
							fflush(stdout);
						}
					}

				}


			}

			//printf("object %llu block units len %u\n", toi, trans_obj->block_list->unit_list->len);
			//fflush(stdout);

			if (toi != FDT_TOI) {
				if (ch->s->rx_memory_mode == 1 || ch->s->rx_memory_mode == 2) {	// Memory Mode is Medium(1) or Low(2)

					//if (block_ready_to_decode(trans_block) || close_object) {  // If full Source Blocks are available and have no Repair Flow data
					if (block_ready_to_decode(trans_block)) {  // If full Source Blocks are available and have no Repair Flow data

						//Malek El Khatib 11.08.2014
						if (ch->s->verbosity > 2) {
							printf("Start decoding %u units of k: %u n: %u in %u Rx Symbols\n", trans_block->nb_of_rx_units, trans_block->k, trans_block->n, trans_block->nb_of_rx_symbols);
							fflush(stdout);
						}
						//End

						if (ch->s->rx_memory_mode == 2) {	// Memory Mode is Low(2)

							/* We have to restore the data symbols to trans_units from the symbol store tmp file */

							next_tu = trans_block->unit_list;

							while (next_tu != NULL) {

								tu = next_tu;
#if defined (WIN32)
								if (_lseeki64(trans_obj->fd_st, tu->offset, SEEK_SET) == -1) {
#else 
								if (lseek64(trans_obj->fd_st, tu->offset, SEEK_SET) == -1) {
#endif 
									printf("alc_rx.c line 1035 lseek error, toi: %llu\n", toi);
									fflush(stdout);
									set_session_state(ch->s->s_id, SExiting);
									unlock_lct_header();

									return MEM_ERROR;
								}

								/* let's copy the data symbols from the tmp file to the memory */

								/* Alloc memory for restoring data symbol */

								if (!(tu->data = (char*)calloc(tu->len, sizeof(char)))) {
									printf("Could not alloc memory for transport unit's data!\n");
									unlock_lct_header();

									return MEM_ERROR;
								}

								if (read(trans_obj->fd_st, tu->data, tu->len) == -1) {
									printf("read error, toi: %llu, sbn: %i\n", toi, sbn);
									fflush(stdout);
									set_session_state(ch->s->s_id, SExiting);
									unlock_lct_header();

									return MEM_ERROR;
								}

								next_tu = tu->next;
							}
						}

						/* decode the block and save data to the tmp file */
						//printf("FEC ENC ID: %d with eslen %d\n", fec_enc_id, es_len);
						//printf("FEC ENC ID: %d with xfer len %lld and lost packets: %u\n", fec_enc_id, transfer_len, ch->s->lost_packets);
						//fflush(stdout);

						if (fec_enc_id == COM_NO_C_FEC_ENC_ID) {
							//buf = null_fec_decode_src_block(trans_block, &block_len, es_len);	// This is for FLUTE
							buf = null_fec_decode_src_block(trans_block, &block_len, transfer_len);	// This is for ROUTE
						}
						else if (fec_enc_id == SIMPLE_XOR_FEC_ENC_ID) {
							buf = xor_fec_decode_src_block(trans_block, &block_len, es_len);
						}
						else if (fec_enc_id == RS_FEC_ENC_ID) {
							buf = rs_fec_decode_src_block(trans_block, &block_len, es_len);
						}
						else if (fec_enc_id == SB_SYS_FEC_ENC_ID && fec_inst_id == REED_SOL_FEC_INST_ID) {
							buf = rs_fec_decode_src_block(trans_block, &block_len, es_len);
						}

						if (buf == NULL) {
							free(buf);
							unlock_lct_header();

							return MEM_ERROR;
						}

						/* We have to check if there is padding in the last source symbol of the last source block */

						if (trans_block->sbn == ((trans_obj->bs->N) - 1)) {
							block_len = (trans_obj->len - ((unsigned int)es_len * (trans_obj->bs->I * trans_obj->bs->A_large +
								(trans_obj->bs->N - trans_obj->bs->I - 1) * trans_obj->bs->A_small)));

						}

						if (trans_block->sbn < trans_obj->bs->I) {
							pos = ((unsigned long long)trans_block->sbn * (unsigned long long)trans_obj->bs->A_large * (unsigned long long)trans_obj->es_len);
						}
						else {
							pos = ((((unsigned long long)trans_obj->bs->I * (unsigned long long)trans_obj->bs->A_large) +
								((unsigned long long)trans_block->sbn - (unsigned long long)trans_obj->bs->I) *
								(unsigned long long)trans_obj->bs->A_small) * (unsigned long long)trans_obj->es_len);
						}

						/* set correct position */
						//printf("set correct position: %lld\tvs. Startoffset: %u\t block len %llu\n", pos, start_offset, block_len);
						//fflush(stdout);					
#if defined(_MSC_VER)
						if (_lseeki64(trans_obj->fd, pos, SEEK_SET) == -1) {
#else
						if (lseek64(trans_obj->fd, pos, SEEK_SET) == -1) {
#endif
							printf("alc_rx.c line 1111 lseek error, toi: %llu\n", toi);
							fflush(stdout);
							free(buf);
							set_session_state(ch->s->s_id, SExiting);
							unlock_lct_header();

							return MEM_ERROR;
						}

						if (write(trans_obj->fd, buf, (unsigned int)block_len) == -1) {
							printf("write error, toi: %llu, sbn: %i\n", toi, sbn);
							fflush(stdout);
							free(buf);
							set_session_state(ch->s->s_id, SExiting);
							unlock_lct_header();

							return MEM_ERROR;
						}

						trans_obj->nb_of_ready_blocks++;
						free(buf);

#ifdef USE_RETRIEVE_UNIT
						free_units2(trans_block);
						free_unit_container(ch->s);
#else
						free_units(trans_block);
#endif

						if (ch->s->verbosity > 2) {
							printf("%u/%u Source Blocks decoded (TOI=%llu SBN=%u)\n", trans_obj->nb_of_ready_blocks, trans_obj->bs->N, toi, sbn);
							fflush(stdout);
						}

					}

				}

			}
			
			//printf("End of analyzed packet for TOI: %lli\n", toi);
			//fflush(stdout);

		}
	}
	else { // We have an empty packet with FEC Payload ID
		printf("ALC Loop END ");
		fflush(stdout);

		unlock_lct_header();

		return EMPTY_PACKET;
	}

	unlock_lct_header();

	return OK;
}


void addPacket(unsigned long long toi, unsigned long long tsi, unsigned int sbn, unsigned int esi, char *buffer, int len)
{
    struct packetBuffer packet;
    int ret;
    packet.toi = toi;
    packet.tsi = tsi;
    packet.sbn = sbn;
    packet.esi = esi;
    packet.length = len;
    packet.buffer = (unsigned char *)buffer;
    ret = newWriteToBuffer(packet);
    if(ret < 0)
        fprintf(stdout,"Failure to write to circular packet buffer!!\n");
}

void cachePacket(unsigned long long toi, unsigned long long tsi, unsigned int sbn, unsigned int esi, char *buffer, int len)
{
    if(tunedIn == 0 && toi == 0)
        tunedIn = 1;
    
    if(tunedIn == 1)
    {
        if(workingPort == 4001 || workingPort == 4003)
        {
            if( toi%2 != 0 )
                tunedIn = 2;
        }
        else if(workingPort == 4002 || workingPort == 4004)
        {
            if( (toi - 2) % 3 == 0 )
                tunedIn = 2;
        }
    }
    
    if(tunedIn == 2)
    {
        if(toi == 0)
            tunedIn = 3;
        else
        {
            if(workingPort == 4001 || workingPort == 4003)
            {
                if( toi%2 != 0 )
                    addPacket(toi,tsi,sbn,esi,buffer,len);
            }
            else if(workingPort == 4002 || workingPort == 4004)
            {
                if( (toi - 2) % 3 == 0 )
                    addPacket(toi,tsi,sbn,esi,buffer,len);
            }
        }
            
    }
    
    if(tunedIn == 3)
    {
        if(workingPort == 4001 || workingPort == 4003)
        {
            if( toi%2 == 0 && toi != 0 )
                addPacket(toi,tsi,sbn,esi,buffer,len);
        }
        else if(workingPort == 4002 || workingPort == 4004)
        {
            if( (toi - 3) % 3 == 0 && toi != 0)
                addPacket(toi,tsi,sbn,esi,buffer,len);
        }
    }

}

/**
* This is a private function which receives unit(s) from the session's channels.
*
* @param s pointer to the session
*
* @return number of correct packets received from ALC session, or 0 when state is SClosed or no packets,
* or -1 in error cases, or -2 when state is SExiting
*
*/

int recv_packet(alc_session_t* s) {

	int recvlen;
	int i=0;
	int retval = 0;
	//int recv_pkts = 0;
	alc_channel_t* ch;
	lct_ch_t* ls;

	double loss_prob;
	int content_enc_algo = -1;

	alc_rcv_container_t* container;
	int my_list_not_empty = 0;

	time_t systime;
	unsigned long long curr_time;

	for (i = 0; i < s->nb_channel; i++) {
		ch = s->ch_list[i];

		if (ch->receiving_list != NULL) {
			if (!is_empty(ch->receiving_list)) {
				++my_list_not_empty;
				//break;
			}
		}

		if (my_list_not_empty == 0) {
			if (s->stoptime != 0) {
				time(&systime);
				curr_time = systime + 2208988800U;

				if (curr_time >= s->stoptime) {
					set_session_state(ch->s->s_id, SExiting);
					//s->state = SExiting;

					return -2;
				}
			}

#ifdef _MSC_VER
			//Sleep(0);	// While waiting for a new packet, sleep to save CPU usage.
			//SleepConditionVariableCS(&packet_ready, &lct_header_variables_semaphore, INFINITE);	// No timeout
			SleepConditionVariableCS(&packet_ready, &lct_header_variables_semaphore, 1);	// Timeout of 1 msec
#else
			//usleep(1000);
			lock_lct_header();
			if (!packet_ready) {
				pthread_cond_wait(&cond, &lct_header_variables_semaphore);	// Wait for packet being available
			}
			else packet_ready = FALSE;
			unlock_lct_header();
#endif

			if (s->state == SAFlagReceived) {
				set_session_state(ch->s->s_id, STxStopped);
				//s->state = STxStopped;
			}

			//return 0;
			continue;
		}

		ls = s->ls;
		//printf("channel: %u (%u) of %u\n", i, ch->ch_id, s->nb_channel);
		//fflush(stdout);
		loss_prob = 0;

		if (ch->s->simul_losses) {
			if (ch->previous_lost == TRUE) {
				loss_prob = ch->s->loss_ratio2;
			}
			else {
				loss_prob = ch->s->loss_ratio1;
			}
		}

		assert(ch->rx_socket_thread_id != 0);

		container = (alc_rcv_container_t*)pop_front(ch->receiving_list);  // Pull out of container each datagram for analysis
		//container = (alc_rcv_container_t*)ch->receiving_list->first_elem->next->data;

		//printf("Pop front current channel %d in session %d\tTSI: %lld\trecvlen: %d\n", ch->ch_id, s->s_id, ch->s->tsi, container->recvlen);
		//fflush(stdout);

		assert(container != NULL); // Ensure it is not empty while we look at the packet

		recvlen = container->recvlen;	// Typically 1472 bytes
		//from = container->from;
		//fromlen = container->fromlen;

		if (recvlen <= 0) {

			free(container);
			container = NULL;

			if (s->state == SExiting) {
				printf("recv_packet() SExiting\n");
				fflush(stdout);

				unlock_lct_header();
				return -2;
			}
			else if (s->state == SClosed) {
				printf("recv_packet() SClosed\n");
				fflush(stdout);

				unlock_lct_header();
				return 0;
			}
			else {
				printf("recvfrom failed: %d\n", errno);
				fflush(stdout);

				unlock_lct_header();
				return -1;
			}
		}
		else {
			unsigned long long toi;

			retval = analyze_packet(container->recvbuf, recvlen, &toi, ch);
			//printf("analyze packet returns: %d\n", retval);
			//fflush(stdout);
			free(container);
			container = NULL;

			if (ch->s->cc_id == RLC) {
				
				if (((ch->s->rlc->drop_highest_layer) && (ch->s->nb_channel != 1))) {
				
					ch->s->rlc->drop_highest_layer = FALSE;
					close_alc_channel(ch->s->ch_list[ch->s->nb_channel - 1], ch->s);
				}
			}

			switch (retval) {
			case WAITING_FDT:
				//wait_cnt++;
				//Malek El Khatib 16.07.2014
				//Start
				//printf("DOES WAITING_FDT EVER HAPPPEN?\n");
				//fflush(stdout);
				//if (sendFDTAfterObj)
				//	push_back(ch->receiving_list, (void*)container);
				//else //END
				//	push_front(ch->receiving_list, (void*)container);
				//ch->previous_lost = FALSE;
				continue;
				// return 0;

				break;
			case DUP_PACKET:
				//printf("Duplicate FDT packet seen\n");
				//fflush(stdout);
				//ch->previous_lost = FALSE;
				//continue;	// Continue looking for applicable packet
				return 0;

				break;
			case MEM_ERROR:
				printf("Memory Error\n");
				fflush(stdout);

				return -1;

				break;
			case HDR_ERROR:
				printf("LCT Header Error\n");
				fflush(stdout);

				return -1;

				break;
			case EMPTY_PACKET:
				//printf("Packet is to a different LCT channel\n");
				//fflush(stdout);
				continue;	// Continue looking for applicable packet
				//return 0;

				break;
			case OK:
				// Packet to current LCT channel
				//recv_pkts++;
				//ch->previous_lost = FALSE;
				// continue;

				break;
			case REPAIR:
				//printf("REPAIR filename: %s\n", ls->location);
				//fflush(stdout);
				continue;
				//return 0;

				break;
			default:
				break;
			}

		}
		//else {
		//	printf("DOES IT EVER GET HERE TO SIMULATE LOSSES?\n");
		//	ch->previous_lost = TRUE;
		//
		//	free(container);
		//	container = NULL;
		//}

		// If receiving list is empty, continue to the next TSI
		//printf("channel Rx list is empty\n"); fflush(stdout);

	}

	return 0;
	//return recv_pkts;

}


void* rx_socket_thread(void *ch) {

	alc_channel_t *channel;
	alc_rcv_container_t *container;
	fd_set read_set;
	struct timeval time_out;
	char hostname[100];
	int retval;
	//unsigned long long id;

	/* FRV: Unused variables. They seem to be used only in case of placing packets
	 * in some sort of circular buffer that i have no idea what it does.
	 * It seems to be related to the test-server.c file
	 */
#ifdef CIRCULAR_BUFFER
	int index;
    int hdrlen = 0;         /* length of whole FLUTE/ALC/LCT header */
    unsigned long long tsi = 0; /* TSI */
    unsigned long long toi = 0; /* TOI */
    unsigned int sbn = 0;
    unsigned int esi = 0;
	char* tempBuf;
    tempBuf = malloc(MAX_PACKET_LENGTH);
#endif

	channel = (alc_channel_t *)ch;

	//Malek El Khatib 04.04.2014
	//Start
	struct timeval socket_time;
	unsigned long long timeInUsec = 0L;		//Used later for timing purposes
	//End
	
	/*
	if(workingPort == 4001 || workingPort == 4003)
	{
		FILE * tempff = fopen("bufferLog.txt","w");
		fclose(tempff);
		tempff = fopen("bufferLogRead.txt","w");
		fclose(tempff);
	}
	*/
	
	/*
	for(index = 0 ; index < circularBufferLength ; index ++)
	{
		circularPacketBuffer[index].buffer = malloc(MAX_PACKET_LENGTH);
        circularPacketBuffer[index].occupied = FALSE;
		if(circularPacketBuffer[index].buffer == NULL)
		{
			fprintf(stderr,"Error creating buffer, exiting!!\n");
			exit(-1);
		}
	}*/
	

	while(channel->s->state == SActive) {

		time_out.tv_sec = 1;
		time_out.tv_usec = 0;

		FD_ZERO(&read_set);
		FD_SET(channel->rx_sock, &read_set);

		retval = select((int)channel->rx_sock+1, &read_set, 0, 0, &time_out);	// Select waits for an event on ethernet, CPU sleeps otherwise
		// NOTE: rx_sock + 1 is a Linux thing and doesn't affect Windows
		//printf("Channel %d Rx socket thread returns %d\n", channel->ch_id, retval); 
		//printf("Rx Socket %d has Channel %d thread returns %d\n", channel->rx_sock, channel->ch_id, retval);
		//fflush(stdout);

		if(retval > 0) {
			if(!(container = (alc_rcv_container_t*)calloc(1, sizeof(alc_rcv_container_t)))) {
				printf("Could not alloc memory for container! %d\n", sizeof(container));
				fflush(stdout);
				free(container);

				continue;
			}

			container->fromlen = (channel->s->addr_family == PF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
			
			// Recvfrom is called only after select finds an event and then blocks to get packet...CPU efficient
			container->recvlen = recvfrom(channel->rx_sock, container->recvbuf, MAX_PACKET_LENGTH, 0, (struct sockaddr*)&(container->from), &(container->fromlen));	
			//container->recvlen = recv(channel->rx_sock, container->recvbuf, MAX_PACKET_LENGTH,0);

			if (container->recvlen == 0) {
				printf("connection closed\n"); 
				fflush(stdout);
				free(container);
				break;
				//return 0;
			}
			else if (container->recvlen < 0) {
				printf("recv failed\n"); 
				fflush(stdout);
				free(container);

				continue; // Tolerate packet loss
			}
			//Malek El Khatib 12.05.2014
			//Start
			else if (container->recvlen > 0) {	//Start of FDT Instance Reception
				gettimeofday(&socket_time, NULL);
				def_lct_hdr_t* header = NULL;
				//printf("trying to allocate %d memory but can only set fixed length of %d\n", container->recvlen, MAX_PACKET_LENGTH);
				//fflush(stdout);
				/*  Following is only for logging. (memcpy slows packet processing)
//				char received[container->recvlen];
				char received[MAX_PACKET_LENGTH];
				memset(received, 0, container->recvlen);
				memcpy(received, container->recvbuf, container->recvlen);
				header = (def_lct_hdr_t*)received;
				*(unsigned short*)header = ntohs(*(unsigned short*)header);
				int headerLen = (int)(sizeof(def_lct_hdr_t)) + 4;
				id = ntohl(*(unsigned int*)((char*)header + headerLen));
				if (id == 0) {
					timeInUsec = (unsigned long long)socket_time.tv_sec * 1000000 + (unsigned long long)socket_time.tv_usec;
					fprintf(logFilePtr, "PACKET Reception %llu\n", timeInUsec);
				}
				*/
			}

			getnameinfo((struct sockaddr*)&(container->from), container->fromlen,
				hostname, sizeof(hostname), NULL, 0, NI_NUMERICHOST);
				
			if (strcmp(channel->s->src_addr, "") != 0) {

				if (strcmp(hostname, channel->s->src_addr) != 0) {
					printf("Packet to wrong session: %s\twrong source: %s\n", hostname, channel->s->src_addr);
					fflush(stdout);

					//Malek El Khatib 07.08.2014
					//Should not we clear container here? On the next loop iteration, container will get new value
					//and the allocated memory would no longer be reachable
					printf("Freeing recv container due to wrong source address\n");
					free(container);
					//End
					break;
					//return 0;
				}
			}
			else { // (strcmp(channel->s->src_addr, "") == 0) {

				if (channel->s->verbosity > 0) {
					printf("Locked to source: %s\n", hostname);
					fflush(stdout);
				}

				memcpy(channel->s->src_addr, hostname, strlen(hostname));
			}

			// Store received packet into container
			push_back(channel->receiving_list, (void*)container);

#ifdef _MSC_VER
			WakeConditionVariable(&packet_ready);
#else
			packet_ready = TRUE;
			pthread_cond_signal(&cond);	// Signal that packets are available
#endif

            /*memcpy(tempBuf,container->recvbuf,container->recvlen);

            retval = parse_packet(tempBuf, container->recvlen,&hdrlen, &toi,&tsi,&sbn,&esi, channel);
            if(workingPort == 4001 || workingPort == 4003)
            {       
            
            static unsigned long long stoi = 0;
            static unsigned long long stsi = 0;
            static unsigned int ssbn = 0;
            static unsigned int sesi = 0;
                FILE * tempff = fopen("bufferLog.txt","a");
            
                fprintf(tempff,"TEST: toi %llu, tsi %llu, sbn %u, esi %u ",toi,tsi,sbn,esi);
                if(toi != 0 && stoi != 0 && (toi != stoi || tsi != stsi || sbn != ssbn || (esi - sesi) > 1))
                    fprintf(tempff,"<==========================");
                fprintf(tempff,"\n");
                fclose(tempff);
                sesi = esi;
                ssbn = sbn;
                stsi = tsi;
                stoi = toi;
            }*/

		}
		else if (retval == 0) {
			printf("Socket Connection timeout.\n");
			fflush(stdout);
		}
		else {
			if (channel->s->verbosity == 4) {
				printf("Lost packets in socket connection.\n");
				fflush(stdout);
			}

			continue; // Tolerate some packet loss past timer of Select command
		}

		retval = 0;	// Reset thread synchronization trigger
	}
	if (channel->s->verbosity == 4) {
		printf("Socket connection is no longer active, exiting thread\n");
		fflush(stdout);
	}

#ifdef _MSC_VER
	_endthread();
#else
	pthread_exit(0);
#endif

	return NULL;
}

void join_rx_socket_thread(alc_channel_t *ch) {

#ifndef _MSC_VER //Luke Fay
	int join_retval;
#endif // Luke Fay
	
	if(ch != NULL) {
#ifdef _MSC_VER // Luke Fay
		WaitForSingleObject(ch->handle_rx_socket_thread, INFINITE);
		CloseHandle(ch->handle_rx_socket_thread);
#else // Luke Fay
		join_retval = pthread_join(ch->rx_socket_thread_id, NULL);
		assert(join_retval == 0);
		pthread_detach(ch->rx_socket_thread_id);
#endif // Luke Fay
	}
}

void* rx_thread(void *s) {

	alc_session_t *session;
	int retval = 0;

	srand((unsigned)time(NULL));

	session = (alc_session_t *)s;

	while(session->state == SActive || session->state == SAFlagReceived) {

#ifdef _MSC_VER
		Sleep(0);
#else
		usleep(1000);
#endif
		
		if(session->nb_channel != 0) {
			retval = recv_packet(session);

			if (retval != 0) {
			//if (retval == -1) {
				printf("RX SESSION THREAD returns: %d\n", retval);
				fflush(stdout);

				set_session_state(session->s_id, SExiting);
			}
		}
	}
	if (session->verbosity == 4) {
		printf("Session %d is no longer active.  Recv_packet stopped, exit thread\n", session->s_id);
		fflush(stdout);
	}

#ifdef _MSC_VER
	_endthread();
#else
	pthread_exit(0);
#endif

	return NULL;
}

char* alc_recv(int s_id, unsigned long long toi, unsigned long long *data_len, int *retval) {

	BOOL obj_completed = FALSE;
	alc_session_t *s;
	char *buf = NULL; /* Buffer where to construct the object from data units */
	trans_obj_t *to;
	int object_exists = 0;

	s = get_alc_session(s_id);

	while(!obj_completed) {

		if(s->state == SExiting) {
			printf("alc_recv() SExiting\n");
			fflush(stdout);
			*retval = -2;
			return NULL;	
		}
		else if(s->state == SClosed) {
			printf("alc_recv() SClosed\n");
			fflush(stdout);
			*retval = 0;
			return NULL;	
		}

		to = s->obj_list;

		if(!object_exists) {

			while(to != NULL) {
				if(to->toi == toi) {
					object_exists = 1;
					break;
				}
				to = to->next;
			}


#ifdef _MSC_VER
			Sleep(1);
#else
			usleep(1000);
#endif
		}

		obj_completed = object_completed(to);

		if(((s->state == STxStopped) && (!obj_completed))) {
			printf("alc_recv() STxStopped, toi: %llu\n", toi);
			fflush(stdout);
			*retval = -3;
			return NULL;	
		}

		continue;

	}

	remove_wanted_object(s_id, toi);

	/* Parse data from object to data buffer, return buffer and buffer length */

	to = object_exist(toi, s, 1);

	if(to->fec_enc_id == COM_NO_C_FEC_ENC_ID) {
		buf = null_fec_decode_object(to, data_len, s);
	}
	else if(to->fec_enc_id == SIMPLE_XOR_FEC_ENC_ID) {
		buf = xor_fec_decode_object(to, data_len, s);
	}
	else if(to->fec_enc_id == RS_FEC_ENC_ID) {
		buf = rs_fec_decode_object(to, data_len, s);
	}
	else if(to->fec_enc_id == SB_SYS_FEC_ENC_ID && to->fec_inst_id == REED_SOL_FEC_INST_ID) {
		buf = rs_fec_decode_object(to, data_len, s);
	}

	if(buf == NULL) {
		*retval = -1;
	}

	free_object(to, s, 1);
	return buf;
}

char* alc_recv2(int s_id, unsigned long long *toi, unsigned long long *data_len, int *retval) {

	BOOL obj_completed = FALSE;
	alc_session_t *s;

	unsigned long long tmp_toi = 0;

	char *buf = NULL; /* Buffer where to construct the object from data units */
	trans_obj_t *to;

	s = get_alc_session(s_id);

	if (s->verbosity == 4) {
		printf("ALC_RECV2 Session ID: %d\n", s->s_id);
		fflush(stdout);
	}

	while(!obj_completed) {

		to = s->obj_list;

		if(s->state == SExiting) {
			printf("alc_recv2() SExiting\n");
			fflush(stdout);
			*retval = -2;
			return NULL;	
		}
		else if(s->state == SClosed) {
			printf("alc_recv2() SClosed\n");
			fflush(stdout);
			*retval = 0;
			return NULL;	
		}
		else if(((s->state == STxStopped) && (to == NULL))) {
			printf("alc_recv2() STxStopped\n");
			fflush(stdout);
			*retval = -3;
			return NULL;	
		}

		//printf("Number of received objects: %d\n", s->rx_objs);
		//fflush(stdout);

#ifdef _MSC_VER
		Sleep(1);	// This sleep helps reduce CPU usage
#else
		usleep(1000);
#endif

		while(to != NULL) {

			if(s->state == SExiting) {
				printf("alc_recv2() SExiting\n");
				fflush(stdout);
				*retval = -2;
				return NULL;	
			}
			else if(s->state == SClosed) {
				printf("alc_recv2() SClosed\n");
				fflush(stdout);
				*retval = 0;
				return NULL;	
			}
			else {
				obj_completed = object_completed(to);
				//printf("Object completed: %d\n", obj_completed);
				//fflush(stdout);
			}

			if(obj_completed) {
				tmp_toi = to->toi;
				break;
			}

			if(((s->state == STxStopped) && (!obj_completed))) {
				printf("alc_recv2() STxStopped\n");
				fflush(stdout);
				*retval = -3;
				return NULL;	
			}

			to = to->next;
		}

		continue;

	}
	if (s->verbosity == 4) {
		printf("alc_recv2 object recovered\n\n");
		fflush(stdout);
	}

	remove_wanted_object(s_id, tmp_toi);

	/* Parse data from object to data buffer, return buffer length */

	to = object_exist(tmp_toi, s, 1);
	//printf("Parsed data %llu from object to data buffer\n", *data_len);
	//fflush(stdout);

	if(to->fec_enc_id == COM_NO_C_FEC_ENC_ID) {
		buf = null_fec_decode_object(to, data_len, s);
	}
	else if(to->fec_enc_id == SIMPLE_XOR_FEC_ENC_ID) {
		buf = xor_fec_decode_object(to, data_len, s);
	}
	else if(to->fec_enc_id == RS_FEC_ENC_ID) {
		buf = rs_fec_decode_object(to, data_len, s);
	}
	else if(to->fec_enc_id == SB_SYS_FEC_ENC_ID && to->fec_inst_id == REED_SOL_FEC_INST_ID) {
		buf = rs_fec_decode_object(to, data_len, s);
	}

	//printf("Decoded object\n");
	//fflush(stdout);

	if(buf == NULL) {
		*retval = -1;
	}
	else {
		*toi = tmp_toi;
	}
 
	free_object(to, s, 1);

	//printf("Returning ALC\n");
	//fflush(stdout);

	return buf;
}

char* alc_recv3(int s_id, unsigned long long *toi, int *retval) {

	BOOL obj_completed = FALSE;
	alc_session_t *s;

	unsigned long long tmp_toi = 0;

	trans_obj_t *to;
	char *tmp_filename = NULL;

	s = get_alc_session(s_id);

	if (s->verbosity == 4) {
		printf("ALC_RECV3 Session ID: %d\n", s->s_id);
		fflush(stdout);
	}

	while (!obj_completed) {

		to = s->obj_list;

		//printf("Processing session %d\n", s_id);
		//fflush(stdout);

		if(s->state == SExiting) {
			printf("alc_recv3() SExiting\n");
			fflush(stdout);
			*retval = -2;

			return NULL;	
		}
		else if(s->state == SClosed) {
			printf("alc_recv3() SClosed\n");
			fflush(stdout);
			*retval = 0;

			return NULL;	
		}
		else if(((s->state == STxStopped) && (to == NULL))) {
			printf("alc_recv3() STxStopped, to == NULL\n");
			fflush(stdout);
			*retval = -3;

			return NULL;	
		}
		
		//printf("Number of received objects: %d\n", s->rx_objs);
		//fflush(stdout);

#ifdef _MSC_VER
		if (to == NULL) {
			Sleep(1);
			//SleepConditionVariableCS(&packet_ready, &lct_header_variables_semaphore, 1);	// Timeout of 1 msec
		}
#else
			usleep(1000);
#endif

		while(to != NULL) {

			obj_completed = object_completed(to);

			if(obj_completed) {
				tmp_toi = to->toi;
				break;
			}

			to = to->next;
		}

	}
	if (s->verbosity == 4) {
		printf("alc_recv3 object recovered\n\n");
		fflush(stdout);
	}

	remove_wanted_object(s_id, tmp_toi);

	if(!(tmp_filename = (char*)calloc((strlen(to->tmp_filename) + 1), sizeof(char)))) {
		printf("Could not alloc memory for tmp_filename!\n");
		*retval = -1;

		return NULL;    
	}

	memcpy(tmp_filename, to->tmp_filename, strlen(to->tmp_filename));

	if (s->verbosity == 4) {
		printf("Free Objects\n");
		fflush(stdout);
	}
	free_object(to, s, 1);
	*toi = tmp_toi;

	return tmp_filename;
}

char* fdt_recv(int s_id, unsigned long long *data_len, int *retval,
	unsigned char *content_enc_algo, int* fdt_instance_id) {

	alc_session_t *s;                                                                                                                                          
	char *buf = NULL; /* Buffer where to construct the object from data units */                                                                                                                                     
	trans_obj_t *to;

	s = get_alc_session(s_id);

	while(s->state == SActive) {
		to = s->fdt_list;

		if(s->state == SExiting) {
			printf("fdt_recv() SExiting\n");
			fflush(stdout);
			*retval = -2;

			return NULL;
		}
		else if(s->state == SClosed) {
			printf("fdt_recv() SClosed\n");
			fflush(stdout);
			*retval = 0;

			return NULL;
		}
		else if(s->state == STxStopped) {
			printf("fdt_recv() STxStopped\n");
			fflush(stdout);
			*retval = -3;

			return NULL;	
		}

		if(to == NULL) {

#ifdef _MSC_VER
			//Sleep(1);	// This sleep helps reduce CPU usage
			SleepConditionVariableCS(&packet_ready, &lct_header_variables_semaphore, 1);	// Timeout of 1 msec
			//printf("NO FDT LIST in session %d\n", s_id);
			//fflush(stdout);
#else
			usleep(1000);
			//printf("Waiting for FDT in session %d\n", s_id);
			//fflush(stdout);
#endif
			continue;
		}

		do {

			if(object_completed(to)) {

				set_received_instance(s, (unsigned int)to->toi);

				*content_enc_algo = to->content_enc_algo;
				*fdt_instance_id = (int)to->toi;
				
				if(to->fec_enc_id == COM_NO_C_FEC_ENC_ID) {
					buf = null_fec_decode_object(to, data_len, s);
				}
				else if(to->fec_enc_id == SIMPLE_XOR_FEC_ENC_ID) {
					buf = xor_fec_decode_object(to, data_len, s);
				}
				else if(to->fec_enc_id == RS_FEC_ENC_ID) {
					buf = rs_fec_decode_object(to, data_len, s);
				}
				else if(to->fec_enc_id == SB_SYS_FEC_ENC_ID && to->fec_inst_id == REED_SOL_FEC_INST_ID) {
					buf = rs_fec_decode_object(to, data_len, s);
				}

				if(buf == NULL) {
					*retval = -1;
				}

				free_object(to, s, 0);

				return buf;
			}
			to = to->next;

		} 
		while(to != NULL);

	}

	return buf;
}

trans_obj_t* object_exist(unsigned long long toi, alc_session_t *s, int type) {

	trans_obj_t *trans_obj = NULL;

	if(type == 0) {
		trans_obj = s->fdt_list;
	}
	else if(type == 1) {
		trans_obj = s->obj_list;
	}

	if(trans_obj != NULL) {
		for(;;) {
			if(trans_obj->toi == toi) {
				break;
			}
			if(trans_obj->next == NULL) {
				trans_obj = NULL;
				break;
			}
			trans_obj = trans_obj->next;
		}
	}

	return trans_obj;
}

BOOL object_completed(trans_obj_t *to) {

	BOOL ready = FALSE;
	//printf("Object completed # Ready blocks %i N: %i\n", to->nb_of_ready_blocks, to->bs->N);
	//fflush(stdout);

#ifdef _MSC_VER
#else
	lock_lct_header();
#endif
	
	if(to->nb_of_ready_blocks == to->bs->N) {
		ready = TRUE;
	}

#ifdef _MSC_VER
#else
	unlock_lct_header();
#endif

	return ready;
}

BOOL block_ready_to_decode(trans_block_t *tb) {

	BOOL ready = FALSE;

	//Malek El Khatib 11.08.2014
	//If multiple encoding symbols (es) are sent in payload, less units (i.e. packets) are to be received
	if (numEncSymbPerPacket == 0) {
		//printf("Check if ready to decode: %u %u %u %u\n",tb->nb_of_rx_units,tb->k,nb_of_symb_to_decode_simult,(unsigned int)ceil((double)tb->k/(double)(nb_of_symb_to_decode_simult)));
		//printf("Check if ready to decode #symbols: %u/%u \n", tb->nb_of_rx_units, tb->k);
		//fflush(stdout);

		if(tb->nb_of_rx_symbols >= tb->k && tb->k > 0) {
			ready = TRUE;
		}
	}
	else 
	{//END
		//printf("Check if ready to decode #symbols: %u/%u\n", tb->nb_of_rx_units, tb->k);
		//printf("Check if ready to decode #symbols: %u/%u \n", tb->nb_of_rx_units, tb->k);
		//fflush(stdout);
		if(tb->nb_of_rx_units >= tb->k && tb->k > 0) {
			ready = TRUE;
		}
	}

	return ready;
}
