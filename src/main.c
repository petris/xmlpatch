/**
 * This is part of an XML patch utility.
 *
 * Copyright (C) 2011 Petr Malat
 *
 * Contact: Petr Malat <oss@malat.biz>
 *
 * This utility is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License 
 * version 3 as published by the Free Software Foundation.
 *
 * This utility is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <unistd.h>
#include <getopt.h>

#include <libxml/tree.h>

#include "xml_patch.h"
#include "buf.h"

#define ERROR_UNKNOWN_FILE 1
#define ERROR_INVALID_FILE 2

static struct patch_settings_s {
	long strip;
	bool quiet;
	bool expand;
	bool backup;
	bool keep_blank;
	int patch_fd;
	char *file;
	char *prefix;
	char *suffix;
	char *basename_prefix;
	enum { REJ_NONE, REJ_LOCAL, REJ_GLOBAL } reject;
	xmlNodePtr reject_root;
	xmlOutputBufferPtr output;
} settings = {
	.strip = -1,
	.quiet = false,
	.expand = false,
	.backup = false,
	.keep_blank = false,
	.patch_fd = -1,
	.file = NULL,
	.prefix = "",
	.suffix = "",
	.basename_prefix = "",
	.reject = REJ_LOCAL,
	.reject_root = NULL,
	.output = NULL,
};

static void usage()
{
	fprintf (stderr, "Usage: xmlpatch [OPTION]... [ORIGFILE [PATCHFILE]]\n\n"
		"Input options:\n\n"
		"  -p NUM  --strip=NUM  Strip NUM leading components from file names.\n"
		"  -x  --expand  Expand environment variables found in patch file.\n"
		"  -k  --keep-blank  Do not ignore text nodes containing only whitespaces.\n"
		"  -i PATCHFILE  --input=PATCHFILE  Read patch from PATCHFILE instead of stdin.\n\n"
		"Output options:\n\n"
		"  -o FILE  --output=FILE  Output patched files to FILE.\n"
		"  -r FILE  --reject-file=FILE  Output rejects to FILE.\n\n"
		"Backup and version control options:\n\n"
		"  -b  --backup  Back up the original contents of each file.\n\n"
		"  -B PREFIX  --prefix=PREFIX  Prepend PREFIX to backup file names.\n"
		"  -Y PREFIX  --basename-prefix=PREFIX  Prepend PREFIX to backup file basenames.\n"
		"  -z SUFFIX  --suffix=SUFFIX  Append SUFFIX to backup file names.\n\n"
		"Miscellaneous options:\n\n"
		"  -s  --quiet  --silent  Work silently unless an error occurs.\n\n"
		"  -d DIR  --directory=DIR  Change the working directory to DIR first.\n\n"
		"  -v  --version  Output version info.\n"
		"  -h  --help  Output this help.\n");

}

/*
 * File or filename operations
 */

/** Strip first settings.strip parts of path */
static char *filename_strip(char *filename)
{
	if (settings.strip < 0) { // Use filename only
		char *last_slash = rindex(filename, '/');
		if (last_slash) {
			filename = last_slash + 1;
		}
	} else { // Strip path
		for (long strip = settings.strip; strip && *filename; filename++) {
			if (filename[0] == '/') {
				while (filename[1] == '/') filename++;
				strip--;
			} 
		}
	}
	return filename;
}

/** Backup file according to settings */
static void file_backup(char *filename) 
{
	char *last_slash, *slash;
	buf_t backup_file = buf_new();

	buf_append(backup_file, settings.prefix, strlen(settings.prefix));

	last_slash = rindex(filename, '/');
	if (last_slash) {
		buf_append(backup_file, filename, last_slash - filename + 1);
		last_slash++;
	} else {
		last_slash = filename;
	}
	buf_append(backup_file, settings.basename_prefix, strlen(settings.basename_prefix));
	buf_append(backup_file, last_slash, strlen(last_slash));
	buf_append(backup_file, settings.suffix, strlen(settings.suffix));
	buf_append(backup_file, "\0", 1);

	/* Make directory */
	for (slash = index(buf_data(backup_file) + 1, '/'); slash; slash = index(slash + 1, '/')) {
		struct stat statbuf;
		*slash = 0;

		if (0 == stat(buf_data(backup_file), &statbuf)) {
			*slash = '/';
			continue;
		}

		if (0 != mkdir(buf_data(backup_file), 0777)) {
			error(EXIT_FAILURE, errno, "**** Can't create directory %s", (char*)buf_data(backup_file));
		}

		*slash = '/';
	}

	/* Rename or copy file */
	if (0 != rename(filename, buf_data(backup_file))) {
		if (errno == EXDEV) {
			int orig, backup;
			ssize_t rd, wr, wr_now;
			char buf[4096];

			if ((orig = open(filename, O_RDONLY)) < 0) {
				error(EXIT_FAILURE, errno, "**** Can't open file %s", filename);
			}
			if ((backup = open(buf_data(backup_file), O_CREAT | O_WRONLY)) < 0) {
				error(EXIT_FAILURE, errno, "**** Can't open backup file %s", (char*)buf_data(backup_file));
			}

			do {
				rd = read(orig, buf, sizeof buf);
				wr = 0;
				if (rd > 0) {
					while (wr < rd) {
						wr_now = write(backup, buf + wr, rd - wr);
						if (wr_now > 0) {
							wr += wr_now;
						} else if (errno != EINTR) {
							error(EXIT_FAILURE, errno, "**** Can't write to backup file %s", filename);
						}
					}
				} else if (rd < 0) {
					if (errno == EINTR) {
						rd = 1;
					} else {
						error(EXIT_FAILURE, errno, "**** Can't read file %s", filename);
					}
				}
			} while (rd > 0);

			close(orig);
			close(backup);
		} else {
			error(EXIT_FAILURE, errno, "**** Can't rename file %s to %s", filename, (char*)buf_data(backup_file));
		}
	}

	buf_free(backup_file);
}

/*
 * Patch loading related functions
 */

/** Parse identifier and return its value */
static char *get_var(char *data, unsigned size, unsigned *used)
{
	static char value[3];

	if (size < 3) {
		*used = size;
		return NULL;
	}

	assert(data[0] == '$');

	if (data[1] == '$') {
		value[0] = '$';
		value[1] = 0;
		*used = 2;
		return value;
	}

	if (!(isalpha(data[1]) || data[1] == '_')) { // Not a identifier
		value[0] = '$';
		value[1] = data[1];
		value[2] = 0;
		*used = 2;
		return value;
	}

	for (unsigned i = 1; i < size - 1; i++) {
		if (!(isalnum(data[i]) || data[i] == '_')) { // Identifier found
			char *env_val;
			int c;

			c = data[i];
			data[i] = 0;
			env_val = getenv(data + 1);
			data[i] = c;
			*used = i;
			if (env_val) {
				return env_val;
			} else {
				return "";
			}
		}
	}

	*used = size;
	return NULL;
}

/** Read XML patch from filedescriptor */
static xmlDocPtr read_patch(int fd)
{
	unsigned roffset = 0;
	char rbuf[1024];
	xmlDocPtr doc;
	int rd;
	buf_t data = buf_new();


	do {
		rd = read(fd, rbuf + roffset, sizeof rbuf - roffset);
		if (rd > 0) {
			if (settings.expand) {
				char *vbuf, *var;

				for (vbuf = rbuf; vbuf < rbuf + rd;) {
					var = memchr(vbuf, '$', rbuf + rd - vbuf);
					if (var) {
						unsigned used;
						char *env;

						buf_append(data, vbuf, var - vbuf);
						vbuf = var;
						env = get_var(var, rbuf + rd - vbuf, &used);
						if (env == NULL) {
							if (roffset) {
								error(EXIT_FAILURE, errno, 
									"**** Too long (> %luB) identifier found",
								    	sizeof rbuf);
							}
							memmove(rbuf, vbuf, rbuf + rd - vbuf);
							roffset = rbuf + rd - vbuf;
						} else {
							buf_append(data, env, strlen(env));
							roffset = 0;
						}
						vbuf += used;
					} else {
						buf_append(data, vbuf, rbuf + rd - vbuf);
						vbuf += rbuf + rd - vbuf;
					}
				}
			} else {
				buf_append(data, rbuf, rd);
			}
		} else if (rd < 0) {
			if (errno == EINTR) { // Interrupted by signal
				rd = 1;
			} else {
				error(EXIT_FAILURE, errno, "**** Reading patch file failed");
			}
		} 
	} while (rd > 0);

	doc = xmlParseMemory(buf_data(data), buf_size(data));
	buf_free(data);

	return doc;
}

/** Recursively unlink blank nodes */
static void strip_blank_nodes(xmlNodePtr node)
{
	while (node) {
		xmlNodePtr next = node->next;
		if (xmlIsBlankNode(node)) {
			xmlUnlinkNode(node);
			xmlFree(node);
		} else if (node->type == XML_ELEMENT_NODE) {
			strip_blank_nodes(node->children);
		}
		node = next;
	}
}

/*
 * Reject generation
 */

typedef xmlNodePtr reject_t;

static void reject_handle(reject_t *reject, char *file, xmlNodePtr node)
{
	if (settings.reject == REJ_NONE) {
		return;
	}
	if (*reject == NULL) {
		*reject = xmlNewNode(NULL, (xmlChar*)"change");
		xmlNewProp(*reject, BAD_CAST "file", BAD_CAST file);
	}
	xmlAddChild(*reject, node);
}

static void reject_finalize(reject_t *reject, char *file)
{
	if (*reject) {
		xmlNodePtr rejected_node = xmlCopyNode(*reject, 1);
		if (settings.reject == REJ_LOCAL) {
			xmlDocPtr doc;
			xmlNodePtr root_node;
			buf_t rejfile = buf_new();

			/* Create XML document */
			doc = xmlNewDoc(BAD_CAST "1.0");
			root_node = xmlNewNode(NULL, BAD_CAST "changes");
			xmlDocSetRootElement(doc, root_node);
			xmlAddChild(root_node, rejected_node);

			buf_append(rejfile, file, strlen(file));
			buf_append(rejfile, ".rej", sizeof ".rej");

			xmlSaveFile(buf_data(rejfile), doc);
			xmlFreeDoc(doc);
			buf_free(rejfile);
		} else {
			assert(settings.reject == REJ_GLOBAL);
			if (settings.reject_root == NULL) {
				settings.reject_root = xmlNewNode(NULL, BAD_CAST "changes");
			}
			xmlAddChild(settings.reject_root, rejected_node);
		}
	}
}

/*
 * Patching
 */
struct output_context_s {
	int last_char;
	FILE *file;
};

static int output_write(void * context, const char * buffer, int len)
{
	struct output_context_s *ctx = context;

	ctx->last_char = buffer[len - 1];
	return fwrite(buffer, len, 1, ctx->file);
}

static int output_finalize(void * context)
{
	struct output_context_s *ctx = context;

	if (ctx->last_char == '\n') {
		return 0;
	} else {
		return fputc('\n', ctx->file);
	}
}

/** Output appender - used to output multiple XMLs into one file */
static xmlOutputBufferPtr xmlOutputAppend(FILE *file, xmlCharEncodingHandlerPtr encoder)
{
	xmlOutputBufferPtr ret;
	static struct output_context_s context;

	ret = xmlAllocOutputBuffer(encoder);
	if (ret != NULL) {
		context.file = file;
		ret->context = &context;
		ret->writecallback = output_write;
		ret->closecallback = output_finalize;
	}
	return ret;
}

/** Apply the single change */
static int xmldoc_change(xmlDocPtr doc, xmlNodePtr node)
{
	if (!strcmp ((char*)node->name, "add")) {
		return xml_patch_add(doc, node);
	} else if (!strcmp ((char*)node->name, "replace")) {
		return xml_patch_replace(doc, node);
	} else if (!strcmp ((char*)node->name, "remove")) {
		return xml_patch_remove(doc, node);
	} 
	return -1;
}

/** Apply changes from one change node */
static int handle_change_node(xmlNodePtr node, xmlDocPtr patch_doc)
{
	reject_t reject = NULL;
	xmlChar *xmlfile = NULL;
	xmlDocPtr doc;
	int rc = 0;
	char *file;

	/* Compose patched file name */
	if (settings.file) {
		file = settings.file;
	} else {
		xmlfile = xmlGetProp(node, (const xmlChar*)"file");
		if (xmlfile == NULL) {
			return ERROR_UNKNOWN_FILE;
		}
		file = filename_strip((char*)xmlfile);
	} 

	doc = xmlParseFile(file);
	if (doc == NULL) {
		xmlFree(file);
		return ERROR_INVALID_FILE;
	}

	if (!settings.quiet) {
		fprintf(stderr, "Patching %s\n", file);
	}

	/* Iterate over changes */
	int i = 1;
	for (node = node->children; node; node = node->next, i++) {
		if (node->type == XML_ELEMENT_NODE) {
			if (!settings.quiet) {
				fprintf(stderr, "Applying %d - %s", i, node->name);
				xmlElemDump(stderr, patch_doc, node);
			}
			int sub_rc = xmldoc_change(doc, node);
			if (sub_rc) {
				reject_handle(&reject, file, node);
			}
			if (!settings.quiet) {
				fprintf(stderr, " %s.\n", sub_rc ? "failed" : "succeeded");
			}
			rc |= sub_rc;
		}
	}

	/* Write patched file */
	if (settings.output == NULL) {
		if (settings.backup) {
			file_backup(file);
		}
		xmlSaveFile(file, doc);
	} else {
		xmlSaveFileTo(settings.output, doc, NULL);
	}
	xmlFreeDoc(doc);

	reject_finalize(&reject, file);

	/* Clean up */
	if (settings.file == NULL) {
		xmlFree(xmlfile);
	}
	return rc;
}

/** Perform patching according to settings */
static int patch(void)
{
	xmlDocPtr patch_doc;
	xmlNodePtr node;
	int rc = 0;

	/* Read and parse patch */
	patch_doc = read_patch(settings.patch_fd); 
	close(settings.patch_fd);
	if (patch_doc == NULL) {
		error(EXIT_FAILURE, errno, "**** Invalid patch file");
	}

	/* Apply patch */
	node = xmlDocGetRootElement(patch_doc);
	if (!settings.keep_blank) {
		strip_blank_nodes(node);
	}

	if (strcmp((const char*)node->name, "change") == 0) {
		rc = handle_change_node(node, patch_doc);
	} else if (strcmp((const char*)node->name, "changes") == 0) {
		for (node = node->children; node; node = node->next) {
			if (strcmp((const char *)node->name, "change") == 0) {
				rc |= handle_change_node(node, patch_doc);
			}
		}
	}

	/* Clean up */
	xmlFreeDoc(patch_doc);
	xmlCleanupParser();

	if (!settings.quiet) {
		fprintf (stderr, "Patching %s\n", !rc ? "successful" : "failed");
	} 

	return rc;
}

/** main for xml-patching, handle command line arguments */
int main(int argc, char *argv[])
{
	char *reject_file = NULL;
	int rc;
	int c;

	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
			{"directory", 1, 0, 'd'},
			{"input", 1, 0, 'i'},
			{"output", 1, 0, 'o'},
			{"strip", 1, 0, 'p'},
			{"reject-file", 1, 0, 'r'},
			{"silent", 0, 0, 's'},
			{"keep-blank", 0, 0, 'k'},
			{"quiet", 0, 0, 's'},
			{"version", 0, 0, 'v'},
			{"backup", 0, 0, 'b'},
			{"prefix", 1, 0, 'B'},
			{"basename-prefix", 1, 0, 'Y'},
			{"suffix", 1, 0, 'z'},
			{"expand", 0, 0, 'x'},
			{"help", 0, 0, 'h'},
			{0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "d:i:o:p:r:svkbB:Y:z:xh",
				long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
			case 'd': // directory
				if (chdir(optarg) != 0) {
					error(EXIT_FAILURE, errno, "**** Can't change to directory %s", optarg);
				}
				break;

			case 'i': // input
				if (strcmp("-", optarg) == 0) {
					settings.patch_fd = dup(0);
				} else {
					settings.patch_fd = open(optarg, O_RDONLY);
				}
				if (settings.patch_fd < 0) {
					error(EXIT_FAILURE, errno, "**** Can't open patch file %s", optarg);
				}
				break;

			case 'o': // output
				if (strcmp("-", optarg) == 0) {
					settings.output = xmlOutputAppend(stdout, NULL);
				} else {
					FILE *output = fopen(optarg, "w");
					if (output == NULL) {
						error(EXIT_FAILURE, errno, "**** Can't open output file %s", optarg);
					}
					settings.output = xmlOutputAppend(output, NULL);
				}
				break;

			case 'p': // strip
				{
					char *endptr;
					settings.strip = strtol(optarg, &endptr, 10);
					if (*endptr != '\0' || optarg[0] == '\0' || settings.strip < 0) {
						error(EXIT_FAILURE, 0, "**** strip count %s is not a "
								       "non-negative number", optarg);
					}
					break;
				}

			case 'r': // reject-file
				if (strcmp(optarg, "-")) {
					settings.reject = REJ_NONE;
				} else {
					settings.reject = REJ_GLOBAL;
					reject_file = optarg;
				}
				break;

			case 's': // quit, silent
				settings.quiet = true;
				break;

			case 'v': // version
				fprintf(stderr, "Version: 0.1\n");
				return 0;

			case 'b': // backup
				settings.backup = true;
				break; 

			case 'k': // keep-blank
				settings.keep_blank = true;
				break; 

			case 'B': // prefix
				settings.prefix = optarg;
				break;

			case 'Y': // basename-prefix
				settings.basename_prefix = optarg;
				break;

			case 'z': // suffix
				settings.suffix = optarg;
				break;

			case 'x': // expand
				settings.expand = true;
				break;

			case 'h': // help
				usage();
				return 0;

			default:
				return EXIT_FAILURE;
		}
	}

	if (optind < argc) {
		settings.file = argv[optind++];
	} 

	if (optind < argc) {
		if (settings.patch_fd != -1) {
			error(EXIT_FAILURE, 0, "**** Patch file specified multiple times");
		}

		settings.patch_fd = open(argv[optind], O_RDONLY);
		if (settings.patch_fd < 0) {
			error(EXIT_FAILURE, errno, "**** Can't open patch file %s", argv[optind]);
		}
		optind++;
	}

	if (settings.patch_fd == -1) {
		settings.patch_fd = dup(0);
		if (settings.patch_fd == -1) {
			error(EXIT_FAILURE, errno, "**** Can't read patch file from stdin");
		}
	}

	if (optind < argc) {
		error(EXIT_FAILURE, 0, "**** Unexpected argument %s\n", argv[optind]);
	}

	if (settings.backup && settings.prefix[0] == 0 && settings.suffix[0] == 0
			&& settings.basename_prefix[0] == 0) {
		settings.suffix = getenv("SIMPLE_BACKUP_SUFFIX");
		if (settings.suffix == NULL) {
			settings.suffix = ".orig";
		}
	}

	rc = patch();
	if (settings.reject_root) {
		assert(reject_file != NULL);
		xmlDocPtr doc;
		doc = xmlNewDoc(BAD_CAST "1.0");
		xmlDocSetRootElement(doc, settings.reject_root);
		xmlSaveFile(reject_file, doc);
		xmlFreeDoc(doc);
	}
	return rc;
}

