//
//  PicoNandReader.hpp
//  blind-nand-dumper
//
//  Created by tihmstar on 07.10.24.
//

#ifndef PicoNandReader_hpp
#define PicoNandReader_hpp

#include "PNR-proto.h"

#include <libgeneral/macros.h>
#include <libgeneral/Mem.hpp>

#include <libusb.h>

#include <stdio.h>

class PicoNandReader {
    /*
        chunk   - current data chunk read from device
        bufSize - current chunk size
        userArg - custom user arg
     
        return - true=continue dumping, false=stop dumping
     */
    using f_dumpCB = std::function<bool(const void *chunk, size_t chunkSize, void *userArg)>;
private:
    libusb_context *_ctx;
    libusb_device_handle *_dev;
    t_ChipProtocol _chipProto;
    
    void sendReaderCommand(t_ReaderCommand cmd, const void *data, size_t dataSize);
    
public:
    PicoNandReader();
    ~PicoNandReader();
    
    void connectReader();
    void disconnectReader();

    void selectProtocol(t_ChipProtocol proto);
    void resetChip();
    
#pragma mark NAND commands
    void sendNandCommand(uint8_t CE, const void *cmd, size_t cmdLen, const void *addr, size_t addrLen, const void *data, size_t dataLen, void *rsp, size_t rspSize, bool isMultiCommand = false);
    uint64_t readChipIDForCE(uint8_t CE);
    
    tihmstar::Mem readPage(uint8_t CE, uint32_t pageAddress, uint16_t pageSize);
    
    void dumpPages(uint8_t CE, uint32_t pageAddress, uint16_t pageSize, uint32_t numPages, f_dumpCB cbFunc, void *cbArg);
};

#endif /* PicoNandReader_hpp */
