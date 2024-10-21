//
//  ECCCorrection.hpp
//  blind-nand-dumper
//
//  Created by tihmstar on 10.10.24.
//

#ifndef ECCCorrection_hpp
#define ECCCorrection_hpp

#include "FileMapping.hpp"

#include <functional>
#include <vector>

#include <stdlib.h>

namespace ECCCorrection {
enum PageCodewordType{
    kPageCodewordTypeUndefined = 0,
    kPageCodewordTypeData,
    kPageCodewordTypeECC,
    kPageCodewordTypeServiceArea,
};
struct PageCodeword{
    uint32_t tag;
    uint32_t len;
    PageCodewordType type;
};

using PageStructure = std::vector<PageCodeword>;

struct NandSection{
    PageStructure pageStructure;
    uint32_t startPage;
    uint32_t pagesCnt;
};

using NandStructure = std::vector<NandSection>;
using cbCodeWord = std::function<void(uint32_t pagenum, uint32_t cwnum, const uint8_t *codeword, size_t codewordSize, const uint8_t *eccdata, size_t eccdataSize, uint8_t *outCodeword, uint8_t *outECC, void *userarg)>;


int eccBCH(void *codeword, size_t codewordSize, void *eccdata, size_t eccdataSize, uint32_t poly, bool swap_bits=false, bool invert=false);


uint32_t processPages(const FileMapping *inmap, FileMapping *outmap, size_t pageSize, NandStructure nstructure, cbCodeWord cb, void *userarg = NULL, uint32_t threadsCnt = 0);

}

#endif /* ECCCorrection_hpp */
