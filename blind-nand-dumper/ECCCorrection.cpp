//
//  ECCCorrection.cpp
//  blind-nand-dumper
//
//  Created by tihmstar on 10.10.24.
//

#include "ECCCorrection.hpp"

#include "external/linux_bch.h"

#include <libgeneral/macros.h>
#include <libgeneral/DeliveryEvent.hpp>

#include <thread>
#include <algorithm>

#include <sys/mman.h>

//#define WITH_MADVICE

inline void invertblock(void *blk_, size_t blocksize){
    uint8_t *blk = (uint8_t *)blk_;
#define blk2 ((uint16_t *)blk)
#define blk4 ((uint32_t *)blk)
#define blk8 ((uint64_t *)blk)

    if ((((uint64_t)blk) & 7) == 0) {
        while (blocksize >= 8) {
            *blk8 ^= 0xffffffffffffffff;
            blk += 8;
            blocksize -= 8;
        }
    }
    if ((((uint64_t)blk) & 3) == 0) {
        while (blocksize >= 4) {
            *blk4 ^= 0xFFFFFFFF;
            blk += 4;
            blocksize -= 4;
        }
    }
    if ((((uint64_t)blk) & 1) == 0) {
        while (blocksize >= 2) {
            *blk2 ^= 0xFFFF;
            blk += 2;
            blocksize -= 2;
        }
    }
    while (blocksize > 0) {
        *blk++ ^= 0xFF;
        blocksize--;
    }
}

int ECCCorrection::eccBCH(void *codeword, size_t codewordSize, void *eccdata, size_t eccdataSize, uint32_t poly, bool swap_bits, bool invert){
    struct bch_control *bch = NULL;
    cleanup([&]{
        safeFreeCustom(bch, bch_free);
    });
    int ret = 0;
    
    assure(bch = bch_init(0, 0, poly, swap_bits, (int)eccdataSize));
    
    if (invert) {
        invertblock(codeword, codewordSize);
        invertblock(eccdata, eccdataSize);
    }
        
    ret = bch_decode(bch, (uint8_t *)codeword, (unsigned int)codewordSize, (uint8_t *)eccdata, NULL, NULL);
        
    if (invert) {
        invertblock(codeword, codewordSize);
        invertblock(eccdata, eccdataSize);
    }
    
    return ret;
}

struct InternalPageStructure{
public:
    ECCCorrection::PageStructure ps;
    uint32_t tagmin;
    uint32_t tagmax;
    uint32_t startPage;
    uint32_t pagesCnt;

    InternalPageStructure() : tagmin(-1), tagmax(0), startPage(0), pagesCnt(0){}
};

uint32_t ECCCorrection::processPages(const FileMapping *inmap, FileMapping *outmap, size_t pageSize, NandStructure nstructure, cbCodeWord cb, void *userarg, uint32_t threadsCnt){
    const uint8_t *mem = NULL;
    size_t memSize = 0;
    
    uint8_t *outMem = NULL;
    size_t outMemSize = 0;
        
    mem = inmap->mem();
    memSize = inmap->memSize();
    
    if (threadsCnt == 0) {
        threadsCnt = 1;
    }
    
    if (outmap) {
        outMem = outmap->mem();
        outMemSize = outmap->memSize();
        retassure(outMemSize == memSize, "outMemSize != memSize");
    }
    
    std::vector<InternalPageStructure> ips;

    for (auto ps : nstructure){
        InternalPageStructure ip;
        for (auto cw : ps.pageStructure){
            if (cw.tag < ip.tagmin) {
                ip.tagmin = cw.tag;
            }
            if (cw.tag > ip.tagmax) {
                ip.tagmax = cw.tag;
            }
        }
        retassure(ip.tagmax != (uint32_t)-1, "max page structure tag too large!");
        ip.ps = ps.pageStructure;
        ip.startPage = ps.startPage;
        ip.pagesCnt = ps.pagesCnt;
        ips.push_back(ip);
    }
    
    std::atomic<uint32_t> processedPages = 0;
    auto processPageFunc =  [mem, memSize, outMem, outMemSize, pageSize, cb, userarg, &processedPages]
                        (InternalPageStructure ips, size_t memOffset)->bool{
        //process page
        const uint8_t *curPage = &mem[memOffset];
        uint8_t *curOutPage = &outMem[memOffset]; //may be invalid!
        
        uint32_t pagenum = (uint32_t)(memOffset / pageSize);
        if ((pagenum & 0xffff) == 0) {
            info("Processing page 0x%08x",pagenum);
        }
        uint32_t cwnum = 0;
        for (uint32_t tag = ips.tagmin; tag <= ips.tagmax; tag++,cwnum++) {
            //process codewords in page
            
            size_t cwStart = 0;
            size_t cwSize = 0;
            size_t eccStart = 0;
            size_t eccSize = 0;
            size_t pageOffset = 0;
            
            for (auto cw : ips.ps) {
                if (cw.tag == tag) {
                    if (cw.type == kPageCodewordTypeECC) {
                        if (!eccSize) {
                            eccSize = cw.len;
                            eccStart = pageOffset;
                        }else{
                            reterror("Multiple ECC definitions for codeword!");
                        }
                    }else{
                        if (!cwSize) {
                            cwSize = cw.len;
                            cwStart = pageOffset;
                        }else{
                            if (cwStart + cwSize == pageOffset) {
                                cwSize += cw.len;
                            }else{
                                reterror("Ecc correction not supported for codewords with holes");
                            }
                        }
                    }
                }
                pageOffset += cw.len;
            }
            retassure(cwSize, "Failed to find codeword with tag '%d'",tag);
            retassure(eccSize, "Failed to find ecc with tag '%d'",tag);

            retassure(memOffset + cwStart + cwSize < memSize, "codeword goes out of memory bounds");
            retassure(memOffset + eccStart + eccSize < memSize, "ecc goes out of memory bounds");

            if (outMemSize) {
                cb(pagenum, cwnum, &curPage[cwStart], cwSize, &curPage[eccStart], eccSize, &curOutPage[cwStart], &curOutPage[eccStart], userarg);
            }else{
                cb(pagenum, cwnum, &curPage[cwStart], cwSize, &curPage[eccStart], eccSize, NULL, NULL, userarg);
            }
        }
        ++processedPages;
        return true;
    };
    
    tihmstar::DeliveryEvent<std::pair<InternalPageStructure, size_t>> workerChunks;
    
    std::vector<std::thread> wthreads;
    
    debug("Starting %d threads",threadsCnt);
    for (uint32_t i=0; i<threadsCnt; i++) {
        wthreads.push_back(std::thread([&workerChunks,&processPageFunc](int tid){
            debug("[%d] Starting thread",tid);
            while (true) {
                std::pair<InternalPageStructure, size_t> wchunk = {};
                try {
                    wchunk = workerChunks.wait();
                } catch (tihmstar::exception &e) {
                    break;
                }
                processPageFunc(wchunk.first,wchunk.second);
            }
            debug("[%d] Stopping thread",tid);
        },i));
    }
    
    if (ips.size()) {
        std::reverse(ips.begin(), ips.end());
        InternalPageStructure curIPS = ips.back(); ips.pop_back();
        uint32_t processedIPS = 0;
        {
            uint32_t printEndPage = (curIPS.pagesCnt) ? curIPS.startPage+curIPS.pagesCnt : 0;
            info("[%d] Processing pagestructure from page 0x%08x (%10d) until page 0x%08x (%10d)",processedIPS,curIPS.startPage,curIPS.startPage,printEndPage,printEndPage);
#ifdef WITH_MADVICE
            if (curIPS.pagesCnt) {
                madvise((void*)&mem[pageSize*curIPS.startPage], pageSize*curIPS.pagesCnt, MADV_SEQUENTIAL);
            }else{
                madvise((void*)&mem[pageSize*curIPS.startPage], memSize-pageSize*curIPS.startPage, MADV_SEQUENTIAL);
            }
#endif
        }
        
        for (size_t memOffset = curIPS.startPage*pageSize; memOffset < memSize; memOffset+=pageSize) {
#define curPage (memOffset/pageSize)
            if (curIPS.pagesCnt && curPage >= curIPS.startPage + curIPS.pagesCnt) {
                while (curIPS.pagesCnt && curPage >= curIPS.startPage + curIPS.pagesCnt) {
                    if (!ips.size()) goto endPostingWork;
                    /*
                        move to next page structure
                     */
                    ++processedIPS;
                    curIPS = ips.back(); ips.pop_back();
                    if (curIPS.startPage > curPage) {
                        memOffset = curIPS.startPage*pageSize;
                    }
                }
                {
                    uint32_t printEndPage = (curIPS.pagesCnt) ? curIPS.startPage+curIPS.pagesCnt : 0;
                    info("[%d] Processing pagestructure from page 0x%08x (%10d) until page 0x%08x (%10d)",processedIPS,curIPS.startPage,curIPS.startPage,printEndPage,printEndPage);
#ifdef WITH_MADVICE
                    if (curIPS.pagesCnt) {
                        madvise((void*)&mem[pageSize*curIPS.startPage], pageSize*curIPS.pagesCnt, MADV_SEQUENTIAL);
                    }else{
                        madvise((void*)&mem[pageSize*curIPS.startPage], memSize-pageSize*curIPS.startPage, MADV_SEQUENTIAL);
                    }
#endif
                }
            }

            workerChunks.post({curIPS,memOffset});
        }
        endPostingWork:;
#undef curPage
    }
    workerChunks.finish();
    
    debug("waiting for threads to finish");
    for (auto &t : wthreads) {
        t.join();
    }
    
    debug("all threads finished");

error:
    return processedPages;
}
