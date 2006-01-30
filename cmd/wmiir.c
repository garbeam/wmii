/*
 * (C)opyright MMIV-MMVI Anselm R. Garbe <garbeam at gmail dot com>
 * See LICENSE file for license details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

#include "ixp.h"

#include <cext.h>

static IXPClient c = { 0 };

static char *version[] = {
    "wmiir - window manager improved remote - " VERSION "\n"
        " (C)opyright MMIV-MMVI Anselm R. Garbe\n", 0
};

static void
usage()
{
    fprintf(stderr, "%s",
            "usage: wmiir [-a <server address>] [-v] <command>\n"
            "      -a    server address (default: $WMIIR_ADDRESS)\n"
            "      -v    version info\n"
            "valid commands:\n"
            "      create <file>      -- creates file and writes data from stdin to file\n"
            "      read   <file/dir>  -- prints file/directory contents\n"
            "      write  <file>      -- writes data from stdin to file\n"
            "      remove <file>      -- removes file\n");
    exit(1);
}

static void
write_data(unsigned int fid)
{
	void *data = cext_emallocz(c.fcall.iounit);
	unsigned long long offset = 0;
	size_t len = 0;

	while((len = read(0, data, c.fcall.iounit)) > 0) {
		fprintf(stderr, "read=%d, bufsz=%d\n", len, c.fcall.iounit);
        if(ixp_client_write
           (&c, fid, offset, len, data) != len) {
            fprintf(stderr, "wmiir: cannot write file: %s\n", c.errstr);
            break;
        }
		offset += len;
    }
	free(data);
}

static int
xcreate(char *file)
{
    unsigned int fid;
    char *p = strrchr(file, '/');

    fid = c.root_fid << 2;
    /* walk to bottom-most directory */
    *p = 0;
    if(ixp_client_walk(&c, fid, file) == -1) {
        fprintf(stderr, "wmiir: cannot walk to '%s': %s\n", file, c.errstr);
        return -1;
    }
    /* create */
    p++;
    if(ixp_client_create(&c, fid, p, (unsigned int) 0xff, IXP_OWRITE) == -1) {
        fprintf(stderr, "wmiir: cannot create file '%s': %s\n", p, c.errstr);
        return -1;
    }
    write_data(fid);
    return ixp_client_close(&c, fid);
}

static int
xwrite(char *file)
{
	
    /* open */
    unsigned int fid = c.root_fid << 2;
    if(ixp_client_open(&c, fid, file, IXP_OWRITE) == -1) {
        fprintf(stderr, "wmiir: cannot open file '%s': %s\n", file, c.errstr);
        return -1;
    }
    write_data(fid);
    return ixp_client_close(&c, fid);
}

static int
comp(const void *s1, const void *s2)
{
	return strcmp(*(char **)s1, *(char **)s2);
}

static void
xls(void *result, unsigned int msize)
{
	size_t n = 0, j = 0;
	char buf[IXP_MAX_FLEN];
    void *p = result;
	char **dir;
    static Stat stat, zerostat = { 0 };
    do {
        p = ixp_dec_stat(p, &stat);
		n++;
    }
    while(p - result < msize);
	dir = (char **)cext_emallocz(sizeof(char *) * n);
	p = result;
	do {
        p = ixp_dec_stat(p, &stat);
        if(stat.qid.type == IXP_QTDIR)
			snprintf(buf, sizeof(buf), "%s/", stat.name);
        else
			snprintf(buf, sizeof(buf), "%s", stat.name);
		dir[j++] = cext_estrdup(buf);
        stat = zerostat;
    }
    while(p - result < msize);
	qsort(dir, n, sizeof(char *), comp);
	for(j = 0; j < n; j++) {
        fprintf(stdout, "%s\n", dir[j]);
		free(dir[j]);
	}
	free(dir);
}

static int
xread(char *file)
{
    unsigned int fid = c.root_fid << 2;
    int count, is_directory = 0;
    static unsigned char result[IXP_MAX_MSG];
	unsigned long long offset = 0;

    /* open */
    if(ixp_client_open(&c, fid, file, IXP_OREAD) == -1) {
        fprintf(stderr, "wmiir: cannot open file '%s': %s\n", file, c.errstr);
        return -1;
    }
    is_directory = !c.fcall.nwqid || (c.fcall.qid.type == IXP_QTDIR);

    /* read */
	while((count = ixp_client_read(&c, fid, offset, result, IXP_MAX_MSG)) > 0) {
		if(is_directory)
			xls(result, count);
		else {
			unsigned int i;
			for(i = 0; i < count; i++)
				fputc(result[i], stdout);
			fflush(stdout);
		}
		offset += count;
	}
    if(count == -1) {
        fprintf(stderr, "wmiir: cannot read file/directory '%s': %s\n", file, c.errstr);
        return -1;
    }
    return ixp_client_close(&c, fid);
}

static int
xremove(char *file)
{
    unsigned int fid;

    /* remove */
    fid = c.root_fid << 2;
    if(ixp_client_remove(&c, fid, file) == -1) {
        fprintf(stderr, "wmiir: cannot remove file '%s': %s\n", file, c.errstr);
        return -1;
    }
    return 0;
}

int
main(int argc, char *argv[])
{
    int i = 0;
    char *cmd, *file, *sockfile = getenv("WMIIR_ADDRESS");

    /* command line args */
	if(argc < 2)
		usage();

	for(i = 1; (i < argc) && (argv[i][0] == '-'); i++) {
		switch (argv[i][1]) {
			case 'v':
				fprintf(stderr, "%s", version[0]);
				exit(0);
				break;
			case 'a':
				if(i + 1 < argc)
					sockfile = argv[++i];
				else
					usage();
				break;
			default:
				usage();
				break;
		}
	}
	cmd = argv[argc - 2];
	file = argv[argc - 1];

    if(!sockfile) {
        fprintf(stderr, "%s", "wmiir: error: $WMIIR_ADDRESS not set\n");
        usage();
    }
    /* open socket */
    if(ixp_client_init(&c, sockfile) == -1) {
        fprintf(stderr, "wmiir: %s\n", c.errstr);
        exit(1);
    }

	if(!strncmp(cmd, "create", 7))
		xcreate(file);
	else if(!strncmp(cmd, "write", 6))
		xwrite(file);
	else if(!strncmp(cmd, "read", 5))
		xread(file);
	else if(!strncmp(cmd, "remove", 7))
		xremove(file);
	else
		usage();

    /* close socket */
    ixp_client_deinit(&c);

    return 0;
}
