/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-1998 University of Maryland at College Park
 * All Rights Reserved.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of U.M. not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  U.M. makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * U.M. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL U.M.
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author: James da Silva, Systems Design and Analysis Group
 *			   Computer Science Department
 *			   University of Maryland at College Park
 */
/*
 * $Id: find.c,v 1.33 2006/07/06 13:13:15 martinea Exp $
 *
 * controlling process for the Amanda backup system
 */
#include "amanda.h"
#include "conffile.h"
#include "tapefile.h"
#include "logfile.h"
#include "holding.h"
#include "find.h"

int find_match(char *host, char *disk);
int search_logfile(find_result_t **output_find, char *label, char *datestamp, char *logfile);
void search_holding_disk(find_result_t **output_find);
char *find_nicedate(char *datestamp);
static int find_compare(const void *, const void *);
static int parse_taper_datestamp_log(char *logline, char **datestamp, char **level);

static char *find_sort_order = NULL;
int dynamic_disklist = 0;
disklist_t* find_diskqp = NULL;

find_result_t *
find_dump(
    int dyna_disklist,
    disklist_t* diskqp)
{
    char *conf_logdir, *logfile = NULL;
    int tape, maxtape, logs;
    unsigned seq;
    tape_t *tp;
    find_result_t *output_find = NULL;

    dynamic_disklist = dyna_disklist;
    find_diskqp = diskqp;
    conf_logdir = getconf_str(CNF_LOGDIR);
    if (*conf_logdir == '/') {
	conf_logdir = stralloc(conf_logdir);
    } else {
	conf_logdir = stralloc2(config_dir, conf_logdir);
    }
    maxtape = lookup_nb_tape();

    for(tape = 1; tape <= maxtape; tape++) {

	tp = lookup_tapepos(tape);
	if(tp == NULL) continue;

	/* search log files */

	logs = 0;

	/* new-style log.<date>.<seq> */

	for(seq = 0; 1; seq++) {
	    char seq_str[NUM_STR_SIZE];

	    g_snprintf(seq_str, SIZEOF(seq_str), "%u", seq);
	    logfile = newvstralloc(logfile,
			conf_logdir, "/log.", tp->datestamp, ".", seq_str, NULL);
	    if(access(logfile, R_OK) != 0) break;
	    logs += search_logfile(&output_find, tp->label, tp->datestamp, logfile);
	}

	/* search old-style amflush log, if any */

	logfile = newvstralloc(logfile,
			       conf_logdir, "/log.", tp->datestamp, ".amflush", NULL);
	if(access(logfile,R_OK) == 0) {
	    logs += search_logfile(&output_find, tp->label, tp->datestamp, logfile);
	}

	/* search old-style main log, if any */

	logfile = newvstralloc(logfile, conf_logdir, "/log.", tp->datestamp, NULL);
	if(access(logfile,R_OK) == 0) {
	    logs += search_logfile(&output_find, tp->label, tp->datestamp, logfile);
	}
	if(logs == 0 && strcmp(tp->datestamp,"0") != 0)
	    g_fprintf(stderr, _("Warning: no log files found for tape %s written %s\n"),
		   tp->label, find_nicedate(tp->datestamp));
    }
    amfree(logfile);
    amfree(conf_logdir);

    search_holding_disk(&output_find);

    return(output_find);
}

char **
find_log(void)
{
    char *conf_logdir, *logfile = NULL;
    int tape, maxtape, logs;
    unsigned seq;
    tape_t *tp;
    char **output_find_log = NULL;
    char **current_log;

    conf_logdir = getconf_str(CNF_LOGDIR);
    if (*conf_logdir == '/') {
	conf_logdir = stralloc(conf_logdir);
    } else {
	conf_logdir = stralloc2(config_dir, conf_logdir);
    }
    maxtape = lookup_nb_tape();

    output_find_log = alloc((maxtape*5+10) * SIZEOF(char *));
    current_log = output_find_log;

    for(tape = 1; tape <= maxtape; tape++) {

	tp = lookup_tapepos(tape);
	if(tp == NULL) continue;

	/* search log files */

	logs = 0;

	/* new-style log.<date>.<seq> */

	for(seq = 0; 1; seq++) {
	    char seq_str[NUM_STR_SIZE];

	    g_snprintf(seq_str, SIZEOF(seq_str), "%u", seq);
	    logfile = newvstralloc(logfile,
			conf_logdir, "/log.", tp->datestamp, ".", seq_str, NULL);
	    if(access(logfile, R_OK) != 0) break;
	    if( search_logfile(NULL, tp->label, tp->datestamp, logfile)) {
		*current_log = vstralloc("log.", tp->datestamp, ".", seq_str, NULL);
		current_log++;
		logs++;
		break;
	    }
	}

	/* search old-style amflush log, if any */

	logfile = newvstralloc(logfile,
			       conf_logdir, "/log.", tp->datestamp, ".amflush", NULL);
	if(access(logfile,R_OK) == 0) {
	    if( search_logfile(NULL, tp->label, tp->datestamp, logfile)) {
		*current_log = vstralloc("log.", tp->datestamp, ".amflush", NULL);
		current_log++;
		logs++;
	    }
	}

	/* search old-style main log, if any */

	logfile = newvstralloc(logfile, conf_logdir, "/log.", tp->datestamp, NULL);
	if(access(logfile,R_OK) == 0) {
	    if(search_logfile(NULL, tp->label, tp->datestamp, logfile)) {
		*current_log = vstralloc("log.", tp->datestamp, NULL);
		current_log++;
		logs++;
	    }
	}
	if(logs == 0 && strcmp(tp->datestamp,"0") != 0)
	    g_fprintf(stderr, _("Warning: no log files found for tape %s written %s\n"),
		   tp->label, find_nicedate(tp->datestamp));
    }
    amfree(logfile);
    amfree(conf_logdir);
    *current_log = NULL;
    return(output_find_log);
}

void
search_holding_disk(
    find_result_t **output_find)
{
    GSList *holding_file_list;
    GSList *e;
    char *holding_file;
    disk_t *dp;
    dumpfile_t file;

    holding_file_list = holding_get_files(NULL, 1);

    for(e = holding_file_list; e != NULL; e = e->next) {
	holding_file = (char *)e->data;

	if (!holding_file_get_dumpfile(holding_file, &file))
	    continue;

	if (file.dumplevel < 0 || file.dumplevel > 9)
	    continue;

	dp = NULL;
	for(;;) {
	    char *s;
	    if((dp = lookup_disk(file.name, file.disk)))
		break;
	    if((s = strrchr(file.name,'.')) == NULL)
		break;
	    *s = '\0';
	}
	if ( dp == NULL ) {
	    continue;
	}

	if(find_match(file.name,file.disk)) {
	    find_result_t *new_output_find =
		alloc(SIZEOF(find_result_t));
	    new_output_find->next=*output_find;
	    new_output_find->timestamp = stralloc(file.datestamp);
	    new_output_find->hostname = stralloc(file.name);
	    new_output_find->diskname = stralloc(file.disk);
	    new_output_find->level=file.dumplevel;
	    new_output_find->label=stralloc(holding_file);
	    new_output_find->partnum=stralloc("--");
	    new_output_find->filenum=0;
	    new_output_find->status=stralloc("OK");
	    *output_find=new_output_find;
	}
    }

    g_slist_free_full(holding_file_list);
}

static int
find_compare(
    const void *i1,
    const void *j1)
{
    int compare=0;
    find_result_t **i = (find_result_t **)i1;
    find_result_t **j = (find_result_t **)j1;

    size_t nb_compare=strlen(find_sort_order);
    size_t k;

    for(k=0;k<nb_compare;k++) {
	switch (find_sort_order[k]) {
	case 'h' : compare=strcmp((*i)->hostname,(*j)->hostname);
		   break;
	case 'H' : compare=strcmp((*j)->hostname,(*i)->hostname);
		   break;
	case 'k' : compare=strcmp((*i)->diskname,(*j)->diskname);
		   break;
	case 'K' : compare=strcmp((*j)->diskname,(*i)->diskname);
		   break;
	case 'd' : compare=strcmp((*i)->timestamp,(*j)->timestamp);
		   break;
	case 'D' : compare=strcmp((*j)->timestamp,(*i)->timestamp);
		   break;
	case 'l' : compare=(*j)->level - (*i)->level;
		   break;
	case 'f' : compare=((*i)->filenum == (*j)->filenum) ? 0 :
		           (((*i)->filenum < (*j)->filenum) ? -1 : 1);
		   break;
	case 'F' : compare=((*j)->filenum == (*i)->filenum) ? 0 :
		           (((*j)->filenum < (*i)->filenum) ? -1 : 1);
		   break;
	case 'L' : compare=(*i)->level - (*j)->level;
		   break;
	case 'b' : compare=strcmp((*i)->label,(*j)->label);
		   break;
	case 'B' : compare=strcmp((*j)->label,(*i)->label);
		   break;
	case 'p' :
		   if(strcmp((*i)->partnum, "--") != 0 &&
		      strcmp((*j)->partnum, "--") != 0){
		      compare = atoi((*i)->partnum) - atoi((*j)->partnum);
		   }
	           else compare=strcmp((*i)->partnum,(*j)->partnum);
		   break;
	case 'P' :
		   if(strcmp((*i)->partnum, "--") != 0 &&
		      strcmp((*j)->partnum, "--") != 0){
		      compare = atoi((*j)->partnum) - atoi((*i)->partnum);
		   }
	           else compare=strcmp((*j)->partnum,(*i)->partnum);
		   break;
	}
	if(compare != 0)
	    return compare;
    }
    return 0;
}

void
sort_find_result(
    char *sort_order,
    find_result_t **output_find)
{
    find_result_t *output_find_result;
    find_result_t **array_find_result = NULL;
    size_t nb_result=0;
    size_t no_result;

    find_sort_order = sort_order;
    /* qsort core dump if nothing to sort */
    if(*output_find==NULL)
	return;

    /* How many result */
    for(output_find_result=*output_find;
	output_find_result;
	output_find_result=output_find_result->next) {
	nb_result++;
    }

    /* put the list in an array */
    array_find_result=alloc(nb_result * SIZEOF(find_result_t *));
    for(output_find_result=*output_find,no_result=0;
	output_find_result;
	output_find_result=output_find_result->next,no_result++) {
	array_find_result[no_result]=output_find_result;
    }

    /* sort the array */
    qsort(array_find_result,nb_result,SIZEOF(find_result_t *),
	  find_compare);

    /* put the sorted result in the list */
    for(no_result=0;
	no_result<nb_result-1; no_result++) {
	array_find_result[no_result]->next = array_find_result[no_result+1];
    }
    array_find_result[nb_result-1]->next=NULL;
    *output_find=array_find_result[0];
    amfree(array_find_result);
}

void
print_find_result(
    find_result_t *output_find)
{
    find_result_t *output_find_result;
    int max_len_datestamp = 4;
    int max_len_hostname  = 4;
    int max_len_diskname  = 4;
    int max_len_level     = 2;
    int max_len_label     =12;
    int max_len_filenum   = 4;
    int max_len_part      = 4;
    int max_len_status    = 6;
    size_t len;

    for(output_find_result=output_find;
	output_find_result;
	output_find_result=output_find_result->next) {

	len=strlen(find_nicedate(output_find_result->timestamp));
	if((int)len > max_len_datestamp)
	    max_len_datestamp=(int)len;

	len=strlen(output_find_result->hostname);
	if((int)len > max_len_hostname)
	    max_len_hostname = (int)len;

	len=strlen(output_find_result->diskname);
	if((int)len > max_len_diskname)
	    max_len_diskname = (int)len;

	len=strlen(output_find_result->label);
	if((int)len > max_len_label)
	    max_len_label = (int)len;

	len=strlen(output_find_result->status);
	if((int)len > max_len_status)
	    max_len_status = (int)len;

	len=strlen(output_find_result->partnum);
	if((int)len > max_len_part)
	    max_len_part = (int)len;
    }

    /*
     * Since status is the rightmost field, we zap the maximum length
     * because it is not needed.  The code is left in place in case
     * another column is added later.
     */
    max_len_status = 1;

    if(output_find==NULL) {
	g_printf(_("\nNo dump to list\n"));
    }
    else {
	g_printf(_("\ndate%*s host%*s disk%*s lv%*s tape or file%*s file%*s part%*s status\n"),
	       max_len_datestamp-4,"",
	       max_len_hostname-4 ,"",
	       max_len_diskname-4 ,"",
	       max_len_level-2    ,"",
	       max_len_label-12   ,"",
	       max_len_filenum-4  ,"",
	       max_len_part-4  ,"");
        for(output_find_result=output_find;
	        output_find_result;
	        output_find_result=output_find_result->next) {
	    char *qdiskname;

	    qdiskname = quote_string(output_find_result->diskname);
	    /*@ignore@*/
	    g_printf("%-*s %-*s %-*s %*d %-*s %*lld %*s %-*s\n",
		    max_len_datestamp, 
			find_nicedate(output_find_result->timestamp),
		    max_len_hostname,  output_find_result->hostname,
		    max_len_diskname,  qdiskname,
		    max_len_level,     output_find_result->level,
		    max_len_label,     output_find_result->label,
		    max_len_filenum,   (long long)output_find_result->filenum,
		    max_len_part,      output_find_result->partnum,
		    max_len_status,    output_find_result->status
		    );
	    /*@end@*/
	    amfree(qdiskname);
	}
    }
}

void
free_find_result(
    find_result_t **output_find)
{
    find_result_t *output_find_result, *prev;

    prev=NULL;
    for(output_find_result=*output_find;
	    output_find_result;
	    output_find_result=output_find_result->next) {
	amfree(prev);
	amfree(output_find_result->timestamp);
	amfree(output_find_result->hostname);
	amfree(output_find_result->diskname);
	amfree(output_find_result->label);
	amfree(output_find_result->partnum);
	amfree(output_find_result->status);
	amfree(output_find_result->timestamp);
	prev = output_find_result;
    }
    amfree(prev);
    *output_find = NULL;
}

int
find_match(
    char *host,
    char *disk)
{
    disk_t *dp = lookup_disk(host,disk);
    return (dp && dp->todo);
}

char *
find_nicedate(
    char *datestamp)
{
    static char nice[20];
    int year, month, day;
    int hours, minutes, seconds;
    char date[9], atime[7];
    int  numdate, numtime;

    strncpy(date, datestamp, 8);
    date[8] = '\0';
    numdate = atoi(date);
    year  = numdate / 10000;
    month = (numdate / 100) % 100;
    day   = numdate % 100;

    if(strlen(datestamp) <= 8) {
	g_snprintf(nice, SIZEOF(nice), "%4d-%02d-%02d",
		year, month, day);
    }
    else {
	strncpy(atime, &(datestamp[8]), 6);
	atime[6] = '\0';
	numtime = atoi(atime);
	hours = numtime / 10000;
	minutes = (numtime / 100) % 100;
	seconds = numtime % 100;

	g_snprintf(nice, SIZEOF(nice), "%4d-%02d-%02d %02d:%02d:%02d",
		year, month, day, hours, minutes, seconds);
    }

    return nice;
}

static int
parse_taper_datestamp_log(
    char *logline,
    char **datestamp,
    char **label)
{
    char *s;
    int ch;

    s = logline;
    ch = *s++;

    skip_whitespace(s, ch);
    if(ch == '\0') {
	return 0;
    }
    if(strncmp_const_skip(s - 1, "datestamp", s, ch) != 0) {
	return 0;
    }

    skip_whitespace(s, ch);
    if(ch == '\0') {
	return 0;
    }
    *datestamp = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';

    skip_whitespace(s, ch);
    if(ch == '\0') {
	return 0;
    }
    if(strncmp_const_skip(s - 1, "label", s, ch) != 0) {
	return 0;
    }

    skip_whitespace(s, ch);
    if(ch == '\0') {
	return 0;
    }
    *label = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';

    return 1;
}

/* if output_find is NULL					*/
/*	return 1 if this is the logfile for this label		*/
/*	return 0 if this is not the logfile for this label	*/
/* else								*/
/*	add to output_find all the dump for this label		*/
/*	return the number of dump added.			*/
int
search_logfile(
    find_result_t **output_find,
    char *label,
    char *datestamp,
    char *logfile)
{
    FILE *logf;
    char *host, *host_undo;
    char *disk, *qdisk, *disk_undo;
    char *date, *date_undo;
    char *partnum=NULL, *partnum_undo;
    char *number;
    int fileno;
    char *thelabel;
    char *rest;
    char *ck_label=NULL;
    int level = 0; 
    int tapematch;
    off_t filenum;
    int passlabel;
    char *ck_datestamp, *ck_datestamp2;
    char *s;
    int ch;
    disk_t *dp;
    find_result_t *part_find = NULL;  /* List for all part of a DLE */
    find_result_t *a_part_find;

    if((logf = fopen(logfile, "r")) == NULL) {
	error(_("could not open logfile %s: %s"), logfile, strerror(errno));
	/*NOTREACHED*/
    }

    /* check that this log file corresponds to the right tape */
    tapematch = 0;
    while(!tapematch && get_logline(logf)) {
	if(curlog == L_START && curprog == P_TAPER) {
	    if(parse_taper_datestamp_log(curstr,
					 &ck_datestamp, &ck_label) == 0) {
		g_printf(_("strange log line \"start taper %s\" curstr='%s'\n"),
		    logfile, curstr);
	    } else if(strcmp(ck_datestamp, datestamp) == 0
		      && strcmp(ck_label, label) == 0) {
		tapematch = 1;
	    }
	}
    }

    if(output_find == NULL) {
	afclose(logf);
        if(tapematch == 0)
	    return 0;
	else
	    return 1;
    }

    if(tapematch == 0) {
	afclose(logf);
	return 0;
    }

    filenum = (off_t)0;
    passlabel = 1;
    while(get_logline(logf) && passlabel) {
	if ((curlog == L_SUCCESS ||
	     curlog == L_CHUNK || curlog == L_PART || curlog == L_PARTPARTIAL) &&
	    curprog == P_TAPER && passlabel) {
	    filenum++;
	}
	if(curlog == L_START && curprog == P_TAPER) {
	    if(parse_taper_datestamp_log(curstr,
					 &ck_datestamp2, &ck_label) == 0) {
		g_printf(_("strange log line in %s \"start taper %s\"\n"),
		    logfile, curstr);
	    } else if (strcmp(ck_label, label)) {
		passlabel = !passlabel;
	    }
	}
	partnum = "--";
	if (curlog == L_SUCCESS || curlog == L_CHUNKSUCCESS ||
	    curlog == L_DONE    || curlog == L_FAIL ||
	    curlog == L_CHUNK   || curlog == L_PART || curlog == L_PARTIAL ||
	    curlog == L_PARTPARTIAL ) {
	    s = curstr;
	    ch = *s++;

	    skip_whitespace(s, ch);
	    if(ch == '\0') {
		g_printf(_("strange log line in %s \"%s\"\n"),
		    logfile, curstr);
		continue;
	    }

	    if (curlog == L_PART || curlog == L_PARTPARTIAL) {
		thelabel = s - 1;
		skip_non_whitespace(s, ch);
		s[-1] = '\0';

		if (strcmp(thelabel, label) != 0) {
		    g_printf("label doesn't match %s %s\n", thelabel, label);
		    continue;
		}
		skip_whitespace(s, ch);
		if(ch == '\0') {
		    g_printf("strange log line in %s \"%s\"\n",
			   logfile, curstr);
		    continue;
		}

		number = s - 1;
		skip_non_whitespace(s, ch);
		s[-1] = '\0';
		fileno = atoi(number);
		filenum = fileno;

		skip_whitespace(s, ch);
		if(ch == '\0') {
		    g_printf("strange log line in %s \"%s\"\n",
			   logfile, curstr);
		    continue;
		}
	    }

	    host = s - 1;
	    skip_non_whitespace(s, ch);
	    host_undo = s - 1;
	    *host_undo = '\0';

	    skip_whitespace(s, ch);
	    if(ch == '\0') {
		g_printf(_("strange log line in %s \"%s\"\n"),
		    logfile, curstr);
		continue;
	    }
	    qdisk = s - 1;
	    skip_quoted_string(s, ch);
	    disk_undo = s - 1;
	    *disk_undo = '\0';
	    disk = unquote_string(qdisk);

	    skip_whitespace(s, ch);
	    if(ch == '\0') {
		g_printf(_("strange log line in %s \"%s\"\n"),
		    logfile, curstr);
		continue;
	    }
	    date = s - 1;
	    skip_non_whitespace(s, ch);
	    date_undo = s - 1;
	    *date_undo = '\0';

	    if(strlen(date) < 3) { /* old log didn't have datestamp */
		level = atoi(date);
		date = stralloc(datestamp);
	    }
	    else {
		if (curlog == L_CHUNK || curlog == L_PART ||
		    curlog == L_PARTPARTIAL ||curlog == L_DONE){
		    skip_whitespace(s, ch);
		    partnum = s - 1;
		    skip_non_whitespace(s, ch);
		    partnum_undo = s - 1;
		    *partnum_undo = '\0';
		}
		skip_whitespace(s, ch);
		if(ch == '\0' || sscanf(s - 1, "%d", &level) != 1) {
		    g_printf(_("strange log line in %s \"%s\"\n"),
		    logfile, curstr);
		    continue;
		}
		skip_integer(s, ch);
	    }

	    skip_whitespace(s, ch);
	    if(ch == '\0') {
		g_printf(_("strange log line in %s \"%s\"\n"),
		    logfile, curstr);
		continue;
	    }
	    rest = s - 1;
	    if((s = strchr(s, '\n')) != NULL) {
		*s = '\0';
	    }

	    dp = lookup_disk(host,disk);
	    if ( dp == NULL ) {
		if (dynamic_disklist == 0) {
		    continue;
		}
		dp = add_disk(find_diskqp, host, disk);
		enqueue_disk(find_diskqp, dp);
	    }

            if (find_match(host, disk)) {
		if(curprog == P_TAPER) {
		    find_result_t *new_output_find =
			(find_result_t *)alloc(SIZEOF(find_result_t));
		    new_output_find->timestamp = stralloc(date);
		    new_output_find->hostname=stralloc(host);
		    new_output_find->diskname=stralloc(disk);
		    new_output_find->level=level;
		    new_output_find->partnum = stralloc(partnum);
		    new_output_find->label=stralloc(label);
		    new_output_find->filenum=filenum;
		    new_output_find->next=NULL;
		    if (curlog == L_SUCCESS) {
			new_output_find->status = stralloc("OK");
			new_output_find->next = *output_find;
			*output_find = new_output_find;
		    } else if (curlog == L_CHUNKSUCCESS || curlog == L_DONE ||
			       curlog == L_PARTIAL      || curlog == L_FAIL) {
			/* result line */
			if (curlog == L_PARTIAL || curlog == L_FAIL) {
			    /* change status of each part */
			    for (a_part_find = part_find; a_part_find;
				 a_part_find = a_part_find->next) {
				if (curlog == L_PARTIAL)
				    a_part_find->status = stralloc("PARTIAL");
				else
				    a_part_find->status = stralloc(rest);
			    }
			}
			if (part_find) { /* find last element */
			    for (a_part_find = part_find;
				 a_part_find->next != NULL;
				 a_part_find=a_part_find->next) {
			    }
			    /* merge part_find to *output_find */
			    a_part_find->next = *output_find;
			    *output_find = part_find;
			    part_find = NULL;
			}
		    } else { /* part line */
			if (curlog == L_PART || curlog == L_CHUNK)
			    new_output_find->status=stralloc("OK");
			else /* PARTPARTIAL */
			    new_output_find->status=stralloc("PARTIAL");
			/* Add to part_find list */
			new_output_find->next = part_find;
			part_find = new_output_find;
			if (curlog == L_PARTPARTIAL) {
			    for (a_part_find = part_find;
				 a_part_find->next != NULL;
				 a_part_find=a_part_find->next) {
			    }
			    /* merge part_find to *output_find */
			    a_part_find->next = *output_find;
			    *output_find = part_find;
			    part_find = NULL;
			}
		    }
		}
		else if(curlog == L_FAIL) {	/* print other failures too */
		    find_result_t *new_output_find =
			(find_result_t *)alloc(SIZEOF(find_result_t));
		    new_output_find->next=*output_find;
		    new_output_find->timestamp = stralloc(date);
		    new_output_find->hostname=stralloc(host);
		    new_output_find->diskname=stralloc(disk);
		    new_output_find->level=level;
		    new_output_find->label=stralloc(label);
		    new_output_find->partnum=stralloc(partnum);
		    new_output_find->filenum=0;
		    new_output_find->status=vstralloc(
			 "FAILED (",
			 program_str[(int)curprog],
			 ") ",
			 rest,
			 NULL);
		    *output_find=new_output_find;
		}
	    }
	    amfree(disk);
	}
    }
    afclose(logf);

    if (part_find != NULL) {
	dbprintf(_("part_find not empty %s %s\n"), logfile, label);
    }

    return 1;
}


/*
 * Return the set of dumps that match *all* of the given patterns (we consider
 * an empty pattern to match .*, though).  If 'ok' is true, will only match
 * dumps with SUCCESS status.
 */
find_result_t *
dumps_match(
    find_result_t *output_find,
    char *hostname,
    char *diskname,
    char *datestamp,
    char *level,
    int ok)
{
    find_result_t *cur_result;
    find_result_t *matches = NULL;

    for(cur_result=output_find;
	cur_result;
	cur_result=cur_result->next) {
	char level_str[NUM_STR_SIZE];
	g_snprintf(level_str, SIZEOF(level_str), "%d", cur_result->level);
	if((!hostname || *hostname == '\0' || match_host(hostname, cur_result->hostname)) &&
	   (!diskname || *diskname == '\0' || match_disk(diskname, cur_result->diskname)) &&
	   (!datestamp || *datestamp== '\0' || match_datestamp(datestamp, cur_result->timestamp)) &&
	   (!level || *level== '\0' || match_level(level, level_str)) &&
	   (!ok || !strcmp(cur_result->status, "OK"))){

	    find_result_t *curmatch = alloc(SIZEOF(find_result_t));
	    memcpy(curmatch, cur_result, SIZEOF(find_result_t));

/*
	    curmatch->hostname = stralloc(cur_result->hostname);
	    curmatch->diskname = stralloc(cur_result->diskname);
	    curmatch->datestamp = stralloc(cur_result->datestamp);
	    curmatch->partnum = stralloc(cur_result->partnum);
	    curmatch->status = stralloc(cur_result->status);
	    curmatch->level = stralloc(cur_result->level);
*/	    
	    curmatch->next = matches;
	    matches = curmatch;
	}
    }

    return(matches);
}

find_result_t *
dump_exist(
    find_result_t *output_find,
    char *hostname,
    char *diskname,
    char *datestamp,
    int level)
{
    find_result_t *output_find_result;

    for(output_find_result=output_find;
	output_find_result;
	output_find_result=output_find_result->next) {
	if( !strcmp(output_find_result->hostname, hostname) &&
	    !strcmp(output_find_result->diskname, diskname) &&
	    !strcmp(output_find_result->timestamp, datestamp) &&
	    output_find_result->level == level) {

	    return output_find_result;
	}
    }
    return(NULL);
}
