/** \file stsid.c \brief stsid parsing
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#ifdef _MSC_VER
#include <windows.h>
#else
#include <pthread.h>
#endif

#include <expat.h>

#include "stsid.h"
#include "mad_utf8.h"
	
 /**
  * Global variable used in parsing
  */

route_t* rs;			/**< ROUTE Session */
route_t* prev_rs;		/**< previous parsed LCT channel */
BOOL is_first_addr;		/**< is first address parsed or not? */
stsid_t* stsid;			/**< S-TSID */

lct_ch_t *lct;			/**< LCT channel */
lct_ch_t* prev_ls;		/**< previous parsed LCT channel */
BOOL is_first_tsi;		/**< is first TSI parsed or not? */

//srcflow_t* src;			/**< Source Flow */

//file_t* file;			/**< file */
//file_t* prev_file;		/**< previous parsed file */
//BOOL is_first_toi;		/**< is first TOI parsed or not? */
//fdt_t* fdtinst;			/**< FDT-Instance */
//afdt_t* afdt;			/**< ATSC FDT Instance */

rating_t* ratings;		/**< ratings */
rating_t* prev_rating;	/**< previous ratings file */
BOOL is_first_rating;	/**< is first rating parsed or not? */
//mediainfo_t* mediainfo;	/**< MediaInfo */

aeaid_t* aeamessage;	/**< AEA messaage */
aeaid_t* prev_aea;		/**< previous aea file */
BOOL is_first_aea;		/**< is first aea parsed or not? */
aea_t* aeamedia;		/**< AEA Media */

payload_t* pay;			/**< OPTIONAL payload element */
payload_t* prev_pay;	/**< previous payload element */
BOOL is_first_pay;		/**< is first aea parsed or not? */

protectobj_t* obj;		/**< Protected Object */
protectobj_t* prev_obj;	/**< previous protected object */
BOOL is_first_obj;		/**< is first protected object parsed or not? */
//fecparam_t* fec;		/**< Repair FEC */

//repairflow_t* rpr;		/**< Repair Flow */

stoi_t* srctoi;			/**< Repair Source TOI */
stoi_t* prev_toi;		/**< previous repair source TOI object */
BOOL is_first_srctoi;	/**< is first repair source TOI parsed or not? */


/**
 * Global variables semaphore
 */

#ifdef _MSC_VER
RTL_CRITICAL_SECTION stsid_variables_semaphore;
#else
pthread_mutex_t stsid_variables_semaphore = PTHREAD_MUTEX_INITIALIZER;
#endif

/**
 * This is a private function, which locks the stsid.
 *
 */

void lock_stsid(void) {
#ifdef _MSC_VER
	EnterCriticalSection(&stsid_variables_semaphore);
#else
	pthread_mutex_lock(&stsid_variables_semaphore);
#endif
}

/**
 * This is a private function, which unlocks the stsid.
 *
 */

void unlock_stsid(void) {
#ifdef _MSC_VER
	LeaveCriticalSection(&stsid_variables_semaphore);
#else
	pthread_mutex_unlock(&stsid_variables_semaphore);
#endif
}

/**
 * This is a private function which is used in S-TSID parsing.
 *
 * @param userData not used, must be
 * @param name pointer to buffer containing element's name
 * @param atts pointer to buffer containing element's attributes
 *
 */

static void startElement_stsid(void *userData, const char *name, const char **atts) {

#ifndef _MSC_VER
	char *ep;
#endif

	char *mbstr;
	//printf("name: %s\tattribute: %s\n", name, *atts);
	//fflush(stdout);

	while (*atts != NULL) {
		if (!strcmp(name, "RS")) {
			//printf("found RS\n");
			//fflush(stdout);

			if (!is_first_addr && strcmp(*atts, "dPort")) rs = NULL;

			if (rs == NULL) {
				if (!(rs = (route_t*)calloc(1, sizeof(route_t)))) {
					printf("Could not alloc memory for S-TSID LCT channel!\n");
					return;
				}

				/* initialize ROUTE Session parameters */
				rs->prev = NULL;
				rs->next = NULL;
				rs->sIpAddr = NULL;
				rs->dIpAddr = NULL;
				rs->dPort = NULL;

				stsid->nb_of_rs++;

				if (is_first_addr) {
					stsid->rs_list = rs;
					is_first_addr = FALSE;
				}
				else {
					prev_rs->next = rs;
					rs->prev = prev_rs;
				}

				prev_rs = rs;
			}

			if (!strcmp(*atts, "sIpAddr")) {
				//printf("found sIPAddr\n");
				//fflush(stdout);

				atts++;

				if (!(mbstr = (char*)calloc((strlen(*atts) + 1), sizeof(char)))) {
					printf("Could not alloc memory for mbstr!\n");
					fflush(stdout);
					return;
				}

				x_utf8s_to_iso_8859_1s(mbstr, *atts, strlen(*atts));

				if (!(rs->sIpAddr = (char*)calloc((size_t)(strlen(mbstr) + 1), sizeof(char)))) {
					printf("Could not alloc memory for Source IP Address!\n");
					fflush(stdout);
					return;
				}

				memcpy(rs->sIpAddr, mbstr, strlen(mbstr));
				free(mbstr);

				if (is_first_addr) {
					stsid->rs_list = rs;
					is_first_addr = FALSE;
				}
				else {
					prev_rs->next = rs;
					rs->prev = prev_rs;
				}

				prev_rs = rs;

			}
			else if (!strcmp(*atts, "dIpAddr")) {
				//printf("found dIpAddr\n");
				//fflush(stdout);

				atts++;

				if (!(mbstr = (char*)calloc((strlen(*atts) + 1), sizeof(char)))) {
					printf("Could not alloc memory for mbstr!\n");
					fflush(stdout);
					return;
				}

				x_utf8s_to_iso_8859_1s(mbstr, *atts, strlen(*atts));

				if (!(rs->dIpAddr = (char*)calloc((size_t)(strlen(mbstr) + 1), sizeof(char)))) {
					printf("Could not alloc memory for Destination IP Address!\n");
					fflush(stdout);
					return;
				}

				memcpy(rs->dIpAddr, mbstr, strlen(mbstr));
				free(mbstr);
			}
			else if (!strcmp(*atts, "dPort")) {
				//printf("found dPort\n");
				//fflush(stdout);

				atts++;

				if (!(mbstr = (char*)calloc((strlen(*atts) + 1), sizeof(char)))) {
					printf("Could not alloc memory for mbstr!\n");
					fflush(stdout);
					return;
				}

				x_utf8s_to_iso_8859_1s(mbstr, *atts, strlen(*atts));

				if (!(rs->dPort = (char*)calloc((size_t)(strlen(mbstr) + 1), sizeof(char)))) {
					printf("Could not alloc memory for Destination IP Port!\n");
					fflush(stdout);
					return;
				}

				memcpy(rs->dPort, mbstr, strlen(mbstr));
				free(mbstr);

			}
			else {
				atts++;
			}

			atts++;

		}
		else if (!strcmp(name, "LS")) {
			//printf("found LS, required cache size: %d bytes\n", sizeof(lct_ch_t));
			//fflush(stdout);

			if (!is_first_tsi && strcmp(*atts, "bw") && strcmp(*atts, "startTime") && strcmp(*atts, "endTime")) lct = NULL;

			if(lct == NULL) {
				if(!(lct = (lct_ch_t*)calloc(1, sizeof(lct_ch_t)))) {
					printf("Could not alloc memory for S-TSID LCT channel!\n");
					return;
				}
				
				if (rs == NULL) {
					if (!(rs = (route_t*)calloc(1, sizeof(route_t)))) {
						printf("Could not alloc memory for S-TSID ROUTE Session!\n");
						return;
					}

					//printf("No attributes found in S-TSID RS element\n"); fflush(stdout);
					/* initialize ROUTE Session parameters if there were no RS attributes */
					rs->prev = NULL;
					rs->next = NULL;
					rs->sIpAddr = NULL;
					rs->dIpAddr = NULL;
					rs->dPort = NULL;

					stsid->nb_of_rs++;

					if (is_first_addr) {
						stsid->rs_list = rs;
						is_first_addr = FALSE;
					}
					else {
						prev_rs->next = rs;
						rs->prev = prev_rs;
					}

					prev_rs = rs;
				}

				/* initialize S-TSID LCT parameters */
				lct->prev = NULL;
				lct->next = NULL;
				lct->tsi = 0;
				lct->bw = 0;
				lct->startTime = NULL;
				lct->endTime = NULL;
				/* initialize Source Flow parameters */
				lct->rt = FALSE;
				lct->minBuffSize = 0;
				/* initialize fdt:File parameters */
				lct->toi = 0;
				lct->status = 0;
				lct->transfer_len = 0;
				lct->content_len = 0;
				lct->location = NULL;
				lct->md5 = NULL;
				/* initialize FDT Instance parameters */
				lct->fdt = NULL;
				lct->expires = 0;
				lct->complete = FALSE;
				lct->fec_enc_id = -1;
				lct->fec_inst_id = -1;
				lct->finite_field = 16;
				lct->nb_of_es_per_group = 1;
				lct->max_sb_len = 0;
				lct->es_len = 0;
				lct->max_nb_of_es = 0;
				lct->nb_of_files = 1;
				lct->type = NULL;
				lct->encoding = NULL;
				lct->file_list = NULL;
				/* initialize ATSC FDT parameters */
				lct->efdtVersion = 0;
				lct->maxExpiresDelta = 0;
				lct->maxTransportSize = 0;
				lct->fileTemplate = NULL;
				lct->appContextIdList = NULL;
				lct->filterCodes = 0;
				/* initialize ratings parameters */
				lct->codePoint = 0;
				lct->formatId = 0;
				lct->frag = 0;
				lct->order = FALSE;
				lct->srcFecPayloadId = 0;
				/* initialize Media Info parameters */
				lct->startup = FALSE;
				lct->lang = NULL;
				lct->contentType = NULL;
				lct->repId = NULL;
				/* initialize FEC Parameters */
				lct->maximumDelay = 0;
				lct->overhead = 0;
				lct->fecMinBuffSize = 0;
				lct->fecOTI = 0;
				lct->nb_of_obj = 0;
				/* initialize Protected Object */
				lct->sessionDescription = NULL;
				lct->objecttsi = 0;
				lct->nb_of_stoi = 0;

				rs->nb_of_ls++;

			}

			if (!strcmp(*atts, "tsi")) {
				//printf("found tsi\n");
				//fflush(stdout);

#ifdef _MSC_VER     
				lct->tsi = atoi(*(++atts));

				if (lct->tsi > (unsigned int)0xFFFFFFFF) {
					printf("LCT TSI too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#else               
				lct->tsi = strtoul(*(++atts), &ep, 10);

				if (*(atts) == '\0' || *ep != '\0') {
					printf("LCT TSI not a number\n");
					fflush(stdout);
					return;
				}

				if (errno == ERANGE && lct->tsi == 0xFFFFFFFF) {
					printf("LCT TSI too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#endif

				if (is_first_tsi) {
					stsid->rs_list->lct_list = lct;
					is_first_tsi = FALSE;
				}
				else {
					prev_ls->next = lct;
					lct->prev = prev_ls;
				}

				prev_ls = lct;

			}
			else if (!strcmp(*atts, "bw")) {

#ifdef _MSC_VER     
				lct->bw = atoi(*(++atts));

				if (lct->bw > (unsigned int)0xFFFFFFFF) {
					printf("LCT bandwidth too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#else               
				lct->bw = strtoul(*(++atts), &ep, 10);

				if (*(atts) == '\0' || *ep != '\0') {
					printf("LCT bandwidth not a number\n");
					fflush(stdout);
					return;
				}

				if (errno == ERANGE && lct->bw == 0xFFFFFFFF) {
					printf("LCT bandwidth too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#endif

			}
			else if (!strcmp(*atts, "startTime")) {
				//printf("found startTime\n");
				//fflush(stdout);

				atts++;

				if (!(mbstr = (char*)calloc((strlen(*atts) + 1), sizeof(char)))) {
					printf("Could not alloc memory for mbstr!\n");
					fflush(stdout);
					return;
				}

				x_utf8s_to_iso_8859_1s(mbstr, *atts, strlen(*atts));

				if (!(lct->startTime = (char*)calloc((size_t)(strlen(mbstr) + 1), sizeof(char)))) {
					printf("Could not alloc memory for LCT Channel start time!\n");
					fflush(stdout);
					return;
				}

				memcpy(lct->startTime, mbstr, strlen(mbstr));
				free(mbstr);

			}
			else if (!strcmp(*atts, "endTime")) {

				atts++;

				if (!(mbstr = (char*)calloc((strlen(*atts) + 1), sizeof(char)))) {
					printf("Could not alloc memory for mbstr!\n");
					fflush(stdout);
					return;
				}

				x_utf8s_to_iso_8859_1s(mbstr, *atts, strlen(*atts));

				if (!(lct->endTime = (char*)calloc((size_t)(strlen(mbstr) + 1), sizeof(char)))) {
					printf("Could not alloc memory for LCT Channel end time!\n");
					fflush(stdout);
					return;
				}

				memcpy(lct->endTime, mbstr, strlen(mbstr));
				free(mbstr);

			}
			else {
				atts++;
			}

			atts++;

		}

		// SOURCE FLOW
		else if (!strcmp(name, "SrcFlow")) {
			//printf("found src\n");
			//fflush(stdout);

			//if (src == NULL) {
			//	if (!(src = (srcflow_t*)calloc(1, sizeof(srcflow_t)))) {
			//		printf("Could not alloc memory for S-TSID Source Flow!\n");
			//		return;
			//	}
			//
			//	/* initialize S-TSID Source Flow parameters */
			//	src->rt = FALSE;
			//	src->minBuffSize = 0;
			//
			//}

			if (!strcmp(*atts, "rt")) {

				if (!strcmp("true", *(++atts))) {
					lct->rt = TRUE;
				}
				else {
					lct->rt = FALSE;
				}

			}
			else if (!strcmp(*atts, "minBuffSize")) {

	#ifdef _MSC_VER     
				lct->minBuffSize = atoi(*(++atts));

				if (lct->minBuffSize > (unsigned int)0xFFFFFFFF) {
					printf("Source Flow Minimum Buffer size too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
	#else               
				lct->minBuffSize = strtoul(*(++atts), &ep, 10);

				if (*(atts) == '\0' || *ep != '\0') {
					printf("Source Flow Minimum Buffer size not a number\n");
					fflush(stdout);
					return;
				}

				if (errno == ERANGE && lct->minBuffSize == 0xFFFFFFFF) {
					printf("Source Flow Minimum Buffer size too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
	#endif
			}
			else {
				atts++;
			}

			atts++;

		}
		else if (!strcmp(name, "FDT-Instance")) {
			//printf("found FDT-Instance\n");
			//fflush(stdout);

			//if (fdtinst == NULL) {
			//	if (!(fdtinst = (fdt_t*)calloc(1, sizeof(fdt_t)))) {
			//		printf("Could not alloc memory for S-TSID FDT-Instance!\n");
			//		return;
			//	}
			//
			//	/* initialize ROUTE Session parameters if there were no RS attributes*/
			//	fdtinst->expires = 0;
			//	fdtinst->complete = FALSE;
			//	fdtinst->fec_enc_id = -1;
			//	fdtinst->fec_inst_id = -1;
			//	fdtinst->finite_field = 16;
			//	fdtinst->nb_of_es_per_group = 1;
			//	fdtinst->max_sb_len = 0;
			//	fdtinst->es_len = 0;
			//	fdtinst->max_nb_of_es = 0;
			//	fdtinst->nb_of_files = 0;
			//	fdtinst->type = NULL;
			//	fdtinst->encoding = NULL;
			//	fdtinst->file_list = NULL;
			//
			//}
			//if (afdt == NULL) {
			//	if (!(afdt = (afdt_t*)calloc(1, sizeof(afdt_t)))) {
			//		printf("Could not alloc memory for S-TSID ATSC FDT!\n");
			//		return;
			//	}
			//
			//	/* initialize ATSC FDT parameters */
			//	afdt->efdtVersion = 0;
			//	afdt->maxExpiresDelta = 0;
			//	afdt->maxTransportSize = 0;
			//	afdt->fileTemplate = NULL;
			//	afdt->appContextIdList = NULL;
			//	afdt->filterCodes = 0;
			//
			//}

			if (!strcmp(*atts, "Expires")) {

#ifdef _MSC_VER
				lct->expires = _atoi64(*(++atts));

				if (lct->expires > (unsigned long long)0xFFFFFFFFFFFFFFFF) {
					printf("Expires too big for unsigned long long (64 bits)\n");
					fflush(stdout);
					return;
				}
#else
				lct->expires = strtoull(*(++atts), &ep, 10);

				if (*(atts) == '\0' || *ep != '\0') {
					printf("Expires not a number\n");
					fflush(stdout);
					return;
				}

				if (errno == ERANGE && lct->expires == 0xFFFFFFFFFFFFFFFFULL) {
					printf("Expires too big for unsigned long long (64 bits)\n");
					fflush(stdout);
					return;
				}
#endif
			}
			else if (!strcmp(*atts, "Complete")) {

				if (!strcmp("true", *(++atts))) {
					lct->complete = TRUE;
				}
				else {
					lct->complete = FALSE;
				}
			}
			else if (!strcmp(*atts, "FEC-OTI-FEC-Encoding-ID")) {
				lct->fec_enc_id = (unsigned char)atoi(*(++atts));
			}
			else if (!strcmp(*atts, "FEC-OTI-FEC-Instance-ID")) {
				lct->fec_inst_id = (unsigned short)atoi(*(++atts));
			}
			else if (!strcmp(*atts, "FEC-OTI-Maximum-Source-Block-Length")) {
				lct->max_sb_len = (unsigned int)atoi(*(++atts));
			}
			else if (!strcmp(*atts, "FEC-OTI-Encoding-Symbol-Length")) {
				lct->es_len = (unsigned short)atoi(*(++atts));
			}
			else if (!strcmp(*atts, "FEC-OTI-Max-Number-of-Encoding-Symbols")) {
				lct->max_nb_of_es = (unsigned short)atoi(*(++atts));
			}
			else if (!strcmp(*atts, "FEC-OTI-Number-of-Encoding-Symbols-per-Group")) {
				lct->nb_of_es_per_group = (unsigned char)atoi(*(++atts));
			}
			else if (!strcmp(*atts, "FEC-OTI-Finite-Field-Parameter")) {
				lct->finite_field = (unsigned char)atoi(*(++atts));
			}
			else if (!strcmp(*atts, "Content-Type")) {
				atts++;

				if (!(lct->type = (char*)calloc((strlen(*atts) + 1), sizeof(char)))) {
					printf("Could not alloc memory for fdt->type!\n");
					return;
				}

				memcpy(lct->type, *atts, strlen(*atts));
			}
			else if (!strcmp(*atts, "Content-Encoding")) {
				atts++;

				if (!(lct->encoding = (char*)calloc((strlen(*atts) + 1), sizeof(char)))) {
					printf("Could not alloc memory for fdt->encoding!\n");
					return;
				}

				memcpy(lct->encoding, *atts, strlen(*atts));
			}
			else if (!strcmp(*atts, "afdt:efdtVersion")) {

#ifdef _MSC_VER     
				lct->efdtVersion = atoi(*(++atts));

				if (lct->efdtVersion > (unsigned int)0xFFFFFFFF) {
					printf("EFDT Version too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#else               
				lct->efdtVersion = strtoul(*(++atts), &ep, 10);

				if (*(atts) == '\0' || *ep != '\0') {
					printf("EFDT Version not a number\n");
					fflush(stdout);
					return;
				}

				if (errno == ERANGE && lct->efdtVersion == 0xFFFFFFFF) {
					printf("EFDT Version too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#endif
			}
			else if (!strcmp(*atts, "afdt:maxExpiresDelta")) {

#ifdef _MSC_VER     
				lct->maxExpiresDelta = atoi(*(++atts));

				if (lct->maxExpiresDelta > (unsigned int)0xFFFFFFFF) {
					printf("Max Expires Delta too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#else               
				lct->maxExpiresDelta = strtoul(*(++atts), &ep, 10);

				if (*(atts) == '\0' || *ep != '\0') {
					printf("Max Expires Delta not a number\n");
					fflush(stdout);
					return;
				}

				if (errno == ERANGE && lct->maxExpiresDelta == 0xFFFFFFFF) {
					printf("Max Expires Delta too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#endif
			}
			else if (!strcmp(*atts, "afdt:maxTransportSize")) {

#ifdef _MSC_VER     
				lct->maxTransportSize = atoi(*(++atts));

				if (lct->maxTransportSize > (unsigned int)0xFFFFFFFF) {
					printf("Max Transport Size too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#else               
				lct->maxTransportSize = strtoul(*(++atts), &ep, 10);

				if (*(atts) == '\0' || *ep != '\0') {
					printf("Max Transport Size not a number\n");
					fflush(stdout);
					return;
				}

				if (errno == ERANGE && lct->maxTransportSize == 0xFFFFFFFF) {
					printf("Max Transport Size too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#endif
			}
			else if (!strcmp(*atts, "afdt:fileTemplate")) {
				atts++;

				if (!(lct->fileTemplate = (char*)calloc((strlen(*atts) + 1), sizeof(char)))) {
					printf("Could not alloc memory for afdt->fileTemplate!\n");
					return;
				}

				memcpy(lct->fileTemplate, *atts, strlen(*atts));
			}
			else if (!strcmp(*atts, "afdt:appContextIdList")) {
				atts++;

				if (!(lct->appContextIdList = (char*)calloc((strlen(*atts) + 1), sizeof(char)))) {
					printf("Could not alloc memory for afdt->appContextIdList!\n");
					return;
				}

				memcpy(lct->appContextIdList, *atts, strlen(*atts));
			}
			else if (!strcmp(*atts, "afdt:filterCodes")) {

#ifdef _MSC_VER     
				lct->filterCodes = atoi(*(++atts));

				if (lct->filterCodes > (unsigned int)0xFFFFFFFF) {
					printf("Filter COdes too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#else               
				lct->filterCodes = strtoul(*(++atts), &ep, 10);

				if (*(atts) == '\0' || *ep != '\0') {
					printf("Filter Codes not a number\n");
					fflush(stdout);
					return;
				}

				if (errno == ERANGE && lct->filterCodes == 0xFFFFFFFF) {
					printf("Filter Codes too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#endif
			}
			else {
				atts++;
			}
			atts++;


		}
		else if (!strcmp(name, "fdt:File")) {
			//printf("found fdt:File\n");
			//fflush(stdout);

			//if (file == NULL) {
			//
			//	if (!(file = (file_t*)calloc(1, sizeof(file_t)))) {
			//		printf("Could not alloc memory for mad_fdt file!\n");
			//		return;
			//	}
			//
			//	/* initialise file parameters */
			//	file->prev = NULL;
			//	file->next = NULL;
			//	file->toi = 0;
			//	file->status = 0;
			//	file->transfer_len = 0;
			//	file->content_len = 0;
			//	file->location = NULL;
			//	file->md5 = NULL;
			//	file->type = NULL;
			//	file->encoding = NULL;
			//
			//	file->expires = fdtinst->expires;
			//
			//	file->fec_enc_id = fdtinst->fec_enc_id;
			//	file->fec_inst_id = fdtinst->fec_inst_id;
			//	file->finite_field = fdtinst->finite_field;
			//	file->nb_of_es_per_group = fdtinst->nb_of_es_per_group;
			//	file->max_sb_len = fdtinst->max_sb_len;
			//	file->es_len = fdtinst->es_len;
			//	file->max_nb_of_es = fdtinst->max_nb_of_es;
			//
			//	lct->nb_of_files++;
			//}

			if (!strcmp(*atts, "TOI")) {

#ifdef _MSC_VER    
				lct->toi = _atoi64(*(++atts));

				if (lct->toi > (unsigned long long)0xFFFFFFFFFFFFFFFF) {
					printf("TOI too big for unsigned long long (64 bits)\n");
					fflush(stdout);
					return;
				}
#else               
				lct->toi = strtoull(*(++atts), &ep, 10);

				if (*(atts) == '\0' || *ep != '\0') {
					printf("TOI not a number\n");
					fflush(stdout);
					return;
				}

				if (errno == ERANGE && lct->toi == 0xFFFFFFFFFFFFFFFFULL) {
					printf("TOI too big for unsigned long long (64 bits)\n");
					fflush(stdout);
					return;
				}
#endif
				lct->nb_of_files++;
			}
			else if (!strcmp(*atts, "Content-Location")) {

				atts++;

				if (!(mbstr = (char*)calloc((strlen(*atts) + 1), sizeof(char)))) {
					printf("Could not alloc memory for mbstr!\n");
					return;
				}

				x_utf8s_to_iso_8859_1s(mbstr, *atts, strlen(*atts));

				if (!(lct->location = (char*)calloc((size_t)(strlen(mbstr) + 1), sizeof(char)))) {
					printf("Could not alloc memory for file->location!\n");
					return;
				}

				memcpy(lct->location, mbstr, strlen(mbstr));
				free(mbstr);

			}
			else if (!strcmp(*atts, "Content-Length")) {

#ifdef _MSC_VER     
				lct->content_len = _atoi64(*(++atts));

				if (lct->content_len > (unsigned long long)0xFFFFFFFFFFFFFFFF) {
					printf("Content-Length too big for unsigned long long (64 bits)\n");
					fflush(stdout);
					return;
				}
#else               
				lct->content_len = strtoull(*(++atts), &ep, 10);

				if (*(atts) == '\0' || *ep != '\0') {
					printf("Content-Length not a number\n");
					fflush(stdout);
					return;
				}

				if (errno == ERANGE && lct->content_len == 0xFFFFFFFFFFFFFFFFULL) {
					printf("Content-Length too big for unsigned long long (64 bits)\n");
					fflush(stdout);
					return;
				}
#endif
				if (lct->transfer_len == 0) {
					lct->transfer_len = lct->content_len;
				}

			}
			else if (!strcmp(*atts, "Transfer-Length")) {

#ifdef _MSC_VER			  
				lct->transfer_len = _atoi64(*(++atts));

				if (lct->transfer_len > (unsigned long long)0xFFFFFFFFFFFFFFFF) {
					printf("Transfer-Length too big for unsigned long long (64 bits)\n");
					fflush(stdout);
					return;
				}
#else
				lct->transfer_len = strtoull(*(++atts), &ep, 10);

				if (*(atts) == '\0' || *ep != '\0') {
					printf("Transfer-Length not a number\n");
					fflush(stdout);
					return;
				}

				if (errno == ERANGE && lct->transfer_len == 0xFFFFFFFFFFFFFFFFULL) {
					printf("Transfer-Length too big for unsigned long long (64 bits)\n");
					fflush(stdout);
					return;
				}
#endif 

			}
			else if (!strcmp(*atts, "Content-Type")) {
			
				atts++;
			
				if (!(lct->type = (char*)calloc((strlen(*atts) + 1), sizeof(char)))) {
					printf("Could not alloc memory for file->type!\n");
					return;
				}
			
				memcpy(lct->type, *atts, strlen(*atts));
			}
			else if (!strcmp(*atts, "Content-Encoding")) {
			
				atts++;
			
				if (!(lct->encoding = (char*)calloc((strlen(*atts) + 1), sizeof(char)))) {
					printf("Could not alloc memory for file->encoding!\n");
					return;
				}
			
				memcpy(lct->encoding, *atts, strlen(*atts));
			}
			else if (!strcmp(*atts, "Content-MD5")) {

				atts++;

				if (!(lct->md5 = (char*)calloc((strlen(*atts) + 1), sizeof(char)))) {
					printf("Could not alloc memory for file->md5!\n");
					return;
				}

				memcpy(lct->md5, *atts, strlen(*atts));
			}
			else if (!strcmp(*atts, "FEC-OTI-FEC-Encoding-ID")) {
				lct->fec_enc_id = (unsigned char)atoi(*(++atts));
			}
			else if (!strcmp(*atts, "FEC-OTI-FEC-Instance-ID")) {
				lct->fec_inst_id = (unsigned short)atoi(*(++atts));
			}
			else if (!strcmp(*atts, "FEC-OTI-Maximum-Source-Block-Length")) {
				lct->max_sb_len = (unsigned int)atoi(*(++atts));
			}
			else if (!strcmp(*atts, "FEC-OTI-Encoding-Symbol-Length")) {
				lct->es_len = (unsigned short)atoi(*(++atts));
			}
			else if (!strcmp(*atts, "FEC-OTI-Max-Number-of-Encoding-Symbols")) {
				lct->max_nb_of_es = (unsigned short)atoi(*(++atts));
			}
			else if (!strcmp(*atts, "FEC-OTI-Number-of-Encoding-Symbols-per-Group")) {
				lct->nb_of_es_per_group = (unsigned char)atoi(*(++atts));
			}
			else if (!strcmp(*atts, "FEC-OTI-Finite-Field-Parameter")) {
				lct->finite_field = (unsigned char)atoi(*(++atts));
			}
			else {
				atts++;
			}

			atts++;

		}
		else if (!strcmp(name, "ContentRating")) {
			//printf("found ContentRating\n");
			//fflush(stdout);

			if (ratings == NULL) {
				if (!(ratings = (rating_t*)calloc(1, sizeof(rating_t)))) {
					printf("Could not alloc memory for S-TSID ratings!\n");
					return;
				}

				/* initialize ratings parameters */
				ratings->prev = NULL;
				ratings->next = NULL;
				ratings->schemeIdUri = NULL;
				ratings->value = NULL;

				lct->nb_of_ratings++;

			}

			if (!strcmp(*atts, "schemeIdUri")) {
				atts++;

				if (!(mbstr = (char*)calloc((strlen(*atts) + 1), sizeof(char)))) {
					printf("Could not alloc memory for mbstr!\n");
					fflush(stdout);
					return;
				}

				x_utf8s_to_iso_8859_1s(mbstr, *atts, strlen(*atts));

				if (!(ratings->schemeIdUri = (char*)calloc((size_t)(strlen(mbstr) + 1), sizeof(char)))) {
					printf("Could not alloc memory for Source Flow Media Content Advisory Rating Scheme!\n");
					fflush(stdout);
					return;
				}

				memcpy(ratings->schemeIdUri, mbstr, strlen(mbstr));
				free(mbstr);

				if (is_first_rating) {
					lct->rating_list = ratings;
					is_first_rating = FALSE;
				}
				else {
					prev_rating->next = ratings;
					ratings->prev = prev_rating;
				}

				prev_rating = ratings;

			}
			else if (!strcmp(*atts, "value")) {
				atts++;

				if (!(mbstr = (char*)calloc((strlen(*atts) + 1), sizeof(char)))) {
					printf("Could not alloc memory for mbstr!\n");
					fflush(stdout);
					return;
				}

				x_utf8s_to_iso_8859_1s(mbstr, *atts, strlen(*atts));

				if (!(ratings->value = (char*)calloc((size_t)(strlen(mbstr) + 1), sizeof(char)))) {
					printf("Could not alloc memory for Source Flow Media Content Advisory Rating Value!\n");
					fflush(stdout);
					return;
				}

				memcpy(ratings->value, mbstr, strlen(mbstr));
				free(mbstr);

			}
			else {
				atts++;
			}

			atts++;

		}
		else if (!strcmp(name, "AEAMedia")) {
			//printf("found AEAMedia\n");
			//fflush(stdout);

			if (aeamessage == NULL) {
				if (!(aeamessage = (aeaid_t*)calloc(1, sizeof(aeaid_t)))) {
					printf("Could not alloc memory for S-TSID AEA Messages!\n");
					return;
				}

				/* initialize ratings parameters */
				aeamessage->prev = NULL;
				aeamessage->next = NULL;
				aeamessage->aeaid = NULL;

				lct->nb_of_aea++;

			}

			if (!strcmp(*atts, "aeaid")) {
				atts++;

				if (!(mbstr = (char*)calloc((strlen(*atts) + 1), sizeof(char)))) {
					printf("Could not alloc memory for mbstr!\n");
					fflush(stdout);
					return;
				}

				x_utf8s_to_iso_8859_1s(mbstr, *atts, strlen(*atts));

				if (!(aeamessage->aeaid = (char*)calloc((size_t)(strlen(mbstr) + 1), sizeof(char)))) {
					printf("Could not alloc memory for Source Flow AEA message ID!\n");
					fflush(stdout);
					return;
				}

				memcpy(aeamessage->aeaid, mbstr, strlen(mbstr));
				free(mbstr);

				if (is_first_aea) {
					lct->aeaid_list = aeamessage;
					is_first_aea = FALSE;
				}
				else {
					prev_aea->next = aeamessage;
					aeamessage->prev = prev_aea;
				}

				prev_aea = aeamessage;

			}
			else {
				atts++;
			}

			atts++;

		}
		else if (!strcmp(name, "Payload")) {
			//printf("found Payload\n");
			//fflush(stdout);

			if (pay == NULL) {
				if (!(pay = (payload_t*)calloc(1, sizeof(payload_t)))) {
					printf("Could not alloc memory for S-TSID Payload Messages!\n");
					return;
				}
			
				/* initialize ratings parameters */
				pay->prev = NULL;
				pay->next = NULL;
				pay->codePoint = 0;
				pay->formatId = 0;
				pay->frag = 0;
				pay->order = FALSE;
				pay->srcFecPayloadId = 0;
			
				lct->nb_of_payload++;
			
			}

			if (!strcmp(*atts, "codePoint")) {

#ifdef _MSC_VER     
				lct->codePoint = atoi(*(++atts));

				if (lct->codePoint > (unsigned int)0xFFFFFFFF) {
					printf("LCT Payload codePoint too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#else               
				lct->codePoint = strtoul(*(++atts), &ep, 10);

				if (*(atts) == '\0' || *ep != '\0') {
					printf("LCT Payload codePoint not a number\n");
					fflush(stdout);
					return;
				}

				if (errno == ERANGE && lct->codePoint == 0xFFFFFFFF) {
					printf("LCT Payload codePoint too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#endif

				if (is_first_pay) {
					lct->payload_list = pay;
					is_first_pay = FALSE;
				}
				else {
					prev_pay->next = pay;
					pay->prev = prev_pay;
				}

				prev_pay = pay;

			}
			else if (!strcmp(*atts, "formatId")) {

#ifdef _MSC_VER     
				lct->formatId = atoi(*(++atts));

				if (lct->formatId > (unsigned int)0xFFFFFFFF) {
					printf("LCT Payload formatId too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#else               
				lct->formatId = strtoul(*(++atts), &ep, 10);

				if (*(atts) == '\0' || *ep != '\0') {
					printf("LCT Payload formatId not a number\n");
					fflush(stdout);
					return;
				}

				if (errno == ERANGE && lct->formatId == 0xFFFFFFFF) {
					printf("LCT Payload formatId too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#endif
			}
			else if (!strcmp(*atts, "frag")) {

#ifdef _MSC_VER     
				lct->frag = atoi(*(++atts));

				if (lct->frag > (unsigned int)0xFFFFFFFF) {
					printf("LCT Payload frag too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#else               
				lct->frag = strtoul(*(++atts), &ep, 10);

				if (*(atts) == '\0' || *ep != '\0') {
					printf("LCT Payload frag not a number\n");
					fflush(stdout);
					return;
				}

				if (errno == ERANGE && lct->frag == 0xFFFFFFFF) {
					printf("LCT Payload frag too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#endif
			}
			else if (!strcmp(*atts, "order")) {
				if (!strcmp("true", *(++atts))) {
					lct->order = TRUE;
				}
				else {
					lct->order = FALSE;
				}

			}
			else if (!strcmp(*atts, "srcFecPayloadId")) {

#ifdef _MSC_VER     
				lct->srcFecPayloadId = atoi(*(++atts));

				if (lct->srcFecPayloadId > (unsigned int)0xFFFFFFFF) {
					printf("LCT Payload srcFecPayloadId too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#else               
				lct->srcFecPayloadId = strtoul(*(++atts), &ep, 10);

				if (*(atts) == '\0' || *ep != '\0') {
					printf("LCT Payload srcFecPayloadId not a number\n");
					fflush(stdout);
					return;
				}

				if (errno == ERANGE && lct->srcFecPayloadId == 0xFFFFFFFF) {
					printf("LCT Payload srcFecPayloadId too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#endif
			}
			else {
				atts++;
			}

			atts++;

		}
		else if (!strcmp(name, "MediaInfo")) {
			//printf("found MediaInfo\n");
			//fflush(stdout);

			//if (mediainfo == NULL) {
			//	if (!(mediainfo = (mediainfo_t*)calloc(1, sizeof(mediainfo_t)))) {
			//		printf("Could not alloc memory for S-TSID Source Flow!\n");
			//		return;
			//	}
			//
			//	/* initialize S-TSID Source Flow parameters */
			//	mediainfo->startup = FALSE;
			//	mediainfo->lang = NULL;
			//	mediainfo->contentType = NULL;
			//	mediainfo->repId = NULL;
			//
			//}

			if (!strcmp(*atts, "startup")) {
				if (!strcmp("true", *(++atts))) {
					lct->startup = TRUE;
				}
				else {
					lct->startup = FALSE;
				}

			}
			else if (!strcmp(*atts, "lang")) {
				atts++;

				if (!(mbstr = (char*)calloc((strlen(*atts) + 1), sizeof(char)))) {
					printf("Could not alloc memory for mbstr!\n");
					fflush(stdout);
					return;
				}

				x_utf8s_to_iso_8859_1s(mbstr, *atts, strlen(*atts));

				if (!(lct->lang = (char*)calloc((size_t)(strlen(mbstr) + 1), sizeof(char)))) {
					printf("Could not alloc memory for Source Flow Media Start flag!\n");
					fflush(stdout);
					return;
				}

				memcpy(lct->lang, mbstr, strlen(mbstr));
				free(mbstr);

			}
			else if (!strcmp(*atts, "contentType")) {
				atts++;

				if (!(mbstr = (char*)calloc((strlen(*atts) + 1), sizeof(char)))) {
					printf("Could not alloc memory for mbstr!\n");
					fflush(stdout);
					return;
				}

				x_utf8s_to_iso_8859_1s(mbstr, *atts, strlen(*atts));

				if (!(lct->contentType = (char*)calloc((size_t)(strlen(mbstr) + 1), sizeof(char)))) {
					printf("Could not alloc memory for Source Flow Media Start flag!\n");
					fflush(stdout);
					return;
				}

				memcpy(lct->contentType, mbstr, strlen(mbstr));
				free(mbstr);

			}
			else if (!strcmp(*atts, "repId")) {
				atts++;

				if (!(mbstr = (char*)calloc((strlen(*atts) + 1), sizeof(char)))) {
					printf("Could not alloc memory for mbstr!\n");
					fflush(stdout);
					return;
				}

				x_utf8s_to_iso_8859_1s(mbstr, *atts, strlen(*atts));

				if (!(lct->repId = (char*)calloc((size_t)(strlen(mbstr) + 1), sizeof(char)))) {
					printf("Could not alloc memory for Source Flow Media Start flag!\n");
					fflush(stdout);
					return;
				}

				memcpy(lct->repId, mbstr, strlen(mbstr));
				free(mbstr);

			}
			else {
				atts++;
			}

			atts++;

		}

		// REPAIR FLOW
		else if (!strcmp(name, "SourceTOI")) {
			//printf("found SourceTOI\n");
			//fflush(stdout);

			//if (srctoi == NULL) {
			//
			//	if (!(srctoi = (stoi_t*)calloc(1, sizeof(stoi_t)))) {
			//		printf("Could not alloc memory for S-TSID repair source TOIs!\n");
			//		return;
			//	}
			//
			//	/* initialize repair source TOI parameters */
			//	srctoi->prev = NULL;
			//	srctoi->next = NULL;
			//	srctoi->x = 0;
			//	srctoi->y = 0;
			//
			//	obj->nb_of_stoi++;
			//
			//}

			if (!strcmp(*atts, "x")) {

#ifdef _MSC_VER     
				lct->stoi_list->x = atoi(*(++atts));

				if (lct->stoi_list->x > (unsigned int)0xFFFFFFFF) {
					printf("LCT Repair Flow Source TOI 'x' too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#else               
				lct->stoi_list->x = strtoul(*(++atts), &ep, 10);

				if (*(atts) == '\0' || *ep != '\0') {
					printf("LCT Repair Flow Source TOI 'x' not a number\n");
					fflush(stdout);
					return;
				}

				if (errno == ERANGE && srctoi->x == 0xFFFFFFFF) {
					printf("LCT Repair Flow Source TOI 'x' too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#endif

				//if (is_first_srctoi) {
				//	obj->stoi_list = srctoi;
				//	is_first_srctoi = FALSE;
				//}
				//else {
				//	prev_toi->next = srctoi;
				//	srctoi->prev = prev_toi;
				//}
				//
				//prev_toi = srctoi;


			}
			else if (!strcmp(*atts, "y")) {

#ifdef _MSC_VER     
				lct->stoi_list->y = atoi(*(++atts));

				if (lct->stoi_list->y > (unsigned int)0xFFFFFFFF) {
					printf("LCT Repair Flow Source TOI 'y' too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#else               
				lct->stoi_list->y = strtoul(*(++atts), &ep, 10);

				if (*(atts) == '\0' || *ep != '\0') {
					printf("LCT Repair Flow Source TOI 'y' not a number\n");
					fflush(stdout);
					return;
				}

				if (errno == ERANGE && srctoi->y == 0xFFFFFFFF) {
					printf("LCT Repair Flow Source TOI 'y' too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#endif
			}
			else {
				atts++;
			}

			atts++;

		}
		else if (!strcmp(name, "ProtectedObject")) {
			//printf("found ProtectedObject\n");
			//fflush(stdout);

			//if (obj == NULL) {
			//	if (!(obj = (protectobj_t*)calloc(1, sizeof(protectobj_t)))) {
			//		printf("Could not alloc memory for S-TSID Payload Messages!\n");
			//		return;
			//	}
			//
			//	/* initialize ratings parameters */
			//	obj->prev = NULL;
			//	obj->next = NULL;
			//	obj->sessionDescription = NULL;
			//	obj->objecttsi = 0;
			//	obj->stoi_list = NULL;
			//
			//	lct->nb_of_obj++;
			//
			//}

			if (!strcmp(*atts, "sessionDescription")) {
				//printf("found ProtectedObject sessionDescription\n");
				//fflush(stdout);

				atts++;

				if (!(mbstr = (char*)calloc((strlen(*atts) + 1), sizeof(char)))) {
					printf("Could not alloc memory for mbstr!\n");
					fflush(stdout);
					return;
				}

				x_utf8s_to_iso_8859_1s(mbstr, *atts, strlen(*atts));

				if (!(lct->sessionDescription = (char*)calloc((size_t)(strlen(mbstr) + 1), sizeof(char)))) {
					printf("Could not alloc memory for Repair Flow protected object Session Description!\n");
					fflush(stdout);
					return;
				}

				memcpy(lct->sessionDescription, mbstr, strlen(mbstr));
				free(mbstr);

				//if (is_first_obj) {
				//	lct->obj_list = obj;
				//	is_first_obj = FALSE;
				//}
				//else {
				//	prev_obj->next = obj;
				//	obj->prev = prev_obj;
				//}
				//
				//prev_obj = obj;

			}
			else if (!strcmp(*atts, "tsi")) {
				//printf("found ProtectedObject TSI\n");
				//fflush(stdout);
#ifdef _MSC_VER     
				lct->objecttsi = atoi(*(++atts));

				if (lct->objecttsi > (unsigned int)0xFFFFFFFF) {
					printf("LCT Repair Flow object TSI too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#else               
				lct->objecttsi = strtoul(*(++atts), &ep, 10);

				if (*(atts) == '\0' || *ep != '\0') {
					printf("LCT Repair Flow object TSI not a number\n");
					fflush(stdout);
					return;
				}

				if (errno == ERANGE && obj->objecttsi == 0xFFFFFFFF) {
					printf("LCT Repair Flow object TSI too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#endif
			}
			else {
				atts++;
			}

			atts++;

		}
		else if (!strcmp(name, "FECParameters")) {
			//printf("found FECParameters\n");
			//fflush(stdout);

			//if (fec == NULL) {
			//	if (!(fec = (fecparam_t*)calloc(1, sizeof(fecparam_t)))) {
			//		printf("Could not alloc memory for S-TSID FDT-Instance!\n");
			//		return;
			//	}
			//
			//	/* initialize FEC Parameters if there were no FEC attributes*/
			//	fec->maximumDelay = 0;
			//	fec->overhead = 0;
			//	fec->minBuffSize = 0;
			//	fec->fecOTI = 0;
			//
			//}

			if (!strcmp(*atts, "maximumDelay")) {

#ifdef _MSC_VER     
				lct->maximumDelay = atoi(*(++atts));

				if (lct->maximumDelay > (unsigned int)0xFFFFFFFF) {
					printf("LCT Repair Flow Maximum Delay too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#else               
				lct->maximumDelay = strtoul(*(++atts), &ep, 10);

				if (*(atts) == '\0' || *ep != '\0') {
					printf("LCT Repair Flow Maximum Delay not a number\n");
					fflush(stdout);
					return;
				}

				if (errno == ERANGE && lct->maximumDelay == 0xFFFFFFFF) {
					printf("LCT Repair Flow Maximum Delay too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#endif
			}
			else if (!strcmp(*atts, "overhead")) {

#ifdef _MSC_VER     
				lct->overhead = atoi(*(++atts));

				if (lct->overhead > (unsigned short)0xFFFF) {
					printf("LCT Repair Flow Percent Overhead too big for unsigned short (16 bits)\n");
					fflush(stdout);
					return;
				}
#else               
				//lct->overhead = strtouint16(*(++atts), &ep, 10);
				lct->overhead = strtoul(*(++atts), &ep, 10);

				if (*(atts) == '\0' || *ep != '\0') {
					printf("LCT Repair Flow percent Overhead not a number\n");
					fflush(stdout);
					return;
				}

				if (errno == ERANGE && lct->overhead == 0xFFFF) {
					printf("LCT Repair Flow Percent Overhead too big for unsigned short (16 bits)\n");
					fflush(stdout);
					return;
				}
#endif
			}
			else if (!strcmp(*atts, "minBuffSize")) {

#ifdef _MSC_VER     
				lct->fecMinBuffSize = atoi(*(++atts));

				if (lct->fecMinBuffSize > (unsigned int)0xFFFFFFFF) {
					printf("LCT Repair Flow Minimum Buffer size for super-object too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#else               
				lct->fecMinBuffSize = strtoul(*(++atts), &ep, 10);

				if (*(atts) == '\0' || *ep != '\0') {
					printf("LCT Repair Flow Minimum Buffer size not a number\n");
					fflush(stdout);
					return;
				}

				if (errno == ERANGE && lct->fecMinBuffSize == 0xFFFFFFFF) {
					printf("LCT Repair Flow Minimum Buffer size for super-object too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#endif
			}
			else if (!strcmp(*atts, "fecOTI")) {
				//printf("found FECParameters fecOTI\n");
				//fflush(stdout);
#ifdef _MSC_VER     
				lct->fecOTI = atoi(*(++atts));

				if (lct->fecOTI > (unsigned int)0xFFFFFFFF) {
					printf("LCT Repair Flow FEC information too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#else               
				lct->fecOTI = strtoul(*(++atts), &ep, 10);

				if (*(atts) == '\0' || *ep != '\0') {
					printf("LCT Repair Flow FEC information not a number\n");
					fflush(stdout);
					return;
				}

				if (errno == ERANGE && lct->fecOTI == 0xFFFFFFFF) {
					printf("LCT Repair Flow FEC information too big for unsigned int (32 bits)\n");
					fflush(stdout);
					return;
				}
#endif
			}
			else {
				atts++;
			}

			atts++;

		}
		else {
			atts += 2;
		}

	}

}

void initialize_stsid_parser(void) {
#ifdef _MSC_VER
  InitializeCriticalSection(&stsid_variables_semaphore);
#else
#endif
}

void release_stsid_parser(void) {
#ifdef _MSC_VER
  DeleteCriticalSection(&stsid_variables_semaphore);
#else
#endif
}

stsid_t* decode_stsid_payload(char *stsid_payload) {

	XML_Parser parser;
	size_t len;

	lock_stsid();

	parser = XML_ParserCreate(NULL);
	/* parser = XML_ParserCreate("iso-8859-1"); */

	len = strlen(stsid_payload);
	stsid = NULL;

	if(!(stsid = (stsid_t*)calloc(1, sizeof(stsid_t)))) {
		printf("Could not alloc memory for stsid!\n");
		XML_ParserFree(parser);
		unlock_stsid();
		return NULL;
	}

	/* initialise S-TSID parameters */
	stsid->nb_of_rs = 0;
	stsid->rs_list = NULL;

	rs = NULL;
	prev_rs = NULL;
	is_first_addr = TRUE;

	lct = NULL;
	prev_ls = NULL;
	is_first_tsi = TRUE;

	ratings = NULL;
	prev_rating = NULL;
	is_first_rating = TRUE;

	aeamessage = NULL;
	prev_aea = NULL;
	is_first_aea = TRUE;
	aeamedia = NULL;

	pay = NULL;
	prev_pay = NULL;
	is_first_pay = TRUE;

	obj = NULL;
	prev_obj = NULL;
	is_first_obj = TRUE;

	srctoi = NULL;
	prev_toi = NULL;
	is_first_srctoi = TRUE;

	XML_SetStartElementHandler(parser, startElement_stsid);

	if(XML_Parse(parser, stsid_payload, len, 1) == XML_STATUS_ERROR) {
		fprintf(stderr, "%s at line %ld\n",
			XML_ErrorString(XML_GetErrorCode(parser)),
			XML_GetCurrentLineNumber(parser));
		XML_ParserFree(parser);
		unlock_stsid();
		return NULL;
	}

	XML_ParserFree(parser);
	unlock_stsid();
	return stsid;
}

void FreeSTSID(stsid_t *stsid) {

	route_t *next_file;
	route_t* route;

	lock_stsid();

	/**** Free stsid struct ****/

	next_file = stsid->rs_list;

	while(next_file != NULL) {
		route = next_file;

		if(route->sIpAddr != NULL) {
			free(route->sIpAddr);
		}

		if(route->dIpAddr != NULL) {
			free(route->dIpAddr);
		}

		//if(route->dPort != 0) {
		//	free(route->dPort);
		//}

		next_file = route->next;
		free(route);
	}

	free(stsid);
	unlock_stsid();
}

void Printstsid(stsid_t *stsid) {

	lct_ch_t *next_ls;
	lct_ch_t *ls;
	//file_t* file;

	lock_stsid();

	next_ls = stsid->rs_list->lct_list;
	//file = stsid->rs_list->lct_list->src->FDTinst->file_list;

	while(next_ls != NULL) {	
		ls = next_ls;

#ifdef _MSC_VER
		//printf("TSI: %d\n", ls->tsi);
		//printf("TSI: %d\tRealtime=%d\n", ls->tsi, src->rt);
		//printf("TSI: %d\tRealtime=%d\tExpires: %lld\n", ls->tsi, src->rt, fdtinst->expires);
		printf("TSI: %d\tRealtime=%d\tContent: %s\tFileTemplate: %s\tTOI: %lld\n", ls->tsi, ls->rt, ls->contentType, ls->fileTemplate, ls->toi);
		//printf("TSI: %d\tRealtime=%d\t(TOI=%I64u)\n", ls->tsi, src->rt, file->toi);

#else
		printf("TSI %d \n", ls->tsi); 
		printf("TSI %d (TOI=%llu)\n",  ls->tsi, ls->toi);
#endif
		fflush(stdout);

		next_ls = ls->next;
	}
	unlock_stsid();
}