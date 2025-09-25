/** \file stsid.h \brief S-TSID parsing

 */

#ifndef _STSID_H_
#define _STSID_H_

#include "efdt.h"
#include "fdt.h"
#include "flute_defines.h"
#include "../alclib/defines.h"
#include "../alclib/utils.h"

#ifdef __cplusplus
extern "C" {
#endif


/**
 * Structure for Content Rating.
 * @struct rating
 */

typedef struct rating {
	struct rating *prev;		/**< previous item */
	struct rating *next;		/**< next item */

	char *schemeIdUri;			/**< Content advisory rating scheme */
	char *value;				/**< Content advisory rating value */
} rating_t;

/**
 * Structure for Advanced Emergency Alerts.
 * @struct aea
 */
typedef struct aeaid {
	struct aeaid *prev;			/**< previous item */
	struct aeaid *next;			/**< next item */

	char* aeaid;				/**< List of AEA message IDs */
} aeaid_t;

typedef struct aea {
	aeaid_t *aeaid_list;		/**< List of AEA messages */
	unsigned int nb_of_aea;		/**< number of AEA media files in the AEA message */
} aea_t;

/**
 * Structure for optional Payload element.
 * @struct payload
 */

typedef struct payload {
	struct payload *prev;		/**< previous item */
	struct payload *next;		/**< next item */

	unsigned int codePoint;		/**< number for the combination of values in Payload element */
	unsigned int formatId;		/**< payload format of the object */
	unsigned int frag; 			/**< flag if objects are fragmented */
	BOOL order;					/**< flag if objects are in order delivered */
	unsigned int srcFecPayloadId;	/**< implied meaning of FEC Payload ID header */
} payload_t;

/**
 * Structure for Source TOI derivations in Repair Flow.
 * @struct stoi
 */

typedef struct stoi {
	struct stoi *prev;			/**< previous item */
	struct stoi *next;			/**< next item */

	unsigned short x;				/**< constant x value for deriving TOI in super-object */
	unsigned short y;				/**< constant y value for deriving TOI in super-object */
} stoi_t;
	
/**
 * Structure for Protected Objects in Repair Flow.
 * @struct protectobj
 */

typedef struct protectobj {
	struct protectobj *prev;	/**< previous item */
	struct protectobj *next;	/**< next item */

	char *sessionDescription;	/**< session of the source flow */
	unsigned int objecttsi;		/**< TSI for the protected Source Flow */
	stoi_t *stoi_list;			/**< List of source toi deriviations */
	unsigned int nb_of_stoi;	/**< number of protected TOI in the source flow */
} protectobj_t;

/**
 * Structure for LCT Channel.
 * @struct lct
 */

typedef struct lct_ch {
	struct lct_ch *prev;		/**< previous item */
	struct lct_ch *next;		/**< next item */

	unsigned int tsi;			/**< transport session ID */
	unsigned int bw;			/**< maximum bandwidth */
	char *startTime;			/**< Presentation Start time */
	char *endTime;				/**< Presentation End time */

	// SOURCE FLOW
	BOOL rt;					/**< Real Time content flag */
	unsigned int minBuffSize;	/**< minimum kBytes for transport buffer */
	payload_t* payload_list;	/**< OPTIONAL list of payload ROUTE packets */
	unsigned int nb_of_payload;	/**< number of payloads in the source flow */

	// FDT INSTANCE
	fdt_t* fdt;				/**< EFDT instance in LCT */
	unsigned long long expires;	/**< fdt expiration time in NTP-time */
	BOOL complete;				/**< is complete FDT? */
	file_t* file_list;			/**< list of files in the FDT*/
	unsigned int nb_of_files;	/**< number of file in the FDT*/

	/* Other common parameters */
	char* type;					/**< default content type */
	char* encoding;				/**< default content encoding */

	/* FEC OTI */
	short fec_enc_id;			/**< default FEC encoding id */
	int fec_inst_id;			/**< default FEC instance id */
	unsigned char finite_field;	/**< default finite field parameter  with new RS FEC */
	unsigned char nb_of_es_per_group;	/**< default number of encoding symbols in packet with new RS FEC */
	unsigned int max_sb_len;	/**< default maximum source block length */
	unsigned short es_len;		/**< default encoding symbol length */
	unsigned short max_nb_of_es;/**< default maximum number of encoding symbols */

	// AFDT 
	unsigned int efdtVersion;	/**< EFDT version */
	unsigned int maxExpiresDelta;/**< Maximum Expiration Delta */
	unsigned int maxTransportSize;/**< Maximum Tranport size */
	char* fileTemplate;			/**< File Template */
	char* appContextIdList;		/**< Application ContextID list */
	unsigned int filterCodes;	/**< Filter Codes */
	unsigned int maxCacheMemory;/**< Maximum Cache Memory required for RaptorQ operation */
	BOOL ext_order;				/**< ATSC defined extension flag if objects are in-order delivered */

	// fdt:FILE
	int status;					/**< status of the file (0 = not wanted, 1 = downloading, 2 = downloaded) */
	unsigned long long toi;		/**< transport object identifier */
	unsigned long long content_len;	/**< length of the file */
	char* location;				/**< content location, file URI */
	char* md5;					/**< MD5 checksum for the file */

	/* FEC OTI, only at file level */
	unsigned long long transfer_len;	/**< transport length */

	// MEDIA INFO
	BOOL startup;				/**< MPD-less startup flag */
	char* lang;					/**< audio language */
	char* contentType;			/**< media type */
	char* repId;				/**< Representation ID */
	rating_t* rating_list;		/**< List of content ratings */
	unsigned int nb_of_ratings;	/**< number of ratings in the media */

	// AEA MEDIA
	aeaid_t* aeaid_list;		/**< List of AEA media files */
	unsigned int nb_of_aea;		/**< number of AEA media files in the AEA message */

	// PAYLOAD -- OPTIONAL !!!
	unsigned int codePoint;		/**< number for the combination of values in Payload element */
	unsigned int formatId;		/**< payload format of the object */
	unsigned int frag; 			/**< flag if objects are fragmented */
	BOOL order;					/**< flag if objects are in order delivered */
	unsigned int srcFecPayloadId;	/**< implied meaning of FEC Payload ID header */


	// REPAIR FLOW
	// FEC PARAMETERS
	unsigned int maximumDelay;	/**< delay between source packet in source flow vs. repair flow */
	unsigned short overhead;	/**< Percentage of FEC overhead */
	unsigned int fecMinBuffSize;/**< Repair flow minimum buffers size for transport super-object */
	char* fecOTI;				/**< FEC related info */
	unsigned short percentRepair;/**< Maximum ratio of repair symbols to source symbols as percent 0-200 */
	char* checksumList;			/**< List of CRC32 hexadecimal checksums for each Source Block in susecutive order */
	protectobj_t* obj_list;		/**< list of source flows protected by this repair flow */
	unsigned int nb_of_obj;		/**< number of protected objects in the source flow */

	// PROTECTED OBJECT
	char* sessionDescription;	/**< session of the source flow */
	unsigned int objecttsi;		/**< TSI for the protected Source Flow */
	stoi_t* stoi_list;			/**< List of source toi deriviations */
	unsigned int nb_of_stoi;	/**< number of protected TOI in the source flow */

} lct_ch_t;

/**
 * Structure for ROUTE Session.
 * @struct rs
 */

typedef struct route {
	struct route *prev;			/**< previous item */
	struct route *next;			/**< next item */

	char *sIpAddr;				/**< Source IP address */
	char *dIpAddr;				/**< Destination IP address */
	char *dPort;				/**< Destination IP port */
	lct_ch_t *lct_list;			/**< List of LCT Channels per ROUTE Session */
	unsigned int nb_of_ls;		/**< number of LCT Channels per ROUTE Session */
} route_t;

/**
 * Structure for S-TSID.
 * @struct stsid
 */

typedef struct stsid {
	route_t *rs_list;			/**< List of ROUTE Sessions */
	unsigned int nb_of_rs;		/**< number of ROUTE Sessions in the S-TSID */
} stsid_t;


/**
 * This function decodes S-TSID XML document to the stsid structure using Expat XML library.
 *
 * @param stsid_payload pointer to buffer containing S-TSID XML document
 *
 * @return pointer to the stsid structure, NULL in error cases
 *
 */

stsid_t* decode_stsid_payload(char *stsid_payload);

/**
 * This function frees S-TSID structure.
 *
 * @param stsid pointer to the S-TSID structure
 *
 */

void FreeSTSID(stsid_t *stsid);

/**
 * This function returns S-TSID information structure for wanted toi.
 *
 * @param stsid pointer to S-TSID database
 * @param rs ROUTE Session identifier
 *
 * @return S-TSID information structure for wanted toi, NULL if S-TSID information does not exists
 *
 */


void Printstsid(stsid_t *stsid);

#ifdef __cplusplus
}; //extern "C"
#endif

#endif
