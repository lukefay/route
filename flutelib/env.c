/** \file env.c \brief env parsing
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <expat.h>

#ifdef _MSC_VER
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "env.h"
#include "mad_utf8.h"
	
/**
 * Global variable used in parsing
 */

item_t *item;			/**< Envelope items */
item_t *prev;			/**< previous parsed item */
bool is_first_item;		/**< is first item parsed or not? */
env_t *env;				/**< Envelope */


/**
 * This is a private function which copies file description from source to destination. 
 *
 * @param src pointer to source file structure
 * @param dest pointer to destination file structure
 *
 * @return 1 if file description is updated, 0 if not, and -1 in error cases
 *
 */

int copy_item_info(item_t *src, item_t *dest) {

	int updated = 0;

	/* Copy only if particular field is not present in destination, so file description can be only
	complemented not modified */


	if (src->metadataURI != NULL) {

		if (dest->metadataURI == NULL) {

			if (!(dest->metadataURI = (char*)calloc((strlen(src->metadataURI) + 1), sizeof(char)))) {
				printf("Could not alloc memory for item->metadataURI!\n");
				return -1;
			}

			memcpy(dest->metadataURI, src->metadataURI, strlen(src->metadataURI));
			updated = 1;
		}
	}

	if (src->contentType != NULL) {

		if (dest->contentType == NULL) {

			if (!(dest->contentType = (char*)calloc((strlen(src->contentType) + 1), sizeof(char)))) {
				printf("Could not alloc memory for item->contentType!\n");
				return -1;
			}

			memcpy(dest->contentType, src->contentType, strlen(src->contentType));
			updated = 1;
		}
	}

	if(src->version != -1) {
		if(dest->version == -1) {
			dest->version = src->version;
			updated = 1;
		}
	}

	return updated;
}


/**
 * This is a private function which is used in FDT parsing.
 *
 * @param userData not used, must be
 * @param name pointer to buffer containing element's name
 * @param atts pointer to buffer containing element's attributes
 *
 */

static void startElement_env(void *userData, const char *name, const char **atts) {

	char* mbstr;

	while(*atts != NULL) {
		if(!strcmp(name, "item")) {

			if(item == NULL) {
				if(!(item = (item_t*)calloc(1, sizeof(item_t)))) {
					printf("Could not alloc memory for envelope item!\n");
					fflush(stdout);
					return;
				}

				/* initialise file parameters */
				item->prev = NULL;
				item->next = NULL;
				item->metadataURI = NULL;
				item->contentType = NULL;
				item->version = 0;

				env->nb_of_items++;
			}

			if(!strcmp(*atts, "metadataURI")) {

				atts++;

				if (!(mbstr = (char*)calloc((strlen(*atts) + 1), sizeof(char)))) {
					printf("Could not alloc memory for mbstr!\n");
					fflush(stdout);
					return;
				}

				x_utf8s_to_iso_8859_1s(mbstr, *atts, strlen(*atts));

				if(!(item->metadataURI = (char*)calloc((size_t)(strlen(mbstr) + 1), sizeof(char)))) {
					printf("Could not alloc memory for item->metadataURI!\n");
					fflush(stdout);
					return;
				}

				memcpy(item->metadataURI, mbstr, strlen(mbstr));
				free(mbstr);

				if (is_first_item) {
					env->item_list = item;
					is_first_item = false;
				}
				else {
					prev->next = item;
					item->prev = prev;
				}

				prev = item;

			}
			else if(!strcmp(*atts, "contentType")) {

				atts++;

				if (!(mbstr = (char*)calloc((strlen(*atts) + 1), sizeof(char)))) {
					printf("Could not alloc memory for mbstr!\n");
					fflush(stdout);
					return;
				}

				x_utf8s_to_iso_8859_1s(mbstr, *atts, strlen(*atts));

				if(!(item->contentType = (char*)calloc((size_t)(strlen(mbstr) + 1), sizeof(char)))) {
					printf("Could not alloc memory for item->contentType!\n");
					fflush(stdout);
					return;
				}

				memcpy(item->contentType, mbstr, strlen(mbstr));
				free(mbstr);
			}
			else if(!strcmp(*atts, "version")) {

				item->version = (unsigned short)atoi(*(++atts));
			}
			else {
				atts++;
			}
			atts++;

		}
		else if(!strcmp(name, "metadataEnvelope")) {

			atts++;

		}
		else {
			atts += 2;
		}
	}

	item = NULL;
}

env_t* decode_env_payload(char *env_payload) {

	XML_Parser parser = XML_ParserCreate(NULL);
	size_t len;

	len = strlen(env_payload);
	env = NULL;

	if(!(env = (env_t*)calloc(1, sizeof(env_t)))) {
		printf("Could not alloc memory for env!\n");
		XML_ParserFree(parser);

		return NULL;
	}

	/* initialise envelope parameters */

	env->item_list = NULL;
	env->nb_of_items = 0;  

	item = NULL;
	prev = NULL;
	is_first_item = true;

	XML_SetStartElementHandler(parser, startElement_env);

	if(XML_Parse(parser, env_payload, len, 1) == XML_STATUS_ERROR) {
		fprintf(stderr, "%s at line %ld\n",
			XML_ErrorString(XML_GetErrorCode(parser)),
			XML_GetCurrentLineNumber(parser));
		XML_ParserFree(parser);

		return NULL;
	}

	//printf("parsed envelope XML parameters with %d item(s) and length %u\n", env->nb_of_items, len);
	//fflush(stdout);

	XML_ParserFree(parser);

	return env;
}

void Freeenv(env_t *env) {

	item_t *next_item;
	item_t *item;

	/**** Free env struct ****/

	next_item = env->item_list;

	while(next_item != NULL) {
		item = next_item;

		if(item->metadataURI != NULL) {
			free(item->metadataURI);
		}

		if(item->contentType != NULL) {
			free(item->contentType);
		}

		next_item = item->next;
		free(item);
	}

	free(env);
}

int update_env(env_t *env_db, env_t *instance) {

	item_t *tmp_item;
	item_t *env_item;
	item_t *new_item;
	int retval = 0;
	int updated = 0;

	assert (env_db != NULL);
	assert (instance != NULL);

	tmp_item = instance->item_list;

	while(tmp_item != NULL) {

		env_item = env_db->item_list;

		for(;; env_item = env_item->next) {

			if(tmp_item->version == env_item->version) {

				retval = copy_item_info(tmp_item, env_item);

				if(retval < 0) {
					return -1;
				}
				else if(((retval == 1)&&(updated != 2))) {
					updated = 1;
				}

				break;
			}
			else if(env_item->next != NULL) {
				continue;
			}
			else {

				if(!(new_item = (item_t*)calloc(1, sizeof(item_t)))) {
					printf("Could not alloc memory for mad_fdt file!\n");

					return -1;
				}

				new_item->version = 0;

				retval = copy_item_info(tmp_item, new_item);

				if(retval < 0) {
					return -1;
				}
				else if(retval == 1) {
					updated = 2;
				}

				new_item->next = env_item->next;
				new_item->prev = env_item;
				env_item->next = new_item;

				break;
			}
		}

		tmp_item = tmp_item->next;
	}

	return updated;
}

void Printenv(env_t *env) {

	item_t *next_item;
	item_t *item;

	next_item = env->item_list;

	while(next_item != NULL) {	
		item = next_item;

#ifdef _MSC_VER
		printf("FILE URI: %s\n",  item->metadataURI);
#else
		printf("FILE URI: %s\n",  item->metadataURI);
#endif
		fflush(stdout);

		next_item = item->next;
	}

}

void free_item(item_t *item) {

	if (item->metadataURI != NULL) {
		free(item->metadataURI);
	}

	if (item->contentType != NULL) {
		free(item->contentType);
	}

	free(item);
}
