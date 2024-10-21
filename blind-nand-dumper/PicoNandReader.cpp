//
//  PicoNandReader.cpp
//  blind-nand-dumper
//
//  Created by tihmstar on 07.10.24.
//

#include "PicoNandReader.hpp"

#include <libgeneral/macros.h>
#include <libgeneral/Mem.hpp>

#include <arpa/inet.h>

#define USB_VID 0x6874
#define USB_PID 0x7064

#define USB_TIMEOUT 10000

#define USB_MAX_TRANSFER_SIZE 0x1000

#ifndef MIN
#define MIN(a, b) ((b)>(a)?(a):(b))
#endif

#pragma mark PicoNandReader
PicoNandReader::PicoNandReader()
: _ctx{NULL}, _dev{NULL}
, _chipProto{kChipProtocolUndefined}
{
    bool didInit = false;
    cleanup([&]{
        if (!didInit) this->~PicoNandReader();
    });
    retassure(!libusb_init(&_ctx), "Failed to init libusb");
        
    didInit = true;
}

PicoNandReader::~PicoNandReader(){
    disconnectReader();
    safeFreeCustom(_ctx, libusb_exit);
}

#pragma mark private
void PicoNandReader::sendReaderCommand(t_ReaderCommand cmd, const void *data, size_t dataSize){
    reterror("TODO");
}


#pragma mark public
void PicoNandReader::disconnectReader(){
    if (_dev) libusb_release_interface(_dev, 0);
    safeFreeCustom(_dev, libusb_close);
}

void PicoNandReader::connectReader(){
    int err = 0;
    if (!_dev) {
        retassure(_dev = libusb_open_device_with_vid_pid(_ctx, USB_VID, USB_PID),"Failed to open PNR device");
        retassure((err = libusb_claim_interface(_dev, 0)) == 0,"Failed to configure device with err=%d",err);
    }
}

void PicoNandReader::selectProtocol(t_ChipProtocol proto){
    t_ReaderResponse rrsp = kReaderResponseUndefined;
    int err = 0;
    retassure((err = libusb_control_transfer(_dev, LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR, kReaderCommandSelectProtocol, 0, proto, (unsigned char *)&rrsp, sizeof(rrsp), USB_TIMEOUT)) == sizeof(rrsp),"faild to receive rrsp with err=%d",err);
    retassure(rrsp == kReaderResponseSuccess, "selectProtocol failed with err=%d",rrsp);
}

void PicoNandReader::resetChip(){
    t_ReaderResponse rrsp = kReaderResponseUndefined;
    int err = 0;
    retassure((err = libusb_control_transfer(_dev, LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR, kReaderCommandResetChip, 0, 0, (unsigned char *)&rrsp, sizeof(rrsp), USB_TIMEOUT)) == sizeof(rrsp),"faild to receive rrrsp with err=%d",err);
    retassure(rrsp == kReaderResponseSuccess, "resetChip failed with err=%d",rrsp);
}

#pragma mark NAND commands
void PicoNandReader::sendNandCommand(uint8_t CE, const void *cmd, size_t cmdLen, const void *addr, size_t addrLen, const void *data, size_t dataLen, void *rsp_, size_t rspSize, bool isMultiCommand){
    int err = 0;
    
    uint8_t *rsp = (uint8_t*)rsp_;
    
    tihmstar::Mem commandbuf;
    commandbuf.append(&CE, 1);
    commandbuf.append(&cmdLen, 2);
    commandbuf.append(cmd, cmdLen & 0xffff);
    commandbuf.append(&addrLen, 2);
    commandbuf.append(addr, addrLen & 0xffff);
    commandbuf.append(&dataLen, 2);
    commandbuf.append(data, dataLen & 0xffff);
    
    retassure((err = libusb_control_transfer(_dev, LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR, kReaderCommandChipCommandSend, (uint16_t)rspSize, isMultiCommand, (unsigned char *)commandbuf.data(), commandbuf.size(), USB_TIMEOUT)) == commandbuf.size(),"faild to send command data with err=%d",err);
    while (rspSize){
        size_t actualSize = MIN(rspSize,USB_MAX_TRANSFER_SIZE);
        retassure((err = libusb_control_transfer(_dev, LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR, kReaderCommandChipCommandReceive, 0, 0, (unsigned char *)rsp, actualSize, USB_TIMEOUT)) == actualSize,"faild to receive command data with err=%d",err);
        rspSize -= actualSize;
        rsp += actualSize;
    }
}

uint64_t PicoNandReader::readChipIDForCE(uint8_t CE){
    uint64_t ret = 0;
    
    uint8_t cmd = 0x90;
    uint8_t addr = 0x00;
    
    sendNandCommand(CE, &cmd, sizeof(cmd), &addr, sizeof(addr), NULL, 0, &ret, sizeof(ret));
    return ret;
}

tihmstar::Mem PicoNandReader::readPage(uint8_t CE, uint32_t pageAddress, uint16_t pageSize){
    tihmstar::Mem ret;
    uint64_t addr = pageAddress << 16;
    uint8_t cmd1 = 0x00;
    uint8_t cmd2 = 0x30;


    ret.resize(pageSize);
    
    sendNandCommand(CE, &cmd1, sizeof(cmd1), &addr, 5, NULL, 0, NULL, 0, true);
    sendNandCommand(CE, &cmd2, sizeof(cmd2), NULL, 0, NULL, 0, ret.data(), ret.size());

    return ret;
}

void PicoNandReader::dumpPages(uint8_t CE, uint32_t pageAddress, uint16_t pageSize, uint32_t numPages, f_dumpCB cbFunc, void *cbArg){
    int err = 0;
    uint8_t chunk[0x1000] = {};
    
    tihmstar::Mem commandbuf;
    commandbuf.append(&pageAddress, sizeof(pageAddress));
    commandbuf.append(&numPages, sizeof(numPages));
    commandbuf.append(&pageSize, sizeof(pageSize));
    commandbuf.append(&CE, sizeof(CE));
    retassure((err = libusb_control_transfer(_dev, LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR, kReaderCommandChipReadPages, 0, 0, (unsigned char *)commandbuf.data(), commandbuf.size(), USB_TIMEOUT)) == commandbuf.size(),"faild to send command data with err=%d",err);

    uint64_t fullSize = pageSize*numPages;
    while (fullSize) {
        uint64_t chunkSize = MIN(sizeof(chunk),fullSize);
        int actualLen = 0;
        retassure((err = libusb_interrupt_transfer(_dev, LIBUSB_ENDPOINT_IN | 1, chunk, (int)chunkSize, &actualLen, USB_TIMEOUT)) == 0, "Failed to read page data");
        retassure(actualLen > 0, "Failed to read a single byte");
        if (!cbFunc(chunk, actualLen, cbArg)) break;
        fullSize -= actualLen;
    }
}
