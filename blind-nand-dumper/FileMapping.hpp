//
//  FileMapping.hpp
//  blind-nand-dumper
//
//  Created by tihmstar on 11.10.24.
//

#ifndef FileMapping_hpp
#define FileMapping_hpp

#include <stdint.h>
#include <stdlib.h>

class FileMapping {
    uint8_t *_mem;
    size_t _memSize;
    bool _writeable;
public:
    FileMapping(const char *path, bool writeable = false, size_t fileSize = 0);
    ~FileMapping();
    
    inline bool isWriteable() const{return _writeable;}
    inline const uint8_t *mem() const{return _mem;}
    inline uint8_t *mem(){return _mem;}
    inline size_t memSize() const{return _memSize;}
};

#endif /* FileMapping_hpp */
