/* mime-parser.h - Parse MIME structures (high level rfc822 parser).
 * Copyright (C) 2016 g10 Code GmbH
 * Copyright (C) 2016 Bundesamt f?r Sicherheit in der Informationstechnik
 *
 * This file is part of GnuPG.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef GNUPG_MIME_PARSER_H
#define GNUPG_MIME_PARSER_H

#include "rfc822parse.h"

struct mime_parser_context_s;
typedef struct mime_parser_context_s *mime_parser_t;
/* We would really like to use bit-fields in a struct, but using
   structs as return values can cause binary compatibility issues, in
   particular if you want to do it effeciently (also see
   -freg-struct-return option to GCC).  */
typedef unsigned int gpg_error_t;

gpg_error_t mime_parser_new(mime_parser_t *r_ctx, void *cookie);
void        mime_parser_release(mime_parser_t ctx);

void mime_parser_set_verbose(mime_parser_t ctx, int level);
void mime_parser_set_t2body(mime_parser_t ctx,
                             gpg_error_t (*fnc) (void *cookie, int level));
void mime_parser_set_new_part(mime_parser_t ctx,
                               gpg_error_t (*fnc) (void *cookie,
                                                   const char *mediatype,
                                                   const char *mediasubtype));
void mime_parser_set_part_data(mime_parser_t ctx,
                                gpg_error_t (*fnc) (void *cookie,
                                                    const void *data,
                                                    size_t datalen));
void mime_parser_set_collect_encrypted(mime_parser_t ctx,
                                        gpg_error_t (*fnc) (void *cookie,
                                                            const char *data));
void mime_parser_set_collect_signeddata(mime_parser_t ctx,
                                         gpg_error_t (*fnc) (void *cookie,
                                                             const char *data));
void mime_parser_set_collect_signature(mime_parser_t ctx,
                                        gpg_error_t (*fnc) (void *cookie,
                                                            const char *data));

//gpg_error_t mime_parser_parse (mime_parser_t ctx, estream_t fp);
gpg_error_t mime_parser_parse(mime_parser_t ctx, FILE *fp);


rfc822parse_t mime_parser_rfc822parser(mime_parser_t ctx);

int message_cb(void* opaque, rfc822parse_event_t event, rfc822parse_t msg);

void parse_message(FILE* fp, char* session_basedir);

/*-- b64enc.c and b64dec.c --*/
struct b64state
{
    unsigned int flags;
    int idx;
    int quad_count;
    FILE* fp;
    char* title;
    unsigned char radbuf[4];
    //u32 crc;
    short crc;
    int stop_seen : 1;
    int invalid_encoding : 1;
    gpg_error_t lasterr;
};

gpg_error_t b64enc_start(struct b64state* state, FILE* fp, const char* title);
gpg_error_t b64enc_write(struct b64state* state, const void* buffer, size_t nbytes);
gpg_error_t b64enc_finish(struct b64state* state);
gpg_error_t b64dec_start(struct b64state* state, const char* title);
gpg_error_t b64dec_proc(struct b64state* state, void* buffer, size_t length, size_t* r_nbytes);
gpg_error_t b64dec_finish(struct b64state* state);

/*-- Macros to replace ctype ones to avoid locale problems. --*/
#define spacep(p)   (*(p) == ' ' || *(p) == '\t')
#define digitp(p)   (*(p) >= '0' && *(p) <= '9')
#define alphap(p)   ((*(p) >= 'A' && *(p) <= 'Z')       \
                     || (*(p) >= 'a' && *(p) <= 'z'))
#define alnump(p)   (alphap (p) || digitp (p))
#define hexdigitp(a) (digitp (a)                     \
                      || (*(a) >= 'A' && *(a) <= 'F')  \
                      || (*(a) >= 'a' && *(a) <= 'f'))
  /* Note this isn't identical to a C locale isspace() without \f and
     \v, but works for the purposes used here. */
#define ascii_isspace(a) ((a)==' ' || (a)=='\n' || (a)=='\r' || (a)=='\t')

     /* The atoi macros assume that the buffer has only valid digits. */
#define atoi_1(p)   (*(p) - '0' )
#define atoi_2(p)   ((atoi_1(p) * 10) + atoi_1((p)+1))
#define atoi_4(p)   ((atoi_2(p) * 100) + atoi_2((p)+2))
#define xtoi_1(p)   (*(p) <= '9'? (*(p)- '0'): \
                     *(p) <= 'F'? (*(p)-'A'+10):(*(p)-'a'+10))
#define xtoi_2(p)   ((xtoi_1(p) * 16) + xtoi_1((p)+1))
#define xtoi_4(p)   ((xtoi_2(p) * 256) + xtoi_2((p)+2))

#endif /*GNUPG_MIME_PARSER_H*/