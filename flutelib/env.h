/** \file fdt.h \brief env parsing

 */

#ifndef _env_H_
#define _env_H_

#include "flute_defines.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Structure for item in the Envelope structure
 * @struct item
 */

typedef struct item {
	struct item* prev;				/**< previous item */
	struct item* next;				/**< next item */

	char* metadataURI;				/**< file name */
	char* contentType;				/**< file content type */
	int version;					/**< file version */
} item_t;

/**
 * Structure for envelope.xml
 * @struct envelope.xml
 */

typedef struct env {
	item_t *item_list;				/**< list of items in the envelope */
	unsigned int nb_of_items;		/**< number of items in the metadataEnvelope */
} env_t;

/**
 * This function decodes env XML document to the env structure using Expat XML library.
 *
 * @param env_payload pointer to buffer containing env XML document
 *
 * @return pointer to the env structure, NULL in error cases
 *
 */

env_t* decode_env_payload(char *env_payload);

/**
 * This function frees env structure.
 *
 * @param env pointer to the env structure
 *
 */

void Freeenv(env_t *env);

/**
 * This function updates env database.
 *
 * @param env_db pointer to env database
 * @param instance pointer to received env Instance
 *
 * @return 1 if existing file description in the env database is complemented or 2
 * when new file description entity is added in the env database, 0 if env database is not
 * updated, -1 in error cases
 *
 */

int update_env(env_t *env_db, env_t *instance);

/**
 * This function returns env information structure for wanted toi.
 *
 * @param env pointer to env database
 * @param toi transport object identifier
 *
 * @return env information structure for wanted toi, NULL if env information does not exists
 *
 */


void Printenv(env_t *env);

/**
 * This function frees file structure.
 *
 * @param file pointer to file structure
 *
 */

void free_item(item_t *item);

#ifdef __cplusplus
}; //extern "C"
#endif

#endif
