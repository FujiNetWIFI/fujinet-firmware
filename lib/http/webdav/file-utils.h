#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>

static int rm_rf(const char *path) {
        struct stat sb;
        if (stat(path, &sb) < 0)
                return -errno;

        int ret = 0;

        if ((sb.st_mode & S_IFMT) == S_IFDIR) {
                DIR *dir = opendir(path);
                if (dir) {
                        struct dirent *de;
                        
                        while ((de = readdir(dir))) {
                                if (strcmp(de->d_name, ".") == 0 ||
                                    strcmp(de->d_name, "..") == 0)
                                        continue;

                                char *rpath;
                                if (asprintf(&rpath, "%s/%s", path, de->d_name) > 0) {
                                        rm_rf(rpath);
                                        free(rpath);
                                }
                        }

                        closedir(dir);
                }

                ret = rmdir(path);
        } else {
                ret = unlink(path);
        }

        return ret;
}

static int copy_file(const char *from, const char *to) {
        FILE *in = fopen(from, "r");
        if (!in)
                return -errno;

        FILE *out = fopen(to, "w+");
        if (!out) {
                fclose(in);
                return -errno;
        }

        const int chunkSize = 8192;
        char *chunk = (char *) malloc(chunkSize);

        for (;;) {
                size_t r = fread(chunk, 1, chunkSize, in);
                if (r == 0)
                        break;

                size_t w = fwrite(chunk, 1, r, out);
                if (w != r)
                        break;
        }

        free(chunk);

        int ret = 0;

        if (ferror(in) || ferror(out))
                ret = -errno;

        fclose(in);
        fclose(out);

        return ret;
}

static int copy_recursive(std::string source, std::string destination, int recurse, bool overwrite) {
        struct stat sourceStat;
        int ret = stat(source.c_str(), &sourceStat);
        if (ret < 0)
                return -errno;

        struct stat destinationStat;
        bool destinationExists = stat(destination.c_str(), &destinationStat) == 0;
        // bool destinationIsDir = destinationExists && ((destinationStat.st_mode & S_IFMT) == S_IFDIR);

        if (destinationExists && !overwrite)
                return -EEXIST;

        // if (destinationIsDir)

        switch (sourceStat.st_mode & S_IFMT) {
        case S_IFREG:
                if ((destinationStat.st_mode & S_IFMT) == S_IFDIR)
                        destination += std::string("/") + basename(source.c_str());

                return copy_file(source.c_str(), destination.c_str());

        case S_IFDIR:
                if (!destinationExists) {
                        ret = mkdir(destination.c_str(), 0755);
                        if (ret < 0)
                                return -errno;
                }

                int ret = 0;
                DIR *dir = opendir(source.c_str());
                if (dir) {
                        struct dirent *de;

                        while ((de = readdir(dir))) {
                                if (strcmp(de->d_name, ".") == 0 ||
                                    strcmp(de->d_name, "..") == 0)
                                        continue;

                                std::string rSource = source + std::string("/") + de->d_name;
                                std::string rDestination = destination + std::string("/") + de->d_name;
                                ret = copy_recursive(rSource, rDestination, recurse-1, overwrite);
                        }

                        closedir(dir);
                }

                return ret;
        }

        return -ENOTSUP;
}
