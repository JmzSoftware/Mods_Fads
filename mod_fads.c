/* ******************************************************************
 *
 * mod_fads v0.3
 * 
 * force ads into HTML pages
 *
 * by Andrew Fabbro
 * andrew@modfads.org
 *
 * See modfads.org for docs: http://www.modfads.org
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 * ******************************************************************/

/* ******************************************************************
 * DEFINES
 * ******************************************************************/

#define MOD_FADS_MAX_AD_FILE_SIZE 8192
#define MOD_FADS_VERSION "0.3"

/* ******************************************************************
 * INCLUDES
 * ******************************************************************/

#include "httpd.h"
#include "http_config.h"
#include "http_log.h"
#include "apr_buckets.h"
#include "apr_general.h"
#include "apr_lib.h"
#include "util_filter.h"
#include "http_request.h"
#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

/* ******************************************************************
 * CONSTANTS, TYPEDEFS, ETC.
 * ******************************************************************/

static const char ModFadsName[]="mod_fads"; 
module AP_MODULE_DECLARE_DATA fads_module; 

typedef struct {
	int bEnabled;
	char header_ad_file[1025];
	char footer_ad_file[1025];
} ModFadsConfig;

/* ******************************************************************
 * write_error_log()
 * 
 * put a message in the apache error log
 *
 * ******************************************************************/

#ifdef DEBUG
static void write_error_log ( server_rec * s, const char * message, ... ) {
	va_list argp;
	char error_message[10241];

	va_start(argp,message);
	vsnprintf(error_message, 10240, message, argp);
	va_end(argp);

	 ap_log_error(APLOG_MARK, APLOG_NOERRNO|APLOG_NOTICE, 0, s, "mod_fads: %s", error_message); 
}
#endif

/* ******************************************************************
 *
 * mod_fads_create_server_config
 * 
 * ******************************************************************/

static void * mod_fads_create_server_config(apr_pool_t *p,server_rec *s) {
	ModFadsConfig *pConfig=apr_pcalloc(p,sizeof *pConfig);
	pConfig->bEnabled=0;
	pConfig->header_ad_file[0] = '\0';
	pConfig->footer_ad_file[0] = '\0';
	return pConfig;
}

/* ******************************************************************
 *
 * mod_fads_insert_filter
 * 
 * ******************************************************************/

static void mod_fads_insert_filter(request_rec *r) {
	ModFadsConfig *pConfig=ap_get_module_config(r->server->module_config, &fads_module);
	if(!pConfig->bEnabled)
		return;
	ap_add_output_filter(ModFadsName,NULL,r,r->connection);
}

/* ******************************************************************
 * find_body_start()
 * 
 * args:
 *  string (presumably the html)
 *  length of the string
 *
 * returns:
 *   if <body> tag is found, returns position of closing >
 *   if <body> tag is not found, returns -1
 *
 * ******************************************************************/

static size_t find_body_start ( const char * string, int len ) {
	int found_body = 0;
	int n;

	for (n=0; n<len-5; n++) {
		if ( string[n] == '<' &&
			toupper(string[n+1]) == 'B' &&
			toupper(string[n+2]) == 'O' &&
			toupper(string[n+3]) == 'D' &&
			toupper(string[n+4]) == 'Y' ) {
			found_body = 1;
		} else if ( found_body == 1 && string[n] == '>' ) {
			return (size_t)(n+1);
		}
	}
	return (size_t) -1;
}

/* ******************************************************************
 * find_body_end()
 * 
 * args:
 *  string (presumably the html)
 *  length of the string
 *
 * returns:
 *   if </body> tag is found, returns position of starting <
 *   if </body> tag is not found, returns -1
 *
 * ******************************************************************/

static size_t find_body_end ( const char * string, int len ) {
	int n;

	for (n=0; n<len-7; n++) {
		if ( string[n] == '<' &&
			toupper(string[n+1]) == '/' &&
			toupper(string[n+2]) == 'B' &&
			toupper(string[n+3]) == 'O' &&
			toupper(string[n+4]) == 'D' &&
			toupper(string[n+5]) == 'Y' &&
			toupper(string[n+6]) == '>' ) {
			return (size_t)(n-1);
		}
	}
	return (size_t) -1;
}

/* ******************************************************************
 * ModFadsOutFilter()
 * 
 * this is where the magic happens
 *
 * ******************************************************************/

static apr_status_t mod_fads_output_filter (ap_filter_t * f, apr_bucket_brigade * bb) {
	apr_bucket * b;

	FILE *fp;
	char buf[100];
	struct stat ad_file;

	char header_ad_html[MOD_FADS_MAX_AD_FILE_SIZE+1];
	apr_bucket * header_ad_bucket;
	int already_inserted_header = 0;

	char footer_ad_html[MOD_FADS_MAX_AD_FILE_SIZE+1];
	apr_bucket * footer_ad_bucket;
	int already_inserted_footer = 0;

	ModFadsConfig * pConfig=ap_get_module_config(f->r->server->module_config, &fads_module);

	/*
	 * announce our invocation
	 */

	#ifdef DEBUG
	write_error_log(f->r->server,"invoked");
	#endif

	/*
	 * put our nose in the air like a haughty Bourbon princess and refuse to
	 * sully our hands with anything except the rarified glories of text/html
	 */

	if ( strstr(f->r->content_type,"text/html") == NULL ) {
		return ap_pass_brigade(f->next,bb);
	}

	/*
	 * note the doc type
	 */

	#ifdef DEBUG
	write_error_log(f->r->server,"Content Type: %s",(const char*)f->r->content_type);
	#endif

	/*
	 * read in the header ad html
	 */

	header_ad_html[0] = '\0';
	if ( pConfig->header_ad_file[0] == '\0' ) {
		#ifdef DEBUG
		write_error_log(f->r->server,"no header ad file specified in configuration");
		#endif
	} else {
		if ( stat(pConfig->header_ad_file,&ad_file) != 0 ) {
			#ifdef DEBUG
			write_error_log(f->r->server,"ERROR: could not stat header forced ads file");
			#endif
		} else if ( ad_file.st_size > MOD_FADS_MAX_AD_FILE_SIZE) {	
			#ifdef DEBUG
			write_error_log(f->r->server,"ERROR: header forced ads file is too big");
			#endif
		} else {
			fp = fopen(pConfig->header_ad_file,"r");
			if ( fp == NULL ) {
				#ifdef DEBUG
				write_error_log(f->r->server,"ERROR: could not fopen header forced ads file");
				#endif
			} else {
				while  ( fgets(buf,100,fp) ) {
					strncat(header_ad_html,buf,100);
				}
				fclose(fp);
			}
		}

		#ifdef DEBUG
		if ( header_ad_html[0] == '\0' ) {
			write_error_log(f->r->server,"no header ad");
		} else {
			write_error_log(f->r->server,"header_ad_html: %s",header_ad_html);
		}
		#endif
	}

	/*
	 * read in the footer ad html
	 */

	footer_ad_html[0] = '\0';
	if ( pConfig->footer_ad_file[0] == '\0' ) {
		#ifdef DEBUG
		write_error_log(f->r->server,"no footer ad file specified in configuration");
		#endif
	} else {
		if ( stat(pConfig->footer_ad_file,&ad_file) != 0 ) {
			#ifdef DEBUG
			write_error_log(f->r->server,"ERROR: could not stat footer forced ads file");
			#endif
		} else if ( ad_file.st_size > MOD_FADS_MAX_AD_FILE_SIZE ) {	
			#ifdef DEBUG
			write_error_log(f->r->server,"ERROR: forced ads footer file is too big");
			#endif
		} else {
			fp = fopen(pConfig->footer_ad_file,"r");
			if ( fp == NULL ) {
				#ifdef DEBUG
				write_error_log(f->r->server,"ERROR: could not fopen footer forced ads file");
				#endif
			} else {
				while  ( fgets(buf,100,fp) ) {
					strncat(footer_ad_html,buf,100);
				}
				fclose(fp);
			}
		}
	
		#ifdef DEBUG
		if ( footer_ad_html[0] == '\0' ) {
			write_error_log(f->r->server,"no footer ad");
		} else {
			write_error_log(f->r->server,"footer_ad_html: %s",footer_ad_html);
		}
		#endif
	} 

	/*
	 * quit if we don't have any ads else create the ad buckets
	 */

	if ( header_ad_html[0] == '\0' && footer_ad_html[0] == '\0' ) {
		/* no point in going on if we don't have any ads */
		#ifdef DEBUG
		write_error_log(f->r->server,"neither header nor footer ad, so nothing for me to do");
		#endif
	  	return ap_pass_brigade(f->next,bb);
	}
	if ( header_ad_html[0] != '\0' ) {
		header_ad_bucket = apr_bucket_immortal_create(header_ad_html, strlen(header_ad_html), f->r->connection->bucket_alloc); 
	} 
	if ( footer_ad_html[0] != '\0' ) {
		footer_ad_bucket = apr_bucket_immortal_create(footer_ad_html, strlen(footer_ad_html), f->r->connection->bucket_alloc); 
	} 

	/*
	 * process the buckets
	 */

	for ( b = APR_BRIGADE_FIRST(bb); b != APR_BRIGADE_SENTINEL(bb); b = APR_BUCKET_NEXT(b) ) {
		const char * buf;
		size_t bytes;

		if ( ! APR_BUCKET_IS_EOS(b) ) {
			#ifdef DEBUG
			write_error_log(f->r->server,"starting a non-EOS bucket");
			#endif
			if ( apr_bucket_read(b, &buf, &bytes, APR_BLOCK_READ) == APR_SUCCESS ) {

				#ifdef DEBUG
				write_error_log(f->r->server,"read bucket - at this point, already_inserted_header is %i, already_inserted_footer is %i",already_inserted_header,already_inserted_footer);
				#endif

				/*
				 * splice in header ad
				 */

				if ( header_ad_html[0] != '\0' && already_inserted_header != 1 ) {
					size_t header_splice_point = -1;
					header_splice_point = find_body_start(buf,bytes);
					if ( header_splice_point == -1 ) {
						#ifdef DEBUG
						write_error_log(f->r->server,"didn't find body start tag");
						#endif
					} else {
						#ifdef DEBUG
						write_error_log(f->r->server,"splicing in header ad at %i",header_splice_point);
						#endif
				
						/*
						 * splice in ad
						 */
		
						/*
						 * we split the bucket into two
						
							-----------------------------
						b->	|...blah</head><body><h1>this
	
							---------------------    ---------
						b->	|...blah</head><body>    |<h1>this
	
	
						*/
			
						apr_bucket_split(b,header_splice_point);
	
						/*
						 * move b to the next bucket
	
							---------------------        ---------
							|...blah</head><body>    b-> |<h1>this
	
						 */
	
						b = APR_BUCKET_NEXT(b);	
	
						/*
						 * insert before
	
							---------------------    --------         ---------
							|...blah</head><body>    |ad text     b-> |<h1>this
						 */
	
						APR_BUCKET_INSERT_BEFORE(b,header_ad_bucket);
	
						/* 
						 * note that the for loop will advance us to the next bucket
						 * so we don't need to do it explicitly.  We also don't WANT
						 * to do it at this point, because the footer ad may need to
						 * be inserted and we want b to point at the remaining text.
						 */
	
						/* 
						 * set already_inserted 
						 */
	
						already_inserted_header = 1;
					}
				}

				/*
				 * splice in footer ad
				 */

				if ( footer_ad_html[0] != '\0' && already_inserted_footer != 1 ) { 
					size_t footer_splice_point = -1;
					footer_splice_point = find_body_end(buf,bytes);
					if ( footer_splice_point == -1 ) {
						#ifdef DEBUG
						write_error_log(f->r->server,"didn't find body end tag");
						#endif
					} else {
						#ifdef DEBUG
						write_error_log(f->r->server,"splicing in footer ad at %i",footer_splice_point);
						#endif

						/*
						 * splice in ad
						 * same procedure as in header so look at the pretty pics there
						 */
				
						apr_bucket_split(b,footer_splice_point);
						b = APR_BUCKET_NEXT(b);	
						APR_BUCKET_INSERT_BEFORE(b,footer_ad_bucket);
						/* b = APR_BUCKET_NEXT(b); */
	
						/* 
						 * set already_inserted 
						 */

						already_inserted_footer = 1;
					}
				}
			}
		}

	}
	return ap_pass_brigade(f->next,bb);
}

/* ******************************************************************
 *
 * mod_fads_enable 
 * 
 * ******************************************************************/

static const char * mod_fads_enable (cmd_parms *cmd, void *dummy, int arg) {
	ModFadsConfig *pConfig=ap_get_module_config(cmd->server->module_config, &fads_module);
	pConfig->bEnabled=arg;
	return NULL;
}

/* ******************************************************************
 *
 * mod_fads_header_ad_file
 * 
 * ******************************************************************/

static const char * mod_fads_header_ad_file (cmd_parms * cmd, void * dummy, const char * arg) {
	ModFadsConfig *pConfig=ap_get_module_config(cmd->server->module_config, &fads_module);
	strncpy(pConfig->header_ad_file,arg,1024);
	return NULL;
}

/* ******************************************************************
 *
 * mod_fads_footer_ad_file
 * 
 * ******************************************************************/

static const char * mod_fads_footer_ad_file (cmd_parms * cmd, void * dummy, const char * arg) {
	ModFadsConfig *pConfig=ap_get_module_config(cmd->server->module_config, &fads_module);
	strncpy(pConfig->footer_ad_file,arg,1024);
	return NULL;
}

/* ******************************************************************
 *
 * ModFadsCmds
 * 
 * ******************************************************************/

static const command_rec ModFadsCmds[] = {
	AP_INIT_FLAG("ModFads", mod_fads_enable, NULL, RSRC_CONF, "Insert forced ads"),
	AP_INIT_TAKE1("ModFadsHeaderAdFile",mod_fads_header_ad_file,NULL,RSRC_CONF, "Location of Header Ad HTML"),
	AP_INIT_TAKE1("ModFadsFooterAdFile",mod_fads_footer_ad_file,NULL,RSRC_CONF, "Location of Footer Ad HTML"),
    { NULL }
};

/* ******************************************************************
 *
 * mod_fads_register_hooks
 * 
 * ******************************************************************/

static void mod_fads_register_hooks(apr_pool_t *p) {
	ap_hook_insert_filter(mod_fads_insert_filter,NULL,NULL,APR_HOOK_MIDDLE);
	ap_register_output_filter(ModFadsName,mod_fads_output_filter,NULL,AP_FTYPE_RESOURCE);
}

/* ******************************************************************
 *
 * module info
 * 
 * ******************************************************************/

module AP_MODULE_DECLARE_DATA fads_module = {
    STANDARD20_MODULE_STUFF,
    NULL,
    NULL,
    mod_fads_create_server_config,
    NULL,
    ModFadsCmds,
    mod_fads_register_hooks
};
