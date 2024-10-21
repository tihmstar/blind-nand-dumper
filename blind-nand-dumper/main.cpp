//
//  main.cpp
//  blind-nand-dumper
//
//  Created by tihmstar on 07.10.24.
//

#include "PicoNandReader.hpp"
#include "ECCCorrection.hpp"
#include "FileMapping.hpp"

#include <libgeneral/macros.h>
#include <libgeneral/Mem.hpp>

#include <atomic>
#include <memory>

#include <getopt.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

using namespace ECCCorrection;

static struct option longopts[] = {
    { "help",           no_argument,        NULL, 'h' },
    { "pageAddr",       required_argument,  NULL, 'a' },
    { "ce",             required_argument,  NULL, 'c' },
    { "input",          required_argument,  NULL, 'i' },
    { "threads",        required_argument,  NULL, 'j' },
    { "output",         required_argument,  NULL, 'o' },
    { "pagesize",       required_argument,  NULL, 'p' },
    { "readPage",       required_argument,  NULL, 'r' },
    { "readID",         no_argument,        NULL, 'I' },
    { "protocol",       required_argument,  NULL, 'P' },

    { "alt-pageread",   no_argument,        NULL,  0  },
    { "inplace",        no_argument,        NULL,  0  },

    //Send raw NAND command
    { "cmd-address",    required_argument,  NULL,  0  },
    { "cmd-command",    required_argument,  NULL,  0  },
    { "cmd-data",       required_argument,  NULL,  0  },
    { "cmd-read-size",  required_argument,  NULL,  0  },

    //Dump processing
    { "ecc",            required_argument,  NULL,  0  },
    { "page-structure", required_argument,  NULL,  0  },
    { "seekPages",      required_argument,  NULL,  0  },
    { "numPages",       required_argument,  NULL,  0  },

    { NULL, 0, NULL, 0 }
};

void cmd_help(){
    printf(
           "Usage: bnd [OPTIONS]\n"
           "Controlls nand reader\n"
           "  -h, --help\t\t\t\t\tprints usage information\n"
           "  -a, --pageAddr\t<addr>\t\t\tSet start page for reading/writing\n"
           "  -c, --ce\t\t<CE>\t\t\tSelect Crystal\n"
           "  -i, --input\t\t<PATH>\t\t\tSet input for reading\n"
           "  -j, --threads\t\t<num>\t\t\tSet number of threads\n"
           "  -o, --output\t\t<PATH>\t\t\tSet output for writing\n"
           "  -p, --pagesize\t<size>\t\t\tSet page size\n"
           "  -r, --readPage\t<num>\t\t\tRead number of pages\n"
           "  -I, --readID\t\t\t\t\tRead NAND chip ID\n"
           "  -P, --protocol\t<protocol>\t\tSelect Protocol ('nand8')\n"
           "      --alt-pageread\t\t\t\tUse alternative USB method for downloading page memory from pico\n"
           "      --inplace\t\t\t\t\tModify infile inplace\n"
           "\n"

           "Send raw NAND command:\n"
           "      --cmd-address\t\t\t\tSpecify raw NAND command address\n"
           "      --cmd-command\t\t\t\tSpecify raw NAND command command\n"
           "      --cmd-data\t\t\t\tSpecify raw NAND command data\n"
           "      --cmd-read-size\t\t\t\tSpecify raw NAND command response size\n"
           "\n"

           "Dump processing:\n"
           "      --ecc\t\t<alg,poly,params>\tSpecify ECC correction parameters (eg. bch,17475,ir)\n"
           "      --page-structure\t<N:size:type,N:size:type,...>\n"
           "                             \t\t\tSpecify page structure. Types d=data, s=service area, e=ecc (eg. 1:512:d,1:10:s,1:53:e,2:512:d,2:53:e,...)\n"
           "      --seekPages\t\t\t\tNumber of pages to skip\n"
           "      --numPages\t\t\t\tNumber of pages to process\n"
           "\n"
           );
}

void DumpHex(const void* data, size_t size, uint32_t startaddr = 0) {
    char ascii[17];
    size_t i, j;
    ascii[16] = '\0';
    for (i = 0; i < size; ++i) {
        if ((i & 0xf) == 0) {
            printf("0x%08llx: ",(uint64_t)startaddr + i);
        }
        printf("%02X ", ((unsigned char*)data)[i]);
        if (((unsigned char*)data)[i] >= ' ' && ((unsigned char*)data)[i] <= '~') {
            ascii[i % 16] = ((unsigned char*)data)[i];
        } else {
            ascii[i % 16] = '.';
        }
        if ((i+1) % 8 == 0 || i+1 == size) {
            printf(" ");
            if ((i+1) % 16 == 0) {
                printf("|  %s \n", ascii);
            } else if (i+1 == size) {
                ascii[(i+1) % 16] = '\0';
                if ((i+1) % 16 <= 8) {
                    printf(" ");
                }
                for (j = (i+1) % 16; j < 16; ++j) {
                    printf("   ");
                }
                printf("|  %s \n", ascii);
            }
        }
    }
}

t_ChipProtocol parseChipProtocol(const char *str){
    int protoNum = atoi(str);
    if (protoNum) return (t_ChipProtocol)protoNum;
    if (strcasecmp(str, "nand8") == 0) {
        return kChipProtocolNAND8;
    }else{
        return kChipProtocolUndefined;
    }
}

tihmstar::Mem parseHexdata(const char *str){
    tihmstar::Mem ret;
    while (*str) {
        uint32_t byte = 0;
        retassure(sscanf(str, "%02x",&byte) == 1, "Failed to parse byte");
        ret.append(&byte, 1);
        str += 2;
    }
    return ret;
}

uint64_t parseNumber(const char *str){
    if (strncasecmp(str, "0x", 2) == 0) {
        return strtoull(str, NULL, 16);
    }else{
        return strtoull(str, NULL, 10);
    }
}

struct RawNandCommand{
    tihmstar::Mem cmdAddress;
    tihmstar::Mem cmdCommand;
    tihmstar::Mem cmdData;
    size_t cmdResponseSize;
};

PageStructure parsePageStructure(const char *str){
    PageStructure ret;
    std::vector<std::string> parts;
    {
        std::string argstr = str;
        ssize_t commaPos = 0;
        while ((commaPos = argstr.find(",")) != std::string::npos) {
            std::string sp = argstr.substr(0, commaPos);
            parts.push_back(sp);
            argstr = argstr.substr(commaPos+1);
        }
        parts.push_back(argstr);
    }
    
    for (auto p : parts) {
        ssize_t posCol1 = 0;
        ssize_t posCol2 = 0;
        posCol1 = p.find(":");
        posCol2 = p.rfind(":");
        retassure(posCol1 != std::string::npos, "parsePageStructure failed to find first ':' in codeword descriptor");
        retassure(posCol2 != std::string::npos, "parsePageStructure failed to find second ':' in codeword descriptor");
        retassure(posCol1 < posCol2, "parsePageStructure failed to two ':' in codeword descriptor");
        
        std::string str_tag  = p.substr(0,posCol1);
        std::string str_len  = p.substr(posCol1+1,posCol2-posCol1-1);
        std::string str_type = p.substr(posCol2+1);
        
        retassure(str_type.size() >= 1, "parsePageStructure type got invalid len");
        
        PageCodeword ps = {
            .tag = static_cast<uint32_t>(atoi(str_tag.c_str())),
            .len = static_cast<uint32_t>(atoi(str_len.c_str())),
        };
        
        switch (str_type.at(0)) {
            case 'd':
            case 'D':
                ps.type = kPageCodewordTypeData;
                break;

            case 'e':
            case 'E':
                ps.type = kPageCodewordTypeECC;
                break;

            case 's':
            case 'S':
                ps.type = kPageCodewordTypeServiceArea;
                break;

            default:
                reterror("unexpected page structure type '%s'",str_type.c_str());
                break;
        }
        ret.push_back(ps);
    }
    
    return ret;
}

MAINFUNCTION
int main_r(int argc, const char * argv[]) {
    info("%s",VERSION_STRING);
    
    int optindex = 0;
    int opt = 0;
    
    uint8_t CE = 0;
    t_ChipProtocol chipProtocol = kChipProtocolNAND8;
    
    const char *inFile = NULL;
    const char *outFile = NULL;

    uint32_t numThreads = 0;
    uint32_t numPages = 0;
    uint32_t seekPages = 0;
    uint32_t pageAddress = 0;
    uint16_t pageSize = 0;
    
    uint32_t readPagesNum = 0;
    bool doReadID = false;
    
    bool wantAltPageread = false;
    bool modifyFileInplace = false;
    
    RawNandCommand nandCmd = {};
    std::vector<RawNandCommand> multipleNandCmds;

    std::vector<std::string> eccargs;
    PageStructure pageStructure;
    NandStructure nandStructure;

    while ((opt = getopt_long(argc, (char* const *)argv, "ha:c:i:j:o:p:r:IP:", longopts, &optindex)) >= 0) {
        switch (opt) {
            case 0: //long opts
            {
                std::string curopt = longopts[optindex].name;
                if (curopt == "alt-pageread") {
                    wantAltPageread = true;
                }else if (curopt == "cmd-address") {
                    nandCmd.cmdAddress = parseHexdata(optarg);
                }else if (curopt == "cmd-command") {
                    if (nandCmd.cmdCommand.size()) {
                        multipleNandCmds.push_back(nandCmd);
                        nandCmd = {};
                    }
                    nandCmd.cmdCommand = parseHexdata(optarg);
                }else if (curopt == "cmd-data") {
                    nandCmd.cmdData = parseHexdata(optarg);
                }else if (curopt == "cmd-read-size") {
                    nandCmd.cmdResponseSize = parseNumber(optarg);
                }else if (curopt == "ecc") {
                    std::string paramstring = optarg;
                    ssize_t commaPos = 0;
                    while ((commaPos = paramstring.find(",")) != std::string::npos) {
                        std::string part = paramstring.substr(0, commaPos);
                        eccargs.push_back(part);
                        paramstring = paramstring.substr(commaPos+1);
                    }
                    eccargs.push_back(paramstring);
                }else if (curopt == "inplace") {
                    modifyFileInplace = true;
                }else if (curopt == "numPages") {
                    numPages = (uint32_t)parseNumber(optarg);
                }else if (curopt == "page-structure") {
                    if (pageStructure.size()) {
                        retassure(nandStructure.size() == 0 || nandStructure.back().pagesCnt != 0, "Cannot chain multiple page structures with implicit length");
                        nandStructure.push_back({
                            .pageStructure = pageStructure,
                            .startPage = seekPages,
                            .pagesCnt = numPages,
                        });
                        pageStructure = {};
                        seekPages += numPages;
                        numPages = 0;
                    }
                    pageStructure = parsePageStructure(optarg);
                }else if (curopt == "seekPages") {
                    seekPages = (uint32_t)parseNumber(optarg);
                }else{
                    error("Unexpected longopt '%s'",curopt.c_str());
                    return -2;
                }
                break;
            }
                
            case 'h': //help
                cmd_help();
                return 0;

            case 'a': //pageAddr
                pageAddress = (uint32_t)parseNumber(optarg);
                break;
                
            case 'c': //ce
                CE = atoi(optarg);
                debug("selecting CE %d",CE);
                break;

            case 'i': //input
                retassure(!inFile, "Invalid command line arguments. inFile already set!");
                inFile = optarg;
                break;

            case 'j': //threads
                numThreads = (uint32_t)parseNumber(optarg);
                break;

            case 'o': //output
                retassure(!outFile, "Invalid command line arguments. outFile already set!");
                outFile = optarg;
                
                if (strcmp(outFile, "-") == 0) {
                    int s_out = -1;
                    int s_err = -1;
                    cleanup([&]{
                        safeClose(s_out);
                        safeClose(s_err);
                    });
                    s_out = dup(STDOUT_FILENO);
                    s_err = dup(STDERR_FILENO);
                    dup2(s_out, STDERR_FILENO);
                    dup2(s_err, STDOUT_FILENO);
                }
                break;
                
            case 'p': //protocol
                pageSize = parseNumber(optarg);
                debug("Setting page size to %d (0x%x)",pageSize,pageSize);
                break;
                
            case 'r': //readPage
                readPagesNum = atoi(optarg);
                break;
                
            case 'I': //readId
                doReadID = true;
                break;

            case 'P': //pagesize
                chipProtocol = parseChipProtocol(optarg);
                break;

            default:
                cmd_help();
                return -1;
        }
    }

    PicoNandReader pnr;

    if (eccargs.size()){
        std::string alg = eccargs.front();
        eccargs.erase(eccargs.begin());
        
        if (strcasecmp(alg.c_str(), "bch") == 0) {
            info("Running BCH ECC");
            uint32_t poly = 0;
            bool swapbits = false;
            bool inverse = false;
            
            if (eccargs.size() < 1) {
                error("missing poly argument for BCH");
                return -5;
            }
            {
                std::string polystr = eccargs.front();
                eccargs.erase(eccargs.begin());
                poly = (uint32_t)parseNumber(polystr.c_str());
            }
            if (!poly) {
                error("BCH polynom cannot be 0!");
                return -5;
            }
            
            for (auto arg : eccargs) {
                if (strcasecmp(arg.c_str(), "i") == 0) {
                    inverse = true;
                }else if (strcasecmp(arg.c_str(), "r") == 0) {
                    swapbits = true;
                }else{
                    error("unexpected BCH arg '%s'",arg.c_str());
                    return -5;
                }
            }
            info("poly: %d (0x%x) inverse=%d swapbits=%d",poly,poly,inverse,swapbits);
            
            std::atomic<uint32_t> goodCodewords = 0;
            std::atomic<uint32_t> correctedCodewords = 0;
            std::atomic<uint32_t> uncorrectableCodewords = 0;

            std::atomic<uint32_t> correctedBitflips = 0;
                    
            
            
            {
                FileMapping inmap(inFile, modifyFileInplace && !outFile);
                std::shared_ptr<FileMapping> outmapManaged = nullptr;
                
                FileMapping *outmap = nullptr;

                if (outFile) {
                    reterror("TODO");
                    outmapManaged = std::make_shared<FileMapping>(outFile, true);
                    outmap = outmapManaged.get();
                }else if (modifyFileInplace) {
                    outmap = &inmap;
                }else{
                    warning("No outputfile specified, in-ram ecc computation will be discraded!");
                }
                
                {
                    retassure(nandStructure.size() == 0 || nandStructure.back().pagesCnt != 0, "Cannot chain multiple page structures with implicit length");
                    nandStructure.push_back({
                        .pageStructure = pageStructure,
                        .startPage = seekPages,
                        .pagesCnt = numPages,
                    });
                    seekPages += numPages;
                    numPages = 0;
                }
                
                uint32_t processedPages = processPages(&inmap, outmap, pageSize, nandStructure,
                                                           [&goodCodewords, &correctedCodewords, &uncorrectableCodewords, &correctedBitflips, numPages, seekPages, poly, swapbits, inverse]
                                                           (uint32_t pagenum, uint32_t cwnum, const uint8_t *codeword, size_t codewordSize, const uint8_t *eccdata, size_t eccdataSize, uint8_t *outCodeword, uint8_t *outECC, void *userarg){
                    int errbits = 0;
                    uint8_t cw[codewordSize];
                    uint8_t ecc[eccdataSize];
                    memcpy(cw, codeword, sizeof(cw));
                    memcpy(ecc, eccdata, sizeof(ecc));

                    errbits = eccBCH(cw, sizeof(cw), ecc, sizeof(ecc), poly, swapbits, inverse);

                    if (errbits < 0) {
                        uncorrectableCodewords++;
                        fprintf(stderr,"Uncorrectable errors in Page 0x%x CW %d\n",pagenum,cwnum);
                    }else{
                        if (errbits > 0){
                            correctedBitflips += errbits;
                            correctedCodewords++;
                            debug("Corrected %d bits in Page 0x%x CW %d",errbits,pagenum,cwnum);
                        }else{
                            goodCodewords++;
                        }
                        if (outCodeword) memcpy(outCodeword, cw, sizeof(cw));
                        if (outECC) memcpy(outECC, ecc, sizeof(ecc));
                    }
                }, NULL, numThreads);
                
                double totalCodewords = goodCodewords.load() + correctedCodewords.load() + uncorrectableCodewords.load();
                double percentGood = (goodCodewords.load() / totalCodewords)*100;
                double percentCorrected = (correctedCodewords.load() / totalCodewords)*100;
                double percentUncorrectable = (uncorrectableCodewords.load() / totalCodewords)*100;
                info("ECC Report:");
                info("Processed     pages    : 0x%08x | %10d",processedPages,processedPages);
                info("Good          codewords: 0x%08x | %10d [%5.2f%%]",goodCodewords.load(),goodCodewords.load(),percentGood);
                info("Corrected     codewords: 0x%08x | %10d [%5.2f%%] corrected bitflips 0x%08x (%d)",correctedCodewords.load(),correctedCodewords.load(),percentCorrected,correctedBitflips.load(),correctedBitflips.load());
                info("Uncorrectable codewords: 0x%08x | %10d [%5.2f%%]",uncorrectableCodewords.load(),uncorrectableCodewords.load(), percentUncorrectable);
                return 0;
            }
        }else{
            error("Unkown algorithm ECC '%s'",alg.c_str());
            return -5;
        }
    }
    
    
    pnr.connectReader();
    if (chipProtocol != kChipProtocolUndefined) {
        debug("Setting chip protocol to %d",chipProtocol);
        pnr.selectProtocol(chipProtocol);
    }
    
    debug("resetting chip");
    pnr.resetChip();

    if (doReadID) {
        for (int i=0; i<4; i++) {
            uint64_t cid = pnr.readChipIDForCE(i);
            printf("CE%d ID:",i);
            for (int j=0; j<sizeof(cid); j++) {
                printf(" %02x",(int)(cid >> j*8)&0xff);
            }
            printf("\n");
        }
    }else if (readPagesNum) {
        if (!pageSize) {
            error("Pagesize not set!");
            return -2;
        }
        
        int fd = -1;
        cleanup([&]{
            safeClose(fd);
        });
        if (outFile) {
            if (strcmp(outFile, "-") == 0) {
                fd = dup(STDERR_FILENO);
            }else{
                retassure((fd = open(outFile, O_WRONLY | O_CREAT, 0644)),"Failed to open '%s' with err=%d (%s)",outFile,errno,strerror(errno));
            }
        }
        
        if (wantAltPageread) {
            for (uint32_t i=0; i<readPagesNum; i++) {
                auto data = pnr.readPage(CE, pageAddress+i, pageSize);
                if (fd == -1) {
                    DumpHex(data.data(), data.size(), i*pageSize);
                }else{
                    write(fd, data.data(), data.size());
                }
            }
        }else{
            uint32_t curAddr = 0;
            pnr.dumpPages(CE, pageAddress, pageSize, readPagesNum, [&](const void *chunk, size_t chunkSize, void *arg)->bool{
                if (fd == -1) {
                    DumpHex(chunk, chunkSize, curAddr);
                    curAddr += chunkSize;
                }else{
                    write(fd, chunk, chunkSize);
                }
                return true;
            }, NULL);
        }
    }else if (nandCmd.cmdCommand.size()) {
        multipleNandCmds.push_back(nandCmd);
        
        int cmdNum = 0;
        size_t totalCommands = multipleNandCmds.size();
        for (auto cmd : multipleNandCmds) {
            tihmstar::Mem cmdResponse(cmd.cmdResponseSize);
            pnr.sendNandCommand(CE, cmd.cmdCommand.data(), cmd.cmdCommand.size(), cmd.cmdAddress.data(), cmd.cmdAddress.size(), cmd.cmdData.data(), cmd.cmdData.size(), cmdResponse.data(), cmdResponse.size(),cmdNum+1 < totalCommands);
            printf("\nCommand %d\n",cmdNum++);
            DumpHex(cmdResponse.data(), cmdResponse.size());
        }
    }else{
        cmd_help();
        return -1;
    }

    info("Done");
    return 0;
}
