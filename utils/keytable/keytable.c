/* keytable.c - This program allows checking/replacing keys at IR

   Copyright (C) 2006-2010 Mauro Carvalho Chehab <mchehab@redhat.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <argp.h>

#include "parse.h"

struct keytable {
	int codes[2];
	struct keytable *next;
};

struct uevents {
	char		*key;
	char		*value;
	struct uevents	*next;
};

struct sysfs_names  {
	char			*name;
	struct sysfs_names	*next;
};

static int parse_code(char *string)
{
	struct parse_key *p;

	for (p = keynames; p->name != NULL; p++) {
		if (!strcasecmp(p->name, string))
			return p->value;
	}
	return -1;
}

const char *argp_program_version = "IR keytable control version 0.1.0";
const char *argp_program_bug_address = "Mauro Carvalho Chehab <mchehab@redhat.com>";

static const char doc[] = "\nAllows get/set IR keycode/scancode tables\n"
	"You need to have read permissions on /dev/input for the program to work\n"
	"\nOn the options bellow, the arguments are:\n"
	"  DEV     - the /dev/input/event* device to control\n"
	"  SYSDEV  - the ir class as found at /sys/class/rc\n"
	"  TABLE   - a file wit a set of scancode=keycode value pairs\n"
	"  SCANKEY - a set of scancode1=keycode1,scancode2=keycode2.. value pairs\n"
	"\nOptions can be combined together.";

static const struct argp_option options[] = {
	{"verbose",	'v',	0,		0,	"enables debug messages", 0},
	{"clear",	'c',	0,		0,	"clears the old table", 0},
	{"sysdev",	's',	"SYSDEV",	0,	"ir class device to control", 0},
	{"device",	'd',	"DEV",		0,	"ir device to control", 0},
	{"read",	'r',	0,		0,	"reads the current scancode/keycode table", 0},
	{"write",	'w',	"TABLE",	0,	"write (adds) the scancodes to the device scancode/keycode table from an specified file", 0},
	{"set-key",	'k',	"SCANKEY",	0,	"Change scan/key pairs", 0},
	{ 0, 0, 0, 0, 0, 0 }
};

static const char args_doc[] =
	"--device [/dev/input/event* device]\n"
	"--sysdev [ir class (f. ex. rc0)]\n"
	"[for using the rc0 sysdev]";

/* Static vars to store the parameters */
static char *devclass = "rc0";
static char *devname = NULL;
static int read = 0;
static int clear = 0;
static int debug = 0;

struct keytable keys = {
	{0, 0}, NULL
};

/*
 * Values that are read only via sysfs node
 */
static int sysfs = 0;

struct rc_device {
	char *sysfs_name;	/* Device sysfs node name */
	char *input_name;	/* Input device file name */
	char *drv_name;		/* Kernel driver that implements it */
	char *keytable_name;	/* Keycode table name */

};

struct keytable *nextkey = &keys;

static error_t parse_keyfile(char *fname, char **table, char **type)
{
	FILE *fin;
	int value, line = 0;
	char *scancode, *keycode, s[2048];

	*table = NULL;
	*type = NULL;

	if (debug)
		fprintf(stderr, "Parsing %s keycode file\n", fname);

	fin = fopen(fname, "r");
	if (!fin) {
		perror("opening keycode file");
		return errno;
	}

	while (fgets(s, sizeof(s), fin)) {
		char *p = s;
		while (*p == ' ' || *p == '\t')
			p++;
		if (!line && p[0] == '#') {
			p++;
			p = strtok(p, "\n\t =:");
			do {
				if (!strcmp(p, "table")) {
					p = strtok(NULL,"\n, ");
					*table = malloc(strlen(p) + 1);
					strcpy(*table, p);
				} else if (!strcmp(p, "type")) {
					p = strtok(NULL,"\n, ");
					*type = malloc(strlen(p) + 1);
					strcpy(*type, p);
				} else {
					goto err_einval;
				}
				p = strtok(NULL, "\n\t =:");
			} while (p);
			continue;
		}

		if (*p == '\n' || *p == '#')
			continue;

		scancode = strtok(p, "\n\t =:");
		if (!scancode)
			goto err_einval;
		if (!strcasecmp(scancode, "scancode")) {
			scancode = strtok(NULL, "\n\t =:");
			if (!scancode)
				goto err_einval;
		}

		keycode = strtok(NULL, "\n\t =:(");
		if (!keycode)
			goto err_einval;

		if (debug)
			fprintf(stderr, "parsing %s=%s:", scancode, keycode);
		value = parse_code(keycode);
		if (debug)
			fprintf(stderr, "\tvalue=%d\n", value);

		if (value == -1) {
			value = strtol(keycode, NULL, 0);
			if (errno)
				perror("value");
		}

		nextkey->codes[0] = (unsigned) strtol(scancode, NULL, 0);
		nextkey->codes[1] = (unsigned) value;
		nextkey->next = calloc(1, sizeof(*nextkey));
		if (!nextkey->next) {
			perror("No memory");
			return ENOMEM;
		}
		nextkey = nextkey->next;
		line++;
	}
	fclose(fin);

	return 0;

err_einval:
	fprintf(stderr, "Invalid parameter on line %d of %s\n",
		line + 1, fname);
	return EINVAL;

}

static error_t parse_opt(int k, char *arg, struct argp_state *state)
{
	char *p;
	long key;
	int rc;

	switch (k) {
	case 'v':
		debug++;
		break;
	case 'c':
		clear++;
		break;
	case 'd':
		devname = arg;
		break;
	case 's':
		devclass = arg;
		break;
	case 'r':
		read++;
		break;
	case 'w': {
		char *name = NULL, *type = NULL;

		rc = parse_keyfile(arg, &name, &type);
		if (rc < 0)
			goto err_inval;
		if (name && type)
			fprintf(stderr, "Read %s table, type %s\n", name, type);
		break;
	}
	case 'k':
		p = strtok(arg, ":=");
		do {
			if (!p)
				goto err_inval;
			nextkey->codes[0] = strtol(p, NULL, 0);
			if (errno)
				goto err_inval;

			p = strtok(NULL, ",;");
			if (!p)
				goto err_inval;
			key = parse_code(p);
			if (key == -1) {
				key = strtol(p, NULL, 0);
				if (errno)
					goto err_inval;
			}
			nextkey->codes[1] = key;

			if (debug)
				fprintf(stderr, "scancode %i=%i\n",
					nextkey->codes[0], nextkey->codes[1]);

			nextkey->next = calloc(1, sizeof(keys));
			if (!nextkey->next) {
				perror("No memory!\n");
				return ENOMEM;
			}
			nextkey = nextkey->next;

			p = strtok(NULL, ":=");
		} while (p);
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;

err_inval:
	fprintf(stderr, "Invalid parameter(s)\n");
	return EINVAL;

}

static struct argp argp = {
	.options = options,
	.parser = parse_opt,
	.args_doc = args_doc,
	.doc = doc,
};

static void prtcode(int *codes)
{
	struct parse_key *p;

	for (p = keynames; p->name != NULL; p++) {
		if (p->value == (unsigned)codes[1]) {
			printf("scancode 0x%04x = %s (0x%02x)\n", codes[0], p->name, codes[1]);
			return;
		}
	}

	if (isprint (codes[1]))
		printf("scancode %d = '%c' (0x%02x)\n", codes[0], codes[1], codes[1]);
	else
		printf("scancode %d = 0x%02x\n", codes[0], codes[1]);
}

static void free_names(struct sysfs_names *names)
{
	struct sysfs_names *old;
	do {
		old = names;
		names = names->next;
		if (old->name)
			free(old->name);
		free(old);
	} while (names);
}

static struct sysfs_names *seek_sysfs_dir(char *dname, char *node_name)
{
	DIR             	*dir;
	struct dirent   	*entry;
	struct sysfs_names	*names, *cur_name;

	names = calloc(sizeof(*names), 1);

	cur_name = names;

	dir = opendir(dname);
	if (!dir) {
		perror(dname);
		return NULL;
	}
	entry = readdir(dir);
	while (entry) {
		if (!strncmp(entry->d_name, node_name, strlen(node_name))) {
			cur_name->name = malloc(strlen(dname) + strlen(entry->d_name) + 2);
			if (!cur_name->name)
				goto err;
			strcpy(cur_name->name, dname);
			strcat(cur_name->name, entry->d_name);
			strcat(cur_name->name, "/");
			cur_name->next = calloc(sizeof(*cur_name), 1);
			if (!cur_name->next)
				goto err;
			cur_name = cur_name->next;
		}
		entry = readdir(dir);
	}
	closedir(dir);

	if (names == cur_name) {
		fprintf(stderr, "Couldn't find any node at %s%s*.\n",
			dname, node_name);
		free (names);
		names = NULL;
	}
	return names;

err:
	perror("Seek dir");
	free_names(names);
	return NULL;
}

static void free_uevent(struct uevents *uevent)
{
	struct uevents *old;
	do {
		old = uevent;
		uevent = uevent->next;
		if (old->key)
			free(old->key);
		if (old->value)
			free(old->value);
		free(old);
	} while (uevent);
}

/* Satisfies the stupid gcc logic that implements -Wmissing-prototypes */
struct uevents *read_sysfs_uevents(char *dname);

struct uevents *read_sysfs_uevents(char *dname)
{
	FILE		*fp;
	struct uevents	*next, *uevent;
	char		*event = "uevent", *file, s[4096];

	next = uevent = calloc(1, sizeof(*uevent));

	file = malloc(strlen(dname) + strlen(event) + 1);
	strcpy(file, dname);
	strcat(file, event);

	if (debug)
		fprintf(stderr, "Parsing uevent %s\n", file);


	fp = fopen(file, "r");
	if (!fp) {
		perror(file);
		free(file);
		return NULL;
	}
	while (fgets(s, sizeof(s), fp)) {
		char *p = strtok(s, "=");
		if (!p)
			continue;
		next->key = malloc(strlen(p) + 1);
		if (!next->key) {
			perror("next->key");
			free(file);
			free_uevent(uevent);
			return NULL;
		}
		strcpy(next->key, p);

		p = strtok(NULL, "\n");
		if (!p) {
			fprintf(stderr, "Error on uevent information\n");
			fclose(fp);
			free(file);
			free_uevent(uevent);
			return NULL;
		}
		next->value = malloc(strlen(p) + 1);
		if (!next->value) {
			perror("next->value");
			free(file);
			free_uevent(uevent);
			return NULL;
		}
		strcpy(next->value, p);

		if (debug)
			fprintf(stderr, "%s uevent %s=%s\n", file, next->key, next->value);

		next->next = calloc(1, sizeof(*next));
		if (!next->next) {
			perror("next->next");
			free(file);
			free_uevent(uevent);
			return NULL;
		}
		next = next->next;
	}
	fclose(fp);
	free(file);

	return uevent;
}

static struct sysfs_names *find_device(char *name)
{
	char		dname[256];
	char		*input = "rc";
	static struct sysfs_names *names, *cur;
	/*
	 * Get event sysfs node
	 */
	snprintf(dname, sizeof(dname), "/sys/class/rc/");

	names = seek_sysfs_dir(dname, input);
	if (!names)
		return NULL;

	if (debug) {
		for (cur = names; cur->next; cur = cur->next) {
			fprintf(stderr, "Found device %s\n", cur->name);
		}
	}

	if (name) {
		static struct sysfs_names *tmp;
		char *p, *n;
		int found = 0;

		n = malloc(strlen(name) + 2);
		strcpy(n, name);
		strcat(n,"/");
		for (cur = names; cur->next; cur = cur->next) {
			if (cur->name) {
				p = cur->name + strlen(dname);
				if (p && !strcmp(p, n)) {
					found = 1;
					break;
				}
			}
		}
		free(n);
		if (!found) {
			free_names(names);
			fprintf(stderr, "Not found device %s\n", name);
			return NULL;
		}
		tmp = calloc(sizeof(*names), 1);
		tmp->name = cur->name;
		cur->name = NULL;
		free_names(names);
		return tmp;
	}

	return names;
}

static int get_attribs(struct rc_device *rc_dev)
{
	struct uevents  *uevent;
	char		*input = "input", *event = "event";
	char		*DEV = "/dev/";
	static struct sysfs_names *input_names, *event_names;

	input_names = seek_sysfs_dir(rc_dev->sysfs_name, input);
	if (!input_names)
		return EINVAL;
	if (input_names->next->next) {
		fprintf(stderr, "Found more than one input interface."
				"This is currently unsupported\n");
		return EINVAL;
	}
	if (debug)
		fprintf(stderr, "Input sysfs node is %s\n", input_names->name);

	event_names = seek_sysfs_dir(input_names->name, event);
	free_names(input_names);
	if (!event_names) {
		free_names(event_names);
		return EINVAL;
	}
	if (event_names->next->next) {
		free_names(event_names);
		fprintf(stderr, "Found more than one event interface."
				"This is currently unsupported\n");
		return EINVAL;
	}
	if (debug)
		fprintf(stderr, "Event sysfs node is %s\n", event_names->name);

	uevent = read_sysfs_uevents(event_names->name);
	free_names(event_names);
	if (!uevent)
		return EINVAL;

	while (uevent->next) {
		if (!strcmp(uevent->key, "DEVNAME")) {
			rc_dev->input_name = malloc(strlen(uevent->value) + strlen(DEV) + 1);
			strcpy(rc_dev->input_name, DEV);
			strcat(rc_dev->input_name, uevent->value);
			break;
		}
		uevent = uevent->next;
	}
	free_uevent(uevent);

	if (!rc_dev->input_name) {
		fprintf(stderr, "Input device name not found.\n");
		return EINVAL;
	}

	uevent = read_sysfs_uevents(rc_dev->sysfs_name);
	if (!uevent)
		return EINVAL;
	while (uevent->next) {
		if (!strcmp(uevent->key, "DRV_NAME")) {
			rc_dev->drv_name = malloc(strlen(uevent->value) + 1);
			strcpy(rc_dev->drv_name, uevent->value);
		}
		if (!strcmp(uevent->key, "NAME")) {
			rc_dev->keytable_name = malloc(strlen(uevent->value) + 1);
			strcpy(rc_dev->keytable_name, uevent->value);
		}
		uevent = uevent->next;
	}
	free_uevent(uevent);

	if (debug)
		fprintf(stderr, "input device is %s\n", rc_dev->input_name);

	sysfs++;

	return 0;
}

static void clear_table(int fd)
{
	int i, j;
	int codes[2];

	/* Clears old table */
	for (j = 0; j < 256; j++) {
		for (i = 0; i < 256; i++) {
			codes[0] = (j << 8) | i;
			codes[1] = KEY_RESERVED;
			ioctl(fd, EVIOCSKEYCODE, codes);
		}
	}
}

static int add_keys(int fd)
{
	int write_cnt = 0;

	nextkey = &keys;
	while (nextkey->next) {
		struct keytable *old;

		write_cnt++;
		if (debug)
			fprintf(stderr, "\t%04x=%04x\n",
			       nextkey->codes[0], nextkey->codes[1]);

		if (ioctl(fd, EVIOCSKEYCODE, nextkey->codes)) {
			fprintf(stderr,
				"Setting scancode 0x%04x with 0x%04x via ",
				nextkey->codes[0], nextkey->codes[1]);
			perror("EVIOCSKEYCODE");
		}
		old = nextkey;
		nextkey = nextkey->next;
		if (old != &keys)
			free(old);
	}

	return write_cnt;
}

static void display_table(int fd)
{
	unsigned int i, j;

	for (j = 0; j < 256; j++) {
		for (i = 0; i < 256; i++) {
			int codes[2];

			codes[0] = (j << 8) | i;
			if (!ioctl(fd, EVIOCGKEYCODE, codes) && codes[1] != KEY_RESERVED)
				prtcode(codes);
		}
	}
}

int main(int argc, char *argv[])
{
	int dev_from_class = 0, write_cnt;
	int fd;
	static struct sysfs_names *names;
	struct rc_device	  rc_dev;

	argp_parse(&argp, argc, argv, 0, 0, 0);

	/* Just list all devices */
	if (!clear && !read && !keys.next) {
		static struct sysfs_names *names, *cur;

		names = find_device(NULL);
		for (cur = names; cur->next; cur = cur->next) {
			if (cur->name) {
				rc_dev.sysfs_name = cur->name;
				if (!get_attribs(&rc_dev))
					fprintf(stderr, "Kernel IR driver for %s (%s) is %s (using table %s)\n",
						rc_dev.sysfs_name,
						rc_dev.input_name,
						rc_dev.drv_name,
						rc_dev.keytable_name);


			}
		}
		return 0;
	}

	if (!devname) {
		names = find_device(devclass);
		if (!names)
			return -1;
		rc_dev.sysfs_name = names->name;
		names->name = NULL;
		free_names(names);

		if (get_attribs(&rc_dev))
			return -1;
		devname = rc_dev.input_name;

		dev_from_class++;
	}

	if (debug)
		fprintf(stderr, "Opening %s\n", devname);
	fd = open(devname, O_RDONLY);
	if (fd < 0) {
		perror(devname);
		return -1;
	}
	if (dev_from_class)
		free(devname);

	/*
	 * First step: clear, if --clear is specified
	 */
	if (clear) {
		clear_table(fd);
		fprintf(stderr, "Old keytable cleared\n");
	}

	/*
	 * Second step: stores key tables from file or from commandline
	 */
	write_cnt = add_keys(fd);
	if (write_cnt)
		fprintf(stderr, "Wrote %d keycode(s) to driver\n", write_cnt);

	/*
	 * Third step: display current keytable
	 */
	if (read)
		display_table(fd);

	return 0;
}