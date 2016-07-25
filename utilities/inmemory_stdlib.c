struct MemFile {
    int fd;
    // if fd == -1
    char * buffer;
    size_t size;
    size_t allocated;
    size_t offset;
    char filename[128];
};

typedef struct MemFile mem_FILE;

void set_filename(mem_FILE*fp, const char *name) {
    size_t len = strlen(name) + 1;
    memcpy(fp->filename, name, len > sizeof(fp->filename) ? sizeof(fp->filename) : len);
    fp->filename[sizeof(fp->filename) - 1] = '\0';
}

mem_FILE *mem_make_inmemory_buffer(const char *name) {
    mem_FILE * retval = (mem_FILE*)calloc(1, sizeof(mem_FILE));
    retval->fd = -1;
    retval->allocated = 4096;
    retval->buffer = malloc(retval->allocated);
    retval->offset = 0;
    set_filename(retval, name);
    return retval;
    
}
mem_FILE*mem_fopen(const char *name, const char *mode);
int AcquireUniqueFileResource(char *path) {
    char xpath[3] = {255, 0, 0};
    static char counter = 1;
    xpath[1] = counter++;
    mem_FILE * fp = mem_fopen(xpath, "w+");
    memcpy(path, xpath, sizeof(xpath));
    return fileno((FILE*)fp);
}


mem_FILE *mem_fdopen(int fd, const char *mode) {
    char fn[1024];
    sprintf(fn, "fd:%d", fd);
    if (strchr(mode, 'w')) {
        return mem_make_inmemory_buffer(fn);
    }
    mem_FILE * retval = (mem_FILE*)calloc(1, sizeof(mem_FILE));
    retval->fd = fd;
    set_filename(retval, fn);
    return retval;
}
size_t mem_fread_inner(void *buf, size_t membsize, size_t nmemb, mem_FILE*stream) {
    if (stream->fd == -1) {
        size_t to_copy = membsize * nmemb;
        if (to_copy > stream->size - stream->offset) {
            to_copy = ((stream->size - stream->offset) / membsize) * membsize;
        }
        if (to_copy) {
            memcpy(buf, stream->buffer + stream->offset, to_copy);
            stream->offset += to_copy;
        }
        return to_copy;
    }
    if (membsize != 1) {
//        abort(); //unsupported -- but we'll let it slid
    }
    size_t to_read = membsize * nmemb;
    size_t read_so_far = 0;
    char * cur_buf = (char*)buf;
    while(read_so_far < to_read) {
        ssize_t ret = read(stream->fd, cur_buf, to_read);
        if (ret < 0){
            if (errno == EINTR) {
                continue;
            }
            return read_so_far / membsize;
        } else if (ret == 0) {
            return read_so_far / membsize;
        } else {
            stream->offset += ret;
            read_so_far += ret;
            cur_buf += ret;
        }
    }
    return read_so_far / membsize;
}
size_t mem_fread(void *buf, size_t membsize, size_t nmemb, mem_FILE*stream) {
    return mem_fread_inner(buf, membsize, nmemb, stream);
}

size_t mem_fwrite(const void *buf, size_t membsize, size_t nmemb, mem_FILE*stream) {
    if (stream->fd == -1) {
        size_t to_copy = membsize * nmemb;
        while (to_copy > stream->allocated - stream->offset) {
            stream->allocated *= 2;
            stream->buffer = realloc(stream->buffer, stream->allocated);
        }
        if (to_copy) {
            memcpy(stream->buffer + stream->offset, buf, to_copy);
            stream->offset += to_copy;
        }
        return to_copy;
    }
    if (membsize != 1) {
//        abort(); //unsupported
    }
    size_t to_write = membsize * nmemb;
    size_t written_so_far = 0;
    char * cur_buf = (char*)buf;
    while(written_so_far < to_write) {
        ssize_t ret = write(stream->fd, cur_buf, to_write);
        if (ret < 0){
            if (errno == EINTR) {
                continue;
            }
            return written_so_far / membsize;
        } else if (ret == 0) {
            return written_so_far / membsize;
        } else {
            written_so_far += ret;
            cur_buf += ret;
        }
    }
    return written_so_far / membsize;
}
void crystallize(mem_FILE*fp) {
    // this reads a file into memorye
    if (fp->fd != -1) {
        fp->offset = 0;
        fp->allocated = 2048;
        fp->size = 0;
        char * buffer = NULL;
        while (1) {
            fp->allocated *= 2;
            buffer = realloc(buffer, fp->allocated);
            size_t read = mem_fread_inner(buffer, 1, fp->allocated - fp->size, fp);
            fp->size += read;
            if (read == 0) {
                break;
            }
        }
        fp->buffer = buffer;
        fp->fd = -1;
    }
}
int mem_fstat(mem_FILE*stream, struct stat*st) {
    crystallize(stream);
    
    st->st_dev = 0;
    st->st_ino = stream->fd;
    st->st_mode = 0777;
    st->st_nlink = 1;
    st->st_uid = 0;
    st->st_gid = 0;
    st->st_rdev = 0;
    st->st_size = stream->size;
    st->st_blksize = (stream->size + 511) / 512;
    st->st_atime = 1400000000;
    st->st_mtime = 1400000000;
    st->st_ctime = 1400000000;
    return 0;
}



off_t mem_ftell(mem_FILE*stream) {
    return stream->offset;
}
int mem_fseek(mem_FILE*stream, off_t offset,  int whence) {
    if(stream->offset ==0) {
        if (stream->fd != -1) {
            crystallize(stream);
        }
    } else if (stream->fd != -1) {
        return -1; // failure
    }
    if (whence == SEEK_CUR) {
        stream->offset += offset;
    } else if (whence == SEEK_END) {
        stream->offset = stream->size + offset;
    } else if (whence == SEEK_SET) {
        stream->offset = offset;
    }
    return 0;
}

FILE *fopen(const char *name, const char *mode) {
    return (FILE*)mem_fopen(name, mode);
}
FILE *fdopen(int fd, const char *mode) {
    return (FILE*)mem_fdopen(fd, mode);
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    return mem_fread(ptr, size, nmemb, (mem_FILE*)stream);
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb,
              FILE *stream) {
    return mem_fwrite(ptr, size, nmemb, (mem_FILE*)stream);
}
int fseek(FILE*stream, long offset,  int whence) {
    return mem_fseek((mem_FILE*)stream, offset, whence);
}
int fseeko(FILE*stream, off_t offset,  int whence) {
    return mem_fseek((mem_FILE*)stream, offset, whence);
}
int fflush(FILE*noop) {
    return 0;
}
long ftell(FILE*dat) {
    return mem_ftell((mem_FILE*)dat);
}
off_t ftello(FILE*dat) {
    return mem_ftell((mem_FILE*)dat);
}
int ferror(FILE*noop) {
    return 0;
}
int setvbuf(FILE* noop, char*buffer, int mode, size_t size) {
    return -1;
}
int fclose(FILE*fp) {
    return 0;
}

struct FileBackpointer {
    mem_FILE * fp;
    int fd;
};
struct FileBackpointer backpointers[128];
int fileno(FILE *fp) {
    mem_FILE * local_fp = (mem_FILE*)fp;
    if (local_fp->fd != -1) {
        return local_fp->fd;
    }
    static int new_fd = 1024;
    for (size_t i = 0; i < sizeof(backpointers) / sizeof(backpointers[0]); ++i) {
        if (backpointers[i].fp == NULL) {
            backpointers[i].fp = local_fp;
            if (local_fp->fd != -1) {
                backpointers[i].fd = local_fp->fd;
            } else {
                backpointers[i].fd = new_fd++;
            }
            return backpointers[i].fd;
        }
        if (backpointers[i].fp == local_fp) {
            return backpointers[i].fd;
        }
    }
    abort(); // not enough room;
}
int fstat(int fd, struct stat *buf) {
    for (size_t i = 0; i < sizeof(backpointers) / sizeof(backpointers[0]); ++i) {
        if (backpointers[i].fd == fd) {
            return mem_fstat(backpointers[i].fp, buf);
        }
    }    
    abort(); // bad fd
}

mem_FILE*mem_fopen(const char *name, const char *mode) {
    for (size_t i = 0; i < sizeof(backpointers) / sizeof(backpointers[0]); ++i) {
        if (backpointers[i].fp && strcmp(backpointers[i].fp->filename, name) == 0) {
            if (backpointers[i].fd == -1) {
                backpointers[i].fp->offset = 0;
                return backpointers[i].fp;
            } else {
                break;
            }
        }
    }
    mem_FILE * retval = (mem_FILE*)calloc(1, sizeof(mem_FILE));
    int flags = 0;
    if (strchr(mode, 'r')) {
        if (strchr(mode, '+')) {
            flags |= O_RDWR;
        } else {
            flags |= O_RDONLY;
        }
    }
    if (strchr(mode, 'w')) {
        if (strchr(mode, 'r')) {
            flags |= O_RDWR|O_CREAT;
        } else {
            flags |= O_WRONLY|O_CREAT;
        }

        mem_FILE*retval = mem_make_inmemory_buffer(name);
        if (fileno((FILE*)retval) == -1) {
            abort();
        }
        return retval;
    }
    int fd = open(name, flags);
    retval->fd = fd;
    set_filename(retval, name);
    return retval;
}

