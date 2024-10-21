//
//  FileMapping.cpp
//  blind-nand-dumper
//
//  Created by tihmstar on 11.10.24.
//

#include "FileMapping.hpp"

#include <libgeneral/macros.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

FileMapping::FileMapping(const char *path, bool writeable, size_t fileSize)
: _mem{NULL}, _memSize{0}, _writeable(writeable)
{
    bool didInit = false;
    int fd = -1;
    cleanup([&]{
        safeClose(fd);
        if (!didInit){
            this->~FileMapping();
        }
    });
    struct stat st = {};
    
    if (writeable) {
        retassure((fd = open(path, O_RDWR | O_CREAT, 0644)), "Faile to open '%s' writeable",path);
    }else{
        retassure((fd = open(path, O_RDONLY)), "Faile to open '%s' readonly",path);
    }
    
    retassure(!fstat(fd, &st), "stat failed");
    _memSize = st.st_size;
    if (_memSize < fileSize) {
        retassure(lseek(fd, fileSize, SEEK_SET) == fileSize,"Failed to seek in file");
        retassure(write(fd, &_mem, 1) == 1, "Failed to write to file during file grow");
    }
    if (fileSize) _memSize = fileSize;
    
    if (writeable) {
        retassure((_mem = (uint8_t*)mmap(NULL, _memSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) != MAP_FAILED, "Failed to map file writeable! errno=%d (%s)",errno,strerror(errno));
    }else{
        retassure((_mem = (uint8_t*)mmap(NULL, _memSize, PROT_READ, MAP_PRIVATE, fd, 0)) != MAP_FAILED, "Failed to map file readonly! errno=%d (%s)",errno,strerror(errno));
    }
    
    didInit = true;
}

FileMapping::~FileMapping(){
    if (_mem){
        munmap(_mem, _memSize); _mem = NULL; _memSize = 0;
    }
}


