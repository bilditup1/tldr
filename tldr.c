/*
 * Copyright 2021 Ivan Kovmir
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* Includes */
#include <archive.h>
#include <archive_entry.h>
#include <curl/curl.h>
#include <dirent.h>
#include <ftw.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Constants and Macros */
#define BUF_SIZE 1024
#define D_NAME entry->d_name

/* Function prototypes */
/* Print error message and terminate the execution */
static void  error_terminate(const char *msg, const char *details);
/* Prints instructions on how to use the program. */
static void  tldr_usage(void);
/* Downloads pages. */
static void  fetch_pages(void);
/* Extracts pages and put them in place. */
static void  extract_pages(void);
/* Creates index file of all pages. */
static void  index_pages(void);
static int   ftw_callback(const char *path, const struct stat *sb, int typeflag);
/* Prints all page names. */
static void  list_pages(void);
/* Returns a file path to a given page. */
static char *find_page(const char *page_name);
/* Prints a given page. */
static void  display_page(const char *page_path);
/* Opens index file performing all the necessary checking. */
static FILE *open_index(const char *mode);

#include "config.h"

static const char *RESET_STYLING = "\033[0m";
/* Index file to hold available page names. */
static FILE *tldr_index;

inline void
error_terminate(const char *msg, const char *details)
{
	fprintf(stderr, "%s; details: %s.\n", msg, details? details : "none");
	exit(1);
}

inline void
tldr_usage(void)
{
	printf("USAGE: tldr [options] <[platform/]command>\n\n"
	    "[options]\n"
	    "\t-h:\tthis help overview\n"
	    "\t-l:\tshow all available pages\n"
	    "\t-u:\tfetch lastest copies of cached pages\n\n"
	    "[platform]\n"
	    "\tandroid\n"
	    "\tcommon\n"
	    "\tindex\n"
	    "\tlinux\n"
	    "\tosx\n"
	    "\tsunos\n"
	    "\twindows\n\n"
	    "<command>\n"
	    "\tShow examples for this command\n");
}

void
fetch_pages(void)
{
	CURL *ceh; /* cURL easy handle. */
	CURLcode cres; /* cURL operation result. */
	char err_curl[CURL_ERROR_SIZE]; /* Curl error message buffer. */
	FILE *tldr_archive; /* File to download to. */

	tldr_archive = fopen(PAGES_TMP, "w");
	if (!tldr_archive)
		error_terminate("Failed to create a temporary file", NULL);

	curl_global_init(CURL_GLOBAL_ALL);
	ceh = curl_easy_init();
	curl_easy_setopt(ceh, CURLOPT_WRITEDATA, tldr_archive);
	curl_easy_setopt(ceh, CURLOPT_URL, PAGES_URL);
	curl_easy_setopt(ceh, CURLOPT_ERRORBUFFER, err_curl);

	cres = curl_easy_perform(ceh);
	curl_easy_cleanup(ceh);
	curl_global_cleanup();
	fclose(tldr_archive);
	if (cres != CURLE_OK)
		error_terminate("Failed to fetch pages", err_curl);
}

void
extract_pages(void)
{
	char page_path[BUF_SIZE]; /* Path to save pages. */
	char archive_path[BUF_SIZE]; /* Path within archive to extract from. */
	FILE *tldr_archive;
	int ares; /* libarchive status. */
	struct archive *ap;
	struct archive_entry *aep;

	tldr_archive = fopen(PAGES_TMP, "r");
	if (tldr_archive == NULL)
		error_terminate("Failed to open the archive", NULL);

	ap = archive_read_new();
	if (ap == NULL)
		error_terminate("Failed to archive_read_new()", NULL);
	archive_read_support_format_zip(ap);
	ares = archive_read_open_FILE(ap, tldr_archive);
	if (ares != ARCHIVE_OK)
		error_terminate("Failed to archive_read_open_FILE()",
		    archive_error_string(ap));

	/* A place inside the archive to extract pages from. */
	strcpy(archive_path, "tldr-master");
	strcat(archive_path, PAGES_LANG);
	strcat(archive_path, "/");

	/* Find the folder within the archive to extract from. */
	while (archive_read_next_header(ap, &aep) != ARCHIVE_EOF)
		if (!strcmp(archive_entry_pathname(aep), archive_path))
			break;
	while (archive_read_next_header(ap, &aep) != ARCHIVE_EOF) {
		if (strncmp(archive_entry_pathname(aep),
		    archive_path, strlen(archive_path)))
			break;

		/* A place to put the extracted pages to. */
		strcpy(page_path, getenv("HOME"));
		strcat(page_path, PAGES_PATH);
		strcat(page_path, strchr(archive_entry_pathname(aep),'/'));
		archive_entry_set_pathname(aep, page_path);
		ares = archive_read_extract(ap, aep, 0);
		if (ares != ARCHIVE_OK)
			error_terminate("Failed to archive_read_extract()",
			    archive_error_string(ap));
	}
	archive_read_free(ap);
	fclose(tldr_archive);
}

void
index_pages(void)
{
	char buf[BUF_SIZE];

	strcpy(buf, getenv("HOME"));
	strcat(buf, PAGES_PATH);
	strcat(buf, PAGES_LANG);
	tldr_index = open_index("w");
	ftw(buf, ftw_callback, 10);
	fclose(tldr_index);
}

int
ftw_callback(const char *path, const struct stat *sb, int typeflag)
{
	(void)sb; /* Suppress compiler warnings about unused *sb. */
	if (typeflag == FTW_F)
		fprintf(tldr_index, "%s\n", strchr(strstr(path, PAGES_LANG)+1, '/')+1);
	return 0;
}

void
list_pages(void)
{
	char buf[BUF_SIZE];

	tldr_index = open_index("w");
	while(fgets(buf, BUF_SIZE, tldr_index))
		printf("%s", strchr(buf+1, '/')+1);
	fclose(tldr_index);
}

char *
find_page(const char *page_name)
{
	static char buf[BUF_SIZE];

	tldr_index = open_index("r");
	char page_filename[strlen(page_name)+5]; /* for '.md\n\0' */
	strcpy(page_filename, page_name);
	strcat(page_filename, ".md\n");
	while(fgets(buf, BUF_SIZE, tldr_index)) {
		/* page_name is either 'command' or 'platform/command'. */
		if (strchr(page_name, '/')) { /* platform/command */
			if(!strcmp(page_filename, buf)) {
				*strchr(buf, '\n') = '\0';
				return buf;
			}
		} else { /* command */
			if (!strcmp(page_filename, strchr(buf, '/')+1)) {
				*strchr(buf, '\n') = '\0';
				return buf;
			}
		}
	}
	fclose(tldr_index);
	return NULL;
}

void
display_page(const char *page_name)
{
	const char *page_path = find_page(page_name);
	if (!page_path) {
		puts("The page has not been found.");
		exit(1);
	}

	char buf[BUF_SIZE];
	FILE *page;
	strcpy(buf, getenv("HOME"));
	strcat(buf, PAGES_PATH);
	strcat(buf, PAGES_LANG);
	strcat(buf, "/");
	strcat(buf, page_path);

	page = fopen(buf, "r");
	while (fgets(buf, BUF_SIZE, page)) {
		if (!strcmp(buf, "\n")) {
			/* Skip empty lines. */
			continue;
		}
		if (buf[0] == '#') {
			printf("%s%s%s", HEADING_STYLE, buf, RESET_STYLING);
		}
		if (buf[0] == '>') {
			printf("%s%s%s", SUBHEADING_STYLE, buf, RESET_STYLING);
		}
		if (buf[0] == '-') {
			printf("%s%s%s", COMMAND_DESC_STYLE, buf, RESET_STYLING);
		}
		if (buf[0] == '`') {
			printf("%s%s%s", COMMAND_STYLE, buf, RESET_STYLING);
		}

	}
	fclose(page);
}

FILE *
open_index(const char *mode)
{
	char buf[BUF_SIZE];
	FILE *fp;
	strcpy(buf, getenv("HOME"));
	strcat(buf, PAGES_PATH);
	strcat(buf, "/index");
	fp = fopen(buf, mode);
	if (!fp)
		error_terminate("Failer to open index, "
		    "probably you should run 'tldr -u'", mode);
	return fp;
}

int
main(int argc, char *argv[])
{
	if (argc != 2) { /* Wrong number of arguments. */
		tldr_usage();
		return 1;
	}

	if (!strcmp("-h", argv[1])) { /* Show usage help. */
		tldr_usage();
	} else if (!strcmp("-u", argv[1])) { /* Update pages. */
		puts("Fetching pages...");
		fetch_pages();
		puts("Extracting pages...");
		extract_pages();
		puts("Indexing pages...");
		index_pages();
	} else if (!strcmp("-l", argv[1])) { /* Print all pages names. */
		list_pages();
	} else {
		display_page(argv[1]); /* Display a given page. */
	}
	return 0;
}
