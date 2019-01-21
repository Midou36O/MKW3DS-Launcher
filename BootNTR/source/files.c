#include "main.h"
#include <errno.h>
#include <sys/stat.h>
#include <malloc.h>

extern char     *g_primary_error;

bool    fileExists(const char *path)
{
    int exists;

    if (!path) goto error;
    exists = access(path, F_OK);
    if (exists == 0)
        return (true);
error:
    return (false);
}

int     copy_file(char *old_filename, char  *new_filename)
{
    FILE        *old_file = NULL;
    FILE        *new_file = NULL;
	u8*          buffer = NULL;

    old_file = fopen(old_filename, "rb");
    new_file = fopen_mkdir(new_filename, "wb");
	u32 buffsize = 0x60000;
	buffer = memalign(0x1000, buffsize);
    if (!new_file || !old_file || !buffer)
    {
        if (old_file) fclose(old_file);
		if (new_file) fclose(new_file);
		if (buffer) free(buffer);
		return 1;
    }
	fseek(old_file, 0, SEEK_END);
	u32 totFile = ftell(old_file);
	fseek(old_file, 0, SEEK_SET);
	u32 currFile = 0;
    while (currFile < totFile) 
    {
		if (totFile - currFile < buffsize) buffsize = totFile - currFile;
		fread(buffer, 1, buffsize, old_file);
		fwrite(buffer, 1, buffsize, new_file);
		currFile += buffsize;
		fseek(old_file, currFile, SEEK_SET);
		fseek(new_file, currFile, SEEK_SET);
    }
    fclose(new_file);
    fclose(old_file);
	free(buffer);
    return  (0);
}

int     createDir(const char *path)
{
    u32     len = strlen(path);
    char    _path[0x100];
    char    *p;

    errno = 0;
    if (len > 0x100) goto error;
    strcpy(_path, path);
    for (p = _path + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';
            if (mkdir(_path, 777) != 0)
                if (errno != EEXIST) goto error;
            *p = '/';
        }
    }
    if (mkdir(_path, 777) != 0)
        if (errno != EEXIST) goto error;
    return (0);
error:
    return (-1);
}
