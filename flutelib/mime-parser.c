/* mime-parser.c - Parse MIME structures (high level rfc822 parser).
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

//#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

//#include "util.h"
#include "rfc822parse.h"
#include "mime-parser.h"

enum pgpmime_states
  {
    PGPMIME_NONE = 0,
    PGPMIME_WAIT_ENCVERSION,
    PGPMIME_IN_ENCVERSION,
    PGPMIME_WAIT_ENCDATA,
    PGPMIME_IN_ENCDATA,
    PGPMIME_GOT_ENCDATA,
    PGPMIME_WAIT_SIGNEDDATA,
    PGPMIME_IN_SIGNEDDATA,
    PGPMIME_WAIT_SIGNATURE,
    PGPMIME_IN_SIGNATURE,
    PGPMIME_GOT_SIGNATURE,
    PGPMIME_INVALID
  };

/* Option flags. */
static int verbose;
static int debug;
static int opt_crypto;    /* Decrypt or verify messages. */
static int opt_no_header; /* Don't output the header lines. */

/* Structure used to communicate with the parser callback. */
struct parse_info_s {
    int show_header;             /* Show the header lines. */
    int show_data;               /* Show the data lines. */
    unsigned int skip_show;      /* Temporary disable above for these
                                     number of lines. */
    int show_data_as_note;       /* The next data line should be shown
                                    as a note. */
    int show_boundary;
    int nesting_level;

    int is_pkcs7;                /* Old style S/MIME message. */

    int smfm_state;              /* State of PGP/MIME or S/MIME parsing.  */
    int is_smime;                /* This is S/MIME and not PGP/MIME. */

    const char* signing_protocol;
    const char* signing_protocol_2; /* there are two ways to present
                                       PKCS7 */
    int hashing_level;           /* The nesting level we are hashing. */
    int hashing;
    FILE* hash_file;

    FILE* sig_file;              /* Signature part with MIME or full
                                    pkcs7 data if IS_PCKS7 is set. */
    int  verify_now;             /* Flag set when all signature data is
                                    available. */
    const char* file;                    // Filename to parse message into
};

/* Definition of the mime parser object.  */
struct mime_parser_context_s
{
  void *cookie;                /* Cookie passed to all callbacks.  */

  /* The callback to announce the transition from header to body.  */
  gpg_error_t (*t2body) (void *cookie, int level);

  /* The callback to announce a new part.  */
  gpg_error_t (*new_part) (void *cookie,
                           const char *mediatype,
                           const char *mediasubtype);
  /* The callback to return data of a part.  */
  gpg_error_t (*part_data) (void *cookie,
                            const void *data,
                            size_t datalen);
  /* The callback to collect encrypted data.  */
  gpg_error_t (*collect_encrypted) (void *cookie, const char *data);
  /* The callback to collect signed data.  */
  gpg_error_t (*collect_signeddata) (void *cookie, const char *data);
  /* The callback to collect a signature.  */
  gpg_error_t (*collect_signature) (void *cookie, const char *data);

  /* The RFC822 parser context is stored here during callbacks.  */
  rfc822parse_t msg;

  /* Helper to convey error codes from user callbacks.  */
  gpg_error_t err;

  int nesting_level;           /* The current nesting level.  */
  int hashing_at_level;        /* The nesting level at which we are hashing. */
  enum pgpmime_states pgpmime; /* Current PGP/MIME state.  */
  unsigned int delay_hashing:1;/* Helper for PGPMIME_IN_SIGNEDDATA. */
  unsigned int want_part:1;    /* Return the current part.  */
  unsigned int decode_part:2;  /* Decode the part.  1 = QP, 2 = Base64. */

  unsigned int verbose:1;      /* Enable verbose mode.  */
  unsigned int debug:1;        /* Enable debug mode.  */

  /* Flags to help with debug output.  */
  struct {
    unsigned int n_skip;         /* Skip showing these number of lines.  */
    unsigned int header:1;       /* Show the header lines.  */
    unsigned int data:1;         /* Show the data lines.  */
    unsigned int as_note:1;      /* Show the next data line as a note.  */
    unsigned int boundary : 1;
  } show;

  struct b64state *b64state;     /* NULL or malloced Base64 decoder state.  */

  /* A buffer for reading a mail line,  */
  char line[5000];
};

/* Print diagnostic message and exit with failure. */
static void
die(const char* format, ...)
{
    fflush(stdout);
    fprintf(stderr, "%s: \n", format);

    exit(1);
}


/* Print diagnostic message. */
static void
err(const char* format, ...)
{
    fflush(stdout);
    fprintf(stderr, "%s: \n", format);

}

static void*
xmalloc(size_t n)
{
    void* p = malloc(n);
    if (!p)
        die("out of core: %s", strerror(errno));
    return p;
}

#ifndef HAVE_STPCPY
char*
stpcpy(char* a, const char* b)
{
    while (*b)
        *a++ = *b++;
    *a = 0;

    return (char*)a;
}
#endif

/* Print the event received by the parser for debugging.  */
static void
show_message_parser_event(rfc822parse_event_t event)
{
  const char *s;

  switch (event)
    {
    case RFC822PARSE_OPEN: s= "Open"; break;
    case RFC822PARSE_CLOSE: s= "Close"; break;
    case RFC822PARSE_CANCEL: s= "Cancel"; break;
    case RFC822PARSE_T2BODY: s= "T2Body"; break;
    case RFC822PARSE_FINISH: s= "Finish"; break;
    case RFC822PARSE_RCVD_SEEN: s= "Rcvd_Seen"; break;
    case RFC822PARSE_LEVEL_DOWN: s= "Level_Down"; break;
    case RFC822PARSE_LEVEL_UP: s= "Level_Up"; break;
    case RFC822PARSE_BOUNDARY: s= "Boundary"; break;
    case RFC822PARSE_LAST_BOUNDARY: s= "Last_Boundary"; break;
    case RFC822PARSE_BEGIN_HEADER: s= "Begin_Header"; break;
    case RFC822PARSE_PREAMBLE: s= "Preamble"; break;
    case RFC822PARSE_EPILOGUE: s= "Epilogue"; break;
    default: s= "[unknown event]"; break;
    }
  printf("*** RFC822 event %s\n", s);
}


/* Do in-place decoding of quoted-printable data of LENGTH in BUFFER.
   Returns the new length of the buffer and stores true at R_SLBRK if
   the line ended with a soft line break; false is stored if not.
   This function assumes that a complete line is passed in
   buffer.  */
static size_t
qp_decode(char *buffer, size_t length, int *r_slbrk)
{
  char *d, *s;

  if (r_slbrk)
    *r_slbrk = 0;

  /* Fixme:  We should remove trailing white space first.  */
  for (s=d=buffer; length; length--)
    {
      if (*s == '=')
        {
          if (length > 2 && hexdigitp (s+1) && hexdigitp (s+2))
            {
              s++;
              *(unsigned char*)d++ = xtoi_2 (s);
              s += 2;
              length -= 2;
            }
          else if (length > 2 && s[1] == '\r' && s[2] == '\n')
            {
              /* Soft line break.  */
              s += 3;
              length -= 2;
              if (r_slbrk && length == 1)
                *r_slbrk = 1;
            }
          else if (length > 1 && s[1] == '\n')
            {
              /* Soft line break with only a Unix line terminator. */
              s += 2;
              length -= 1;
              if (r_slbrk && length == 1)
                *r_slbrk = 1;
            }
          else if (length == 1)
            {
              /* Soft line break at the end of the line. */
              s += 1;
              if (r_slbrk)
                *r_slbrk = 1;
            }
          else
            *d++ = *s++;
        }
      else
        *d++ = *s++;
    }

  return d - buffer;
}


/* This function is called by parse_mail to communicate events.  This
 * callback communicates with the caller using a structure passed in
 * OPAQUE.  Should return 0 on success or set ERRNO and return -1. */
static int
parse_message_cb(void *opaque, rfc822parse_event_t event, rfc822parse_t msg)
{
  mime_parser_t ctx = opaque;
  const char *s;
  int rc = 0;

  /* Make the RFC822 parser context available for callbacks.  */
  ctx->msg = msg;

  if (ctx->debug)
    show_message_parser_event(event);

  if (event == RFC822PARSE_BEGIN_HEADER || event == RFC822PARSE_T2BODY)
    {
      /* We need to check here whether to start collecting signed data
       * because attachments might come without header lines and thus
       * we won't see the BEGIN_HEADER event.  */
      if (ctx->pgpmime == PGPMIME_WAIT_SIGNEDDATA)
        {
          if (ctx->debug)
            printf("begin_hash\n");
          ctx->hashing_at_level = ctx->nesting_level;
          ctx->pgpmime = PGPMIME_IN_SIGNEDDATA;
          ctx->delay_hashing = 0;
        }
    }

  if (event == RFC822PARSE_OPEN)
    {
      /* Initialize for a new message. */
      ctx->show.header = 1;
    }
  else if (event == RFC822PARSE_T2BODY)
    {
      rfc822parse_field_t field;

      ctx->want_part = 0;
      ctx->decode_part = 0;

      if (ctx->t2body)
        {
          rc = ctx->t2body (ctx->cookie, ctx->nesting_level);
          if (rc)
            goto t2body_leave;
        }

      field = rfc822parse_parse_field(msg, "Content-Type", -1);
      if (field)
        {
          const char *s1, *s2;

          s1 = rfc822parse_query_media_type(field, &s2);
          if (s1)
            {
              if (ctx->verbose)
                printf("h media: %*s%s %s\n",
                           ctx->nesting_level*2, "", s1, s2);
              if (ctx->pgpmime == PGPMIME_WAIT_ENCVERSION)
                {
                  if (!strcmp(s1, "application")
                      && !strcmp(s2, "pgp-encrypted"))
                    {
                      if (ctx->debug)
                        printf("c begin_encversion\n");
                      ctx->pgpmime = PGPMIME_IN_ENCVERSION;
                    }
                  else
                    {
                      printf("invalid PGP/MIME structure;"
                                 " expected '%s', got '%s/%s'\n",
                                 "application/pgp-encrypted", s1, s2);
                      ctx->pgpmime = PGPMIME_INVALID;
                    }
                }
              else if (ctx->pgpmime == PGPMIME_WAIT_ENCDATA)
                {
                  if (!strcmp(s1, "application")
                      && !strcmp(s2, "octet-stream"))
                    {
                      if (ctx->debug)
                        printf("c begin_encdata\n");
                      ctx->pgpmime = PGPMIME_IN_ENCDATA;
                    }
                  else
                    {
                      printf("invalid PGP/MIME structure;"
                                 " expected '%s', got '%s/%s'\n",
                                 "application/octet-stream", s1, s2);
                      ctx->pgpmime = PGPMIME_INVALID;
                    }
                }
              else if (ctx->pgpmime == PGPMIME_WAIT_SIGNATURE)
                {
                  if (!strcmp(s1, "application")
                      && !strcmp(s2, "pgp-signature"))
                    {
                      if (ctx->debug)
                        printf("c begin_signature\n");
                      ctx->pgpmime = PGPMIME_IN_SIGNATURE;
                    }
                  else
                    {
                      printf("invalid PGP/MIME structure;"
                                 " expected '%s', got '%s/%s'\n",
                                 "application/pgp-signature", s1, s2);
                      ctx->pgpmime = PGPMIME_INVALID;
                    }
                }
              else if (!strcmp(s1, "multipart")
                       && !strcmp(s2, "encrypted"))
                {
                  s = rfc822parse_query_parameter(field, "protocol", 0);
                  if (s)
                    {
                      if (ctx->debug)
                        printf("h encrypted.protocol: %s\n", s);
                      if (!strcmp (s, "application/pgp-encrypted"))
                        {
                          if (ctx->pgpmime)
                            printf("note: "
                                       "ignoring nested PGP/MIME signature\n");
                          else
                            ctx->pgpmime = PGPMIME_WAIT_ENCVERSION;
                        }
                      else if (ctx->verbose)
                        printf("# this protocol is not supported\n");
                    }
                }
              else if (!strcmp(s1, "multipart")
                       && !strcmp(s2, "signed"))
                {
                  s = rfc822parse_query_parameter (field, "protocol", 1);
                  if (s)
                    {
                      if (ctx->debug)
                        printf("h signed.protocol: %s\n", s);
                      if (!strcmp (s, "application/pgp-signature"))
                        {
                          if (ctx->pgpmime)
                            printf("note: "
                                       "ignoring nested PGP/MIME signature\n");
                          else
                            ctx->pgpmime = PGPMIME_WAIT_SIGNEDDATA;
                        }
                      else if (ctx->verbose)
                        printf("# this protocol is not supported\n");
                    }
                }
              else if (ctx->new_part)
                {
                  ctx->err = ctx->new_part (ctx->cookie, s1, s2);
                  if (!ctx->err)
                    ctx->want_part = 1;
                  //else if (gpg_err_code (ctx->err) == GPG_ERR_FALSE)
                  //else if (gpg_err_code(ctx->err) == 0)
                  //    ctx->err = 0;
                  //else if (gpg_err_code (ctx->err) == GPG_ERR_TRUE)
                  else if (ctx->err == 1)
                    {
                      ctx->want_part = ctx->decode_part = 1;
                      ctx->err = 0;
                    }
                }
            }
          else
            {
              if (ctx->debug)
                printf("h media: %*s none\n", ctx->nesting_level*2, "");
              if (ctx->new_part)
                {
                  ctx->err = ctx->new_part(ctx->cookie, "", "");
                  if (!ctx->err)
                    ctx->want_part = 1;
                  //else if (gpg_err_code (ctx->err) == GPG_ERR_FALSE)
                  //else if (gpg_err_code(ctx->err) == 0)
                  //  ctx->err = 0;
                  //else if (gpg_err_code (ctx->err) == GPG_ERR_TRUE)
                  else if (ctx->err == 1)
                    {
                      ctx->want_part = ctx->decode_part = 1;
                      ctx->err = 0;
                    }
                }
            }

          rfc822parse_release_field(field);
        }
      else
        {
          if (ctx->verbose)
            printf("h media: %*stext plain [assumed]\n",
                       ctx->nesting_level*2, "");
          if (ctx->new_part)
            {
              ctx->err = ctx->new_part(ctx->cookie, "text", "plain");
              if (!ctx->err)
                ctx->want_part = 1;
              //else if (gpg_err_code (ctx->err) == GPG_ERR_FALSE)
              //else if (gpg_err_code(ctx->err) == 0)
              //  ctx->err = 0;
              //else if (gpg_err_code (ctx->err) == GPG_ERR_TRUE)
              else if (ctx->err == 1)
                {
                  ctx->want_part = ctx->decode_part = 1;
                  ctx->err = 0;
                }
            }
        }

      /* Figure out the encoding if needed.  */
      if (ctx->decode_part)
        {
          char *value;
          size_t valueoff;

          ctx->decode_part = 0; /* Fallback for unknown encoding.  */
          value = rfc822parse_get_field(msg, "Content-Transfer-Encoding", -1,
                                         &valueoff);
          if (value)
            {
              //if (!stricmp(value+valueoff, "quoted-printable")) /* Case in-sensitive */
              if (!strcmp(value + valueoff, "quoted-printable")) /* case sensitive */
                      ctx->decode_part = 1;
              //else if (!stricmp(value+valueoff, "base64")) /* Case in-sensitive */
              else if (!strcmp(value + valueoff, "base64")) /* Case sensitive */
              {
                  ctx->decode_part = 2;
//                  if (ctx->b64state)
//                    b64dec_finish(ctx->b64state); /* Reuse state.  */
//                  else
//                    {
//                      //ctx->b64state = xtrymalloc (sizeof *ctx->b64state);
//                      if (!ctx->b64state)
//                        //rc = gpg_error_from_syserror();
//                  }
//                  if (!rc)
//                    rc = b64dec_start(ctx->b64state, NULL);
                }
              free (value); /* Right, we need a plain free.  */
            }
        }

    t2body_leave:
      ctx->show.header = 0;
      ctx->show.data = 1;
      ctx->show.n_skip = 1;
    }
  else if (event == RFC822PARSE_PREAMBLE)
    ctx->show.as_note = 1;
  else if (event == RFC822PARSE_LEVEL_DOWN)
    {
      if (ctx->debug)
        printf("b down\n");
      ctx->nesting_level++;
    }
  else if (event == RFC822PARSE_LEVEL_UP)
    {
      if (ctx->debug)
        printf("b up\n");
      if (ctx->nesting_level)
        ctx->nesting_level--;
      else
        printf("invalid structure (bad nesting level)\n");
    }
  else if (event == RFC822PARSE_BOUNDARY || event == RFC822PARSE_LAST_BOUNDARY)
    {
      ctx->show.data = 0;
      ctx->show.boundary = 1;
      if (event == RFC822PARSE_BOUNDARY)
        {
          ctx->show.header = 1;
          ctx->show.n_skip = 1;
          if (ctx->debug)
            printf("b part\n");
        }
      else if (ctx->debug)
        printf("b last\n");

      if (ctx->pgpmime == PGPMIME_IN_ENCDATA)
        {
          if (ctx->debug)
            printf("c end_encdata\n");
          ctx->pgpmime = PGPMIME_GOT_ENCDATA;
          /* FIXME: We should assert (event == LAST_BOUNDARY).  */
        }
      else if (ctx->pgpmime == PGPMIME_IN_SIGNEDDATA
               && ctx->nesting_level == ctx->hashing_at_level)
        {
          if (ctx->debug)
            printf("c end_hash\n");
          ctx->pgpmime = PGPMIME_WAIT_SIGNATURE;
          if (ctx->collect_signeddata)
            ctx->err = ctx->collect_signeddata (ctx->cookie, NULL);
        }
      else if (ctx->pgpmime == PGPMIME_IN_SIGNATURE)
        {
          if (ctx->debug)
            printf("c end_signature\n");
          ctx->pgpmime = PGPMIME_GOT_SIGNATURE;
          /* FIXME: We should assert (event == LAST_BOUNDARY).  */
        }
      else if (ctx->want_part)
        {
          if (ctx->part_data)
            {
              /* FIXME: We may need to flush things.  */
              ctx->err = ctx->part_data (ctx->cookie, NULL, 0);
            }
          ctx->want_part = 0;
        }
    }

  ctx->msg = NULL;

  return rc;
}


/* Create a new mime parser object.  COOKIE is a values which will be
 * used as first argument for all callbacks registered with this
 * parser object.  */
gpg_error_t
mime_parser_new (mime_parser_t *r_parser, void *cookie)
{
  mime_parser_t ctx;

  *r_parser = NULL;

//  ctx = xtrycalloc (1, sizeof *ctx);
  ctx = calloc(1, sizeof * ctx);
  if (!ctx)
    //return gpg_error_from_syserror ();
    return 65535;
  ctx->cookie = cookie;

  *r_parser = ctx;
  return 0;
}


/* Release a mime parser object.  */
void
mime_parser_release (mime_parser_t ctx)
{
  if (!ctx)
    return;

  if (ctx->b64state)
    {
//      b64dec_finish (ctx->b64state);
//      //xfree(ctx->b64state);
      free(ctx->b64state);
    }
  //xfree (ctx);
  free(ctx);
}


/* Set verbose and debug mode.  */
void
mime_parser_set_verbose (mime_parser_t ctx, int level)
{
  if (!level)
    {
      ctx->verbose = 0;
      ctx->debug = 0;
    }
  else
    {
      ctx->verbose = 1;
      if (level > 10)
        ctx->debug = 1;
    }
}


/* Set a callback for the transition from header to body.  LEVEL is
 * the current nesting level, starting with 0.  This callback can be
 * used to evaluate headers before any other action is done.  Note
 * that if a new NEW_PART callback needs to be called it is done after
 * this T2BODY callback.  */
void
mime_parser_set_t2body (mime_parser_t ctx,
                        gpg_error_t (*fnc) (void *cookie, int level))
{
  ctx->t2body = fnc;
}


/* Set the callback used to announce a new part.  It will be called
 * with the media type and media subtype of the part.  If no
 * Content-type header was given both values are the empty string.
 * The callback should return 0 on success or an error code.  The
 * error code GPG_ERR_FALSE indicates that the caller is not
 * interested in the part and data shall not be returned via a
 * registered part_data callback.  The error code GPG_ERR_TRUE
 * indicates that the parts shall be redurned in decoded format
 * (i.e. base64 or QP encoding is removed).  */
void
mime_parser_set_new_part (mime_parser_t ctx,
                          gpg_error_t (*fnc) (void *cookie,
                                              const char *mediatype,
                                              const char *mediasubtype))
{
  ctx->new_part = fnc;
}


/* Set the callback used to return the data of a part to the caller.
 * The end of the part is indicated by passing NUL for DATA.  */
void
mime_parser_set_part_data (mime_parser_t ctx,
                           gpg_error_t (*fnc) (void *cookie,
                                               const void *data,
                                               size_t datalen))
{
  ctx->part_data = fnc;
}


/* Set the callback to collect encrypted data.  A NULL passed to the
 * callback indicates the end of the encrypted data; the callback may
 * then decrypt the collected data.  */
void
mime_parser_set_collect_encrypted (mime_parser_t ctx,
                                   gpg_error_t (*fnc) (void *cookie,
                                                       const char *data))
{
  ctx->collect_encrypted = fnc;
}


/* Set the callback to collect signed data.  A NULL passed to the
 * callback indicates the end of the signed data.  */
void
mime_parser_set_collect_signeddata (mime_parser_t ctx,
                                    gpg_error_t (*fnc) (void *cookie,
                                                        const char *data))
{
  ctx->collect_signeddata = fnc;
}


/* Set the callback to collect the signature.  A NULL passed to the
 * callback indicates the end of the signature; the callback may the
 * verify the signature.  */
void
mime_parser_set_collect_signature (mime_parser_t ctx,
                                   gpg_error_t (*fnc) (void *cookie,
                                                       const char *data))
{
  ctx->collect_signature = fnc;
}


/* Return the RFC888 parser context.  This is only available inside a
 * callback.  */
rfc822parse_t
mime_parser_rfc822parser (mime_parser_t ctx)
{
  return ctx->msg;
}


/* Helper for mime_parser_parse.  */
static gpg_error_t
process_part_data (mime_parser_t ctx, char *line, size_t *length)
{
//  gpg_error_t err;
//  size_t nbytes;

  if (!ctx->want_part)
    return 0;
  if (!ctx->part_data)
    return 0;

  if (ctx->decode_part == 1)
    {
      *length = qp_decode(line, *length, NULL);
    }
  else if (ctx->decode_part == 2)
    {
      //log_assert(ctx->b64state);
      //err = b64dec_proc(ctx->b64state, line, *length, &nbytes);
      //if (err)
      //  return err;
      //*length = nbytes;
      printf("process part data of '2'\n");
    }

  return ctx->part_data (ctx->cookie, line, *length);
}


/* Read and parse a message from FP and call the appropriate
 * callbacks.  */
gpg_error_t
//mime_parser_parse (mime_parser_t ctx, estream_t fp)
mime_parser_parse(mime_parser_t ctx, FILE *fp)
{
  gpg_error_t err;
  rfc822parse_t msg = NULL;
  unsigned int lineno = 0;
  size_t length;
  char *line;

  line = ctx->line;

  msg = rfc822parse_open(parse_message_cb, ctx);
  if (!msg)
    {
      //err = gpg_error_from_syserror();
      //printf("can't open mail parser: %s", gpg_strerror (err));
      printf("can't open mail parser: %i", 65535);
      goto leave;
    }

  /* Fixme: We should not use fgets because it can't cope with
     embedded nul characters. */
  //while (es_fgets (ctx->line, sizeof (ctx->line), fp))
  while (fgets(ctx->line, sizeof(ctx->line), fp))
      {
      lineno++;
      if (lineno == 1 && !strncmp (line, "From ", 5))
        continue;  /* We better ignore a leading From line. */

      length = strlen(line);
      if (length && line[length - 1] == '\n')
	line[--length] = 0;
      else
        printf("mail parser detected too long or"
                   " non terminated last line (lnr=%u)\n", lineno);
      if (length && line[length - 1] == '\r')
	line[--length] = 0;

      ctx->err = 0;
      if (rfc822parse_insert(msg, line, length))
        {
          //err = gpg_error_from_syserror ();
          //printf("mail parser failed: %s", gpg_strerror (err));
          printf("mail parser failed: %i", 65535);
          goto leave;
        }
      if (ctx->err)
        {
          /* Error from a callback detected.  */
          err = ctx->err;
          goto leave;
        }


      /* Debug output.  Note that the boundary is shown before n_skip
       * is evaluated.  */
      if (ctx->show.boundary)
        {
          if (ctx->debug)
            printf("# Boundary: %s\n", line);
          ctx->show.boundary = 0;
        }
      if (ctx->show.n_skip)
        ctx->show.n_skip--;
      else if (ctx->show.data)
        {
          if (ctx->show.as_note)
            {
              if (ctx->verbose)
                printf("# Note: %s\n", line);
              ctx->show.as_note = 0;
            }
          else if (ctx->debug)
            printf("# Data: %s\n", line);
        }
      else if (ctx->show.header && ctx->verbose)
        printf("# Header: %s\n", line);

      if (ctx->pgpmime == PGPMIME_IN_ENCVERSION)
        {
          //trim_trailing_spaces (line);
          if (!*line)
            ;  /* Skip empty lines.  */
          else if (!strcmp (line, "Version: 1"))
            ctx->pgpmime = PGPMIME_WAIT_ENCDATA;
          else
            {
              printf("invalid PGP/MIME structure;"
                         " garbage in pgp-encrypted part ('%s')\n", line);
              ctx->pgpmime = PGPMIME_INVALID;
            }
        }
      else if (ctx->pgpmime == PGPMIME_IN_ENCDATA)
        {
          if (ctx->collect_encrypted)
            {
              err = ctx->collect_encrypted (ctx->cookie, line);
              if (!err)
                err = ctx->collect_encrypted (ctx->cookie, "\r\n");
              if (err)
                goto leave;
            }
        }
      else if (ctx->pgpmime == PGPMIME_GOT_ENCDATA)
        {
          ctx->pgpmime = PGPMIME_NONE;
          if (ctx->collect_encrypted)
            ctx->collect_encrypted (ctx->cookie, NULL);
        }
      else if (ctx->pgpmime == PGPMIME_IN_SIGNEDDATA)
        {
          /* If we are processing signed data, store the signed data.
           * We need to delay the hashing of the CR/LF because the
           * last line ending belongs to the next boundary.  This is
           * the reason why we can't use the PGPMIME state as a
           * condition.  */
          if (ctx->debug)
            printf("# hashing %s'%s'\n",
                       ctx->delay_hashing? "CR,LF+":"", line);
          if (ctx->collect_signeddata)
            {
              if (ctx->delay_hashing)
                ctx->collect_signeddata (ctx->cookie, "\r\n");
              ctx->collect_signeddata (ctx->cookie, line);
            }
          ctx->delay_hashing = 1;

          err = process_part_data(ctx, line, &length);
          if (err)
            goto leave;
        }
      else if (ctx->pgpmime == PGPMIME_IN_SIGNATURE)
        {
          if (ctx->collect_signeddata)
            {
              ctx->collect_signature (ctx->cookie, line);
              ctx->collect_signature (ctx->cookie, "\r\n");
            }
        }
      else if (ctx->pgpmime == PGPMIME_GOT_SIGNATURE)
        {
          ctx->pgpmime = PGPMIME_NONE;
          if (ctx->collect_signeddata)
            ctx->collect_signature (ctx->cookie, NULL);
        }
      else
        {
          err = process_part_data (ctx, line, &length);
          if (err)
            goto leave;
        }
    }

  rfc822parse_close (msg);
  msg = NULL;
  err = 0;

 leave:
  rfc822parse_cancel (msg);
  return err;
}


/* This function is called by the parser to communicate events.  This
   callback communicates with the main program using a structure
   passed in OPAQUE. Should return 0 or set errno and return -1. */
int
message_cb(void* opaque, rfc822parse_event_t event, rfc822parse_t msg)
{
    struct parse_info_s* info = opaque;

    if (event == RFC822PARSE_BEGIN_HEADER || event == RFC822PARSE_T2BODY)
    {
        /* We need to check here whether to start collecting signed data
           because attachments might come without header lines and thus
           we won't see the BEGIN_HEADER event.  */
        if (info->smfm_state == 1)
        {
            //printf("c begin_hash\n");
            info->hashing = 1;
            info->hashing_level = info->nesting_level;
            info->smfm_state++;

            if (opt_crypto)
            {
                //assert(!info->hash_file);
                info->hash_file = tmpfile();
                if (!info->hash_file)
                    die("failed to create temporary file: %s", strerror(errno));
            }
        }
    }


    if (event == RFC822PARSE_OPEN)
    {
        /* Initialize for a new message. */
        info->show_header = 1;
        info->file = NULL;
    }
    else if (event == RFC822PARSE_T2BODY)
    {
        rfc822parse_field_t ctx, clx;
        info->file = NULL;
        const char* sname = NULL;

        ctx = rfc822parse_parse_field(msg, "Content-Type", -1);
        clx = rfc822parse_parse_field(msg, "Content-Location", -1);
        if (ctx)
        {
            const char* s1, * s2;

            s1 = rfc822parse_query_media_type(ctx, &s2);

            if (s1)
            {
                //printf("h media: %*s%s %s\n", info->nesting_level * 2, "", s1, s2);
                if (info->smfm_state == 3)
                {
                    char* buf = xmalloc(strlen(s1) + strlen(s2) + 2);
                    strcpy(stpcpy(stpcpy(buf, s1), "/"), s2);
                    //assert(info->signing_protocol);
                    if (strcmp(buf, info->signing_protocol) &&
                        (!info->signing_protocol_2
                            || strcmp(buf, info->signing_protocol_2)))
                        err("invalid %s structure; expected %s%s%s, found '%s'",
                            info->is_smime ? "S/MIME" : "PGP/MIME",
                            info->signing_protocol,
                            info->signing_protocol_2 ? " or " : "",
                            info->signing_protocol_2 ? info->signing_protocol_2 : "",
                            buf);
                    else
                    {
                        //printf("c begin_signature\n");
                        info->smfm_state++;
                        if (opt_crypto)
                        {
                            //assert(!info->sig_file);
                            info->sig_file = tmpfile();
                            if (!info->sig_file)
                                die("error creating temp file: %s",
                                    strerror(errno));
                        }
                    }
                    free(buf);
                }
                else if (!strcmp(s1, "multipart"))
                {
                    if (!strcmp(s2, "signed")) {
                        printf("Start Mime Unsigning\n");
                        //mime_signed_begin(info, msg, ctx);
                    }
                    else if (!strcmp(s2, "encrypted")) {
                        printf("Start Mime Decrypting\n");
                        //mime_encrypted_begin(info, msg, ctx);
                    }
                }
                else if (!strcmp(s1, "application") && (!strcmp(s2, "pkcs7-mime") || !strcmp(s2, "x-pkcs7-mime"))) {
                    printf("PKCS7-MIME\n");
                    //pkcs7_begin(info, msg, ctx);
                }

                sname = rfc822parse_query_parameter(ctx, "name", 0);
                if (sname) {
                    //printf("Signature name: %s\n", sname);
                    info->file = sname;
                }
                else {
                    info->file = "sls.p7s";
                }

            }
            else {
                //printf("h media: %*s none\n", info->nesting_level * 2, "");
            }

            //rfc822parse_release_field(ctx);
        }
        else {
            rfc822parse_release_field(ctx);
        }
        
        if (clx) {
            const char* l1;

            l1 = rfc822parse_query_media_location(clx);
            if (l1) {
                //printf("h location: %*s%s\n", info->nesting_level * 2, "", l1);
                info->file = l1;
            }
            else {
                info->file = "clx.l1";
            }

            //rfc822parse_release_field(clx);
        }
        else {
            rfc822parse_release_field(clx);
        }

        info->show_header = 0;
        info->show_data = 1;
        info->skip_show = 1;
    }
    else if (event == RFC822PARSE_PREAMBLE)
        info->show_data_as_note = 1;
    else if (event == RFC822PARSE_LEVEL_DOWN)
    {
        //printf("b down\n");
        info->nesting_level++;
    }
    else if (event == RFC822PARSE_LEVEL_UP)
    {
        //printf("b up\n");
        if (info->nesting_level)
            info->nesting_level--;
        else
            err("invalid structure (bad nesting level)");
    }
    else if (event == RFC822PARSE_BOUNDARY || event == RFC822PARSE_LAST_BOUNDARY)
    {
        info->show_data = 0;
        info->show_boundary = 1;
        if (event == RFC822PARSE_BOUNDARY)
        {
            info->show_header = 1;
            info->skip_show = 1;
            //printf("b part\n");
        }
        else {
            info->file = NULL;
            //printf("b last\n");
        }


        if (info->smfm_state == 2 && info->nesting_level == info->hashing_level)
        {
            //printf("c end_hash\n");
            info->smfm_state++;
            info->hashing = 0;
        }
        else if (info->smfm_state == 4)
        {
            //printf("c end_signature\n");
            info->verify_now = 1;
        }
    }

    return 0;
}

/* Read a message from FP and process it according to the global
   options. */
void
parse_message(FILE* fp, char* session_basedir)
{
    char line[5000];
    size_t length;
    rfc822parse_t msg;
    unsigned int lineno = 0;
    int no_cr_reported = 0;
    struct parse_info_s info;
    char* sls_file = session_basedir;
    size_t URLlength = strlen(session_basedir);
    int tmp = 0;

    memset(&info, 0, sizeof info);

    msg = rfc822parse_open(message_cb, &info);
    if (!msg)
        die("can't open parser: %s", strerror(errno));

    /* Fixme: We should not use fgets because it can't cope with
       embedded nul characters. */
    while (fgets(line, sizeof(line), fp))
    {
        // Check what is read
        //printf("%s", line);
        //fflush(stdout);

        //if (line[0] == '-' && line[1] == '-') {
        //    printf("\n FOUND BOUNDARY\n");
        //    fflush(stdout);
        //}

        lineno++;
        if (lineno == 1 && !strncmp(line, "From ", 5))
            continue;  /* We better ignore a leading From line. */

        length = strlen(line);
        if (length && line[length - 1] == '\n')
            line[--length] = 0;
        else
            err("line number %u too long or last line not terminated", lineno);
        if (length && line[length - 1] == '\r')
            line[--length] = 0;
        else if (verbose && !no_cr_reported)
        {
            err("non canonical ended line detected (line %u)", lineno);
            no_cr_reported = 1;
        }


        if (rfc822parse_insert(msg, line, length))
            die("parser failed: %s", strerror(errno));

        if (info.hashing)
        {
            /* Delay hashing of the CR/LF because the last line ending
               belongs to the next boundary. */
            if (debug)
                printf("# hashing %s'%s'\n", info.hashing == 2 ? "CR,LF+" : "", line);
            if (opt_crypto)
            {
                if (info.hashing == 2)
                    fputs("\r\n", info.hash_file);
                fputs(line, info.hash_file);
                if (ferror(info.hash_file))
                    die("error writing to temporary file: %s", strerror(errno));
            }

            info.hashing = 2;
        }

        if (info.sig_file && opt_crypto)
        {
            if (info.verify_now)
            {
                //verify_signature(&info);
                if (info.hash_file)
                    fclose(info.hash_file);
                info.hash_file = NULL;
                fclose(info.sig_file);
                info.sig_file = NULL;
                info.smfm_state = 0;
                info.is_smime = 0;
                info.is_pkcs7 = 0;
            }
            else
            {
                fputs(line, info.sig_file);
                fputs("\r\n", info.sig_file);
                if (ferror(info.sig_file))
                    die("error writing to temporary file: %s", strerror(errno));
            }
        }

        if (info.show_boundary)
        {
            if (!opt_no_header)
                //printf(":%s\n", line);
            info.show_boundary = 0;
        }

        if (info.skip_show)
            info.skip_show--;
        else if (info.show_data)
        {
            //printf("\n SHOW DATA");
            //fflush(stdout);

            if (info.show_data_as_note)
            {
                if (verbose) {
                    printf("# DATA: %s\n", line);
                    fflush(stdout);
                }
                info.show_data_as_note = 0;
            }
            else if (info.file) { // Write the file
                memcpy((sls_file + URLlength), "/", 1);
                memcpy((sls_file + URLlength+1), info.file, (strlen(info.file) + (URLlength+1)));
                if (verbose) printf(" File: %s\n", sls_file);

                FILE* fq = fopen(sls_file, "wb");
                fputs(line, fq);
                //if (line[0] != '\n') fputs("\n", fq);
                if (verbose) printf(" Data: %s\n", line);
                fclose(fq);

                // Put parameters back where we found them.
                //memcpy((sls_file + URLlength), "", strlen(info.file)+1);
                tmp = strlen(info.file);
                info.file = NULL;
            }
            else {
                if(verbose) printf(" Data: %s\n", line);

                FILE* fq = fopen(sls_file, "a");
                fputs(line, fq);
                //if (line[0] != '\n') fputs("\n", fq);
                if (verbose) printf(" Data: %s\n", line);
                fclose(fq);

            }
            fflush(stdout);
        }
        else if (info.show_header && !opt_no_header)
            if (verbose) printf(".%s\n", line);
        fflush(stdout);
    }

    if (info.sig_file && opt_crypto && info.is_pkcs7)
    {
        //verify_signature(&info);
        fclose(info.sig_file);
        info.sig_file = NULL;
        info.is_pkcs7 = 0;

    }
    // Put parameters back where we found them.
    memcpy((sls_file + URLlength), "", tmp+1);

    rfc822parse_close(msg);
}
