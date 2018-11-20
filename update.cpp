#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <cctype>
#include <string>

#include "update.h"
#include "AmlTime.h"
#include "Amldbglog/Amldbglog.h"
#include "AmlUsbScanX3/AmlUsbScanX3.h"
#include "UsbRomDrv/UsbRomDrv.h"
#include "AmlUsbScan/AmlUsbScan.h"
#include "defs.h"

std::string strToLower (const std::string &src) {
  std::string dst(src);
  std::transform(src.begin(), src.end(), dst.begin(), tolower);
  return dst;
}

const char *_parse_integer_fixup_radix (const char *s, unsigned int *base) {
  if (*base == 0) {
    if (s[0] == '0') {
      if (_tolower(s[1]) == 'x' && isxdigit(s[2])) {
        *base = 16;
      } else {
        *base = 8;
      }
    } else {
      *base = 10;
    }
  }
  if (*base == 16 && s[0] == '0' && _tolower(s[1]) == 'x') {
    s += 2;
  }
  return s;
}


unsigned int _parse_integer (const char *s, unsigned int base, unsigned long *p) {
  unsigned long res;
  unsigned int rv;

  res = 0;
  rv = 0;
  while (true) {
    unsigned int c = *(unsigned char *) s;
    unsigned int lc = c | 0x20; /* don't tolower() this line */
    unsigned int val;

    if ('0' <= c && c <= '9') {
      val = c - '0';
    } else if ('a' <= lc && lc <= 'f') {
      val = lc - 'a' + 10;
    } else {
      break;
    }

    if (val >= base) {
      break;
    }
    res = res * base + val;
    rv++;
    s++;
  }
  *p = res;
  return rv;
}

//----- (000000000040885C) ----------------------------------------------------
unsigned long simple_strtoul (const char *cp, char **endp, unsigned int base) {
  unsigned long result;
  unsigned int rv;

  cp = _parse_integer_fixup_radix(cp, &base);
  rv = _parse_integer(cp, base, &result);
  cp += rv;

  if (endp) {
    *endp = (char *) cp;
  }

  return result;
}

//----- (000000000040899A) ----------------------------------------------------
int _print_memory_view (char *buf, unsigned int size, unsigned int offset) {
  for (int printedDq = 0; printedDq < (size + 15) >> 4; ++printedDq) {
    unsigned int DqToPrint = 16 * (printedDq + 1) <= size ? 16 : size & 0xF;
    printf("\n%08X: ", offset);
    for (int i = 0; i < (DqToPrint + 3) >> 2; ++i) {
      printf("%08x ", *(unsigned int *) &buf[4 * (4 * printedDq + i)]);
    }
    offset += 16;
  }
  putchar('\n');
  return 0;
}

//----- (0000000000408A77) ----------------------------------------------------
int update_help () {
  puts("====>Amlogic update USB tool(Ver 1.5) 2017/05<=============");
  puts("update\t<command>\t[device name]\t<arg0>\t<arg1>\t<arg2>\t...");
  puts("\nCommon Commands:");
  puts("update <partition>: Burn a partition with a partition image");
  puts("update <mwrite>   : Burn data to media or memory");
  puts("update <mread>    : Dump a data from media or memory to pc and save as a file");
  puts("update <tplcmd>   : like bulkcmd");
  puts("update <bulkcmd>  : pass and exec a command platform bootloader can support");
  puts("update <write>    : Down a file to memory");
  puts("update <run>      : Run code from memory address");
  puts("update <read>     : Dump data from memory:");
  puts("update <wreg>     : set one 32bits reg:");
  puts("update <rreg>     : Dump data from reg:");
  puts("update <password> : unlock chip:");
  puts("update <chipinfo> : get chip info at page index:");
  puts("update <chipid>   : get chip id");
  puts("\nCommon Commands format:");
  puts("update partition partName imgFilePath [imgFileFmt] [sha1VeryFile]");
  puts(
      "\t\te.g.--\tupdate partition boot z:\\a\\b\\boot.img [normal] //format normal is optional");
  puts(
      "\t\te.g.--\tupdate partition system z:\\xxxx\\system.img [sparse] //format sparse is optional");
  puts(
      "\t\te.g.--\tupdate partition upgrade z:\\xxxx\\upgrade.ubifs.img ubifs //format ubifs is MANDATORY");
  puts("\nupdate bulkcmd \"burning cmd or u-boot cmd\"");
  puts(
      "\t\te.g.--\tupdate bulkcmd \"disk_intial 0\" //cmd to init flash for usb burning");
  puts("\t\te.g.--\tupdate bulkcmd \"printenv\" //uboot command 'printenv'");
  puts("\nupdate mread store/mem partName/memAddress");
  puts(
      "\t\te.g.--\tupdate mread store boot normal 0x200000 d:\boot.dump //upload 32M of boot partition in path d:\boot.dump");
  puts(
      "\t\te.g.--\tupdate mread mem 0x1080000 normal d:\\mem_2M.dump //upload 2M memory at address 0x1080000 in path d:\\mem_2M.dump");
  puts("\t\te.g.--\tupdate chipinfo pageIndex dumpFilePath nBytes startOffset");
  return puts("====>Amlogic update USB tool(Ver 1.5) 2017/05<=============");
}

//----- (0000000000408BA0) ----------------------------------------------------
int update_scan (void **resultDevices, int print_dev_list, unsigned int dev_no,
                 int *success, char *scan_mass_storage) {
  int result; // [rsp+30h] [rbp-E0h]
  char *buf; // [rsp+58h] [rbp-B8h]
  char *candidateDevices[16] = {};

  *success = -1;
  result = 0;
  candidateDevices[0] = new char[0x800];
  buf = candidateDevices[0];
  memset(candidateDevices[0], 0, 0x800);
  for (int i = 1; i <= 15; ++i) {
    candidateDevices[i] = candidateDevices[i - 1] + 128;
  }
  const char *vendorName = scan_mass_storage ? "USB Mass Storage Device"
                                             : "WorldCup Device";
  int nDevices = AmlScanUsbX3Devices(vendorName, candidateDevices);
  if (nDevices <= 0) {
    aml_printf("[update]No [%s] device after scan\n", vendorName);
    result = -175;
    goto finish;
  }

  if (dev_no != -2 && nDevices) {
    if (dev_no + 1 > nDevices) {
      aml_printf("[update]ERR(L%d):", 183);
      aml_printf("dev_no(%d) too only, only [%d] (%s)devices Connected\n", dev_no,
                 nDevices, vendorName);
      result = -184;
      goto finish;
    }
    if (!scan_mass_storage) {
      if (!resultDevices) {
        aml_printf("[update]ERR(L%d):", 195);
        aml_printf("handle for (%s) NULL is invalid\n", vendorName);
        result = -196;
        goto finish;
      }
      *resultDevices = AmlGetDeviceHandle(vendorName, candidateDevices[dev_no]);
    } else {
      printf("%d.%s ==> ", dev_no, candidateDevices[dev_no]);
      AmlGetMsNumber(candidateDevices[dev_no], 1, scan_mass_storage);
    }
  }

  *success = 0;
  if (print_dev_list) {
    printf("\t-------%sdevices list -------\n",
           scan_mass_storage ? "Mass Storage " : "MPtool ");
    for (int j = 0; j < nDevices; ++j) {
      printf("WorldCup[%02d].%s\n", j, candidateDevices[j]);
    }
  }

  finish:
  if (buf) {
    delete[] buf;
  }
  return result;
}
// 6CD3D8: using guessed type char *off_6CD3D8;

//----- (0000000000408F60) ----------------------------------------------------
int do_cmd_mwrtie (const char **argv, signed int argc, AmlUsbRomRW &rom) {
  int result = -229;
  const char *filename = argv[0];
  const char *storeOrMem = argv[1];
  const char *partition = argv[2];
  const char *filetype = argv[3];
  const char *verifyFile = argc <= 4 ? nullptr : argv[4];
  signed int retry; // [rsp+20h] [rbp-E0h]
  off_t fileSize;
  unsigned int dataSize; // [rsp+30h] [rbp-D0h]fp; // [rsp+38h] [rbp-C8h]
  char buffer[128] = {};

  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    aml_printf("Open file %s failed\n", filename);
    goto finish;
  }

  fseeko64(fp, 0, 2);
  fileSize = ftello(fp);
  fclose(fp);
  fp = nullptr;
  aml_printf("file size is 0x%llx\n", fileSize);
  if (!fileSize) {
    aml_printf("file size 0!!\n");
    return -252;
  }

  snprintf((char *) buffer, sizeof(buffer), "download %s %s %s 0x%lx", storeOrMem,
           partition, filetype, fileSize);
  buffer[66] = 1;
  rom.buffer = buffer;
  rom.bufferLen = 68;
  rom.pDataSize = &dataSize;
  if (AmlUsbTplCmd(&rom)) {
    goto finish;
  }

  retry = 1;
  memset(buffer, 0, 0x80u);
  while (retry) {
    rom.buffer = buffer;
    rom.bufferLen = 64;
    rom.pDataSize = &dataSize;
    if (!(unsigned int) AmlUsbReadStatus(&rom)) {
      break;
    }
    usleep(1000000);
    --retry;
  }
  if (!retry) {
    aml_printf("Read status failed\n");
    result = -292;
    goto finish;
  }

  if (strncmp((const char *) &rom.buffer, "success", 7uLL) != 0) {
    aml_printf("[update]ERR(L%d):", 298);
    aml_printf("cmdret=[%s]\n", rom.buffer);
    result = -299;
    goto finish;
  }

  if (WriteMediaFile(&rom, filename) != 0) {
    aml_printf("ERR:write data to media failed\n");
    result = -306;
    goto finish;
  }

  strcpy((char *) buffer, "download get_status");
  buffer[66] = 1;
  rom.buffer = buffer;
  rom.bufferLen = 68;
  rom.pDataSize = &dataSize;
  if (AmlUsbBulkCmd(&rom) != 0) {
    aml_printf("[update]ERR(L%d):", 319);
    aml_printf("AmlUsbBulkCmd failed!\n");
    result = -320;
    goto finish;
  }

  if (!verifyFile) {
    result = 0;
    aml_printf("[update]mwrite success\n");
    goto finish;
  }

  memset(buffer, 0, 0x80);
  fp = fopen(verifyFile, "rb");
  if (!fp) {
    aml_printf("Open file %s failed\n", verifyFile);
    goto finish;
  }

  strcpy((char *) buffer, "verify ");
  fread(&buffer[7], 1, 0x79, fp);
  fclose(fp);
  fp = nullptr;
  buffer[66] = 1;
  rom.buffer = buffer;
  rom.bufferLen = 68;
  rom.pDataSize = &dataSize;
  if (AmlUsbBulkCmd(&rom) != 0) {
    aml_printf("ERR: AmlUsbBulkCmd failed!\n");
    result = -346;
    goto finish;
  }


  finish:
  if (fp) {
    fclose(fp);
  }
  if (rom.device) {
    rom.device = nullptr;
  }
  return result;
}

//----- (0000000000409531) ----------------------------------------------------
int update_sub_cmd_run_and_rreg (AmlUsbRomRW &rom, const char *cmd, const char **argv,
                                 signed int argc) {
  int result; // rax
  const char *v5; // rax
  const char *v6; // rax
  const char *filename; // rax
  FILE *fp; // rax MAPDST
  int v9; // eax
  const char *v10; // rax
  unsigned int v11; // eax
  unsigned int transferSize_1; // [rsp+28h] [rbp-B8h]
  int ret; // [rsp+28h] [rbp-B8h]
  unsigned int transferSize; // [rsp+2Ch] [rbp-B4h]
  unsigned int v17; // [rsp+30h] [rbp-B0h]
  unsigned int offset; // [rsp+34h] [rbp-ACh]
  unsigned int v19; // [rsp+40h] [rbp-A0h]
  int isRreg; // [rsp+48h] [rbp-98h]
  unsigned int size; // [rsp+4Ch] [rbp-94h]
  FILE *size_4; // [rsp+50h] [rbp-90h]
  char *buffer; // [rsp+78h] [rbp-68h]

  transferSize_1 = 0;
  if (argc <= 0) {
    aml_printf("[update]ERR(L%d):", 373);
    aml_printf("argc(%d)<2, address for cmd[%s] not assigned\n", argv, cmd);
    return -374;
  }

  rom.address = simple_strtoul(strToLower(argv[0]).c_str(), nullptr, 0);
  if (!strcmp(cmd, "run")) {
    aml_printf("[update]Run at Addr %x\n", (unsigned int) rom.address);
    rom.buffer = (char *) &rom.address;
    result = AmlUsbRunBinCode(&rom);
    if (result) {
      puts("ERR: Run cmd failed");
    }
  } else {
    v19 = simple_strtoul(strToLower(argv[1]).c_str(), nullptr, 0);
    filename = argc <= 2 ? nullptr : argv[2];
    fp = filename ? fopen(filename, "wb") : nullptr;
    buffer = new char[0x100000];
    v9 = strcmp(cmd, "rreg");
    isRreg = v9 == 0;
    size_4 = nullptr;
    if (v9 == 0) {
      transferSize = simple_strtoul(strToLower(argv[0]).c_str(), nullptr, 0);
    } else {
      size_4 = fopen(*argv, "rb");
      fseeko64(size_4, 0LL, 2);
      transferSize = ftell(size_4);
      fseek(size_4, 0LL, 0);
    }
    aml_printf("[update]Total tansfer size 0x%x\n", transferSize);
    v17 = 0;
    offset = v19;
    while (v17 < transferSize) {
      v11 = transferSize - v17;
      if (transferSize - v17 > 0x100000) {
        v11 = 0x100000;
      }
      size = v11;
      if (size_4) {
        fread(buffer, v11, 1uLL, size_4);
      }
      transferSize_1 = Aml_Libusb_Ctrl_RdWr(&rom.device, offset, buffer, size, isRreg,
                                            5000u);
      if (transferSize_1) {
        aml_printf("[update]ret=%d\n", transferSize_1);
        break;
      }
      if (isRreg) {
        if (fp) {
          fwrite(buffer, size, 1uLL, fp);
        } else {
          _print_memory_view(buffer, size, offset);
        }
      }
      v17 += size;
      offset += size;
    }
    if (buffer) {
      delete[] buffer;
    }
    if (fp) {
      fclose(fp);
    }
    result = transferSize_1;
  }
  return result;
}

//----- (0000000000409B77) ----------------------------------------------------
int update_sub_cmd_set_password (AmlUsbRomRW &rom, const char **argv, int a3) {
  int ret = 0; // [rsp+28h] [rbp-78h]filename; // [rsp+40h] [rbp-60h]

  const char *filename = argv[0];
  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    aml_printf("[update]ERR(L%d):", 470);
    aml_printf("Open file %s failed\n", filename);
    return -1;
  }

  fseeko(fp, 0, 2);
  off_t totalFileSz = ftello(fp);
  if (totalFileSz > 64) {
    aml_printf("[update]ERR(L%d):", 477);
    aml_printf("size of file(%s) is %lld too large!!\n", filename);
    fclose(fp);
    return -479;
  }

  fseek(fp, 0, 0);
  char ptr; // [rsp+50h] [rbp-50h]
  size_t rdLen = fread(&ptr, 1, totalFileSz, fp);
  if (rdLen != totalFileSz) {
    aml_printf("[update]ERR(L%d):", 486);
    aml_printf("FAil in read pwd file (%s), rdLen(%d) != TotalFileSz(%d)\n", filename,
               rdLen, totalFileSz);
    fclose(fp);
    return -488;
  }

  fclose(fp);
  for (int i = 0; i <= 1 && (ret & 0x80000000) == 0; ++i) {
    ret = Aml_Libusb_Password(&rom.device, &ptr, rdLen, 8000);
    if (!i) {
      printf("Setting PWD..");
      usleep(5000000);
    }
  }
  aml_printf("Pwd returned %d\n", ret);
  return ret <= 0 ? ret : 0;
}

int update_sub_cmd_get_chipinfo (AmlUsbRomRW &rom, const char **argv, signed int argc) {
  const char *saveFile; // rax MAPDST
  int nBytes; // eax MAPDST
  int startOffset; // eax MAPDST
  int result; // rax
  // [rsp+23h] [rbp-2Dh]
  unsigned int pageIndex; // [rsp+28h] [rbp-28h]
  FILE *fp; // [rsp+38h] [rbp-18h]
  char *buf; // [rsp+48h] [rbp-8h]

  pageIndex = atoi(*argv);
  saveFile = argc <= 1 ? "-" : argv[1];
  nBytes = argc <= 2 ? 64 : atoi(argv[2]);
  startOffset = argc <= 3 ? 0 : atoi(argv[3]);
  fp = nullptr;
  aml_printf("[update]pageIndex %d, saveFile %s, nBytes %d, startOffset %d\n", pageIndex,
             saveFile, nBytes, startOffset);
  if (nBytes > 64) {
    aml_printf("[update]ERR(L%d):", 518);
    aml_printf("pageSzMax %d < nBytes %d\n", 64LL, nBytes);
    result = -519;
    return result;
  }

  buf = new char[0x40u];
  if (!buf) {
    aml_printf("[update]ERR(L%d):", 523);
    aml_printf("Fail in alloc buff\n");
    result = -524;
    return result;
  }

  int read = Aml_Libusb_get_chipinfo(&rom.device, buf, 64, pageIndex, 8000);
  if (read != 64) {
    aml_printf("[update]ERR(L%d):", 529);
    aml_printf("Fail in get chipinfo, want %d, actual %d\n", 64, read);
    result = -530;
    goto finish;
  }

  if (strcmp("-", saveFile) == 0) {
    _print_memory_view(&buf[startOffset], nBytes, startOffset);
    result = read;
  } else {
    fp = fopen(saveFile, "wb");
    if (!fp) {
      aml_printf("[update]ERR(L%d):", 539);
      aml_printf("FAil in open file %s\n", saveFile);
      result = -540;
      goto finish;
    }

    if (fwrite(&buf[startOffset], 1uLL, nBytes, fp) != nBytes) {
      aml_printf("[update]ERR(L%d):", 545);
      aml_printf("FAil in save file %s\n", saveFile);
      result = -546;
      goto finish;
    }

    result = 0;
  }

  finish:;
  if (buf) {
    delete[]buf;
  }
  if (fp) {
    fclose(fp);
  }
  return result;
}

//----- (000000000040A07E) ----------------------------------------------------
int update_sub_cmd_read_write (AmlUsbRomRW &rom, const char *cmd, const char **argv,
                               int argc) {
  if (argc <= 1) {
    aml_printf("[update]ERR(L%d):", 574);
    aml_printf("cmd[%s] must at least %d parameters, but only %d\n", cmd, 2, argc);
    return 575;
  }

  const char *v5; // rax
  const char *dumpFilename; // rax MAPDST
  FILE *dumpFp; // rax MAPDST
  const char *v8; // rax
  unsigned int total_; // ST5C_4
  const char *v10; // rax
  int v13; // eax
  int v14; // eax
  signed int v15; // eax
  signed int v16; // eax
  signed int v17; // eax
  bool isCmdDump; // [rsp+2Bh] [rbp-115h]
  signed int v21; // [rsp+2Ch] [rbp-114h]
  unsigned int totalSize; // [rsp+30h] [rbp-110h]
   int result; // [rsp+34h] [rbp-10Ch]
  int resulta; // [rsp+34h] [rbp-10Ch]
  unsigned int dataLen; // [rsp+38h] [rbp-108h] MAPDST
  unsigned int v26; // [rsp+3Ch] [rbp-104h]
  unsigned int i; // [rsp+40h] [rbp-100h]
  unsigned int j; // [rsp+44h] [rbp-FCh]
  signed int v29; // [rsp+50h] [rbp-F0h]
  // [rsp+54h] [rbp-ECh]
  // [rsp+58h] [rbp-E8h]
  unsigned int dumpFileSize; // [rsp+60h] [rbp-E0h]
  int v33; // [rsp+64h] [rbp-DCh]
  __int64 v34; // [rsp+68h] [rbp-D8h]
  long buf; // [rsp+70h] [rbp-D0h]
  FILE *fp; // [rsp+78h] [rbp-C8h]
  void *buffer; // [rsp+80h] [rbp-C0h] MAPDST
  const char *v38; // [rsp+88h] [rbp-B8h]
  unsigned int offset; // [rsp+90h] [rbp-B0h]
  void *v40; // [rsp+98h] [rbp-A8h]
  unsigned int *v44; // [rsp+B8h] [rbp-88h]
  std::string str; // [rsp+C0h] [rbp-80h]
  int v47; // [rsp+120h] [rbp-20h]
  char v48; // [rsp+124h] [rbp-1Ch]

  v38 = nullptr;
  fp = nullptr;
  buffer = nullptr;
  buf = 0;
  totalSize = 0;
  v47 = 0;
  v48 = 0;

  rom.address = simple_strtoul(strToLower(argv[1]).c_str(), 0, 0);
  offset = rom.address;
  if (!strcmp(cmd, "wreg")) {
    buf = simple_strtoul(*argv, nullptr, 0x10u);
    rom.buffer = (char *) &buf;
    rom.bufferLen = 4;
    result = Aml_Libusb_Ctrl_RdWr(&rom.device, offset, rom.buffer, rom.bufferLen, 0,
                                  5000u);
    if (result) {
      aml_printf("[update]ERR(L%d):", 593);
      aml_printf("write register failed\n");
    } else {
      aml_printf("[update]operate finished!\n");
    }
    goto finish;
  }

  if (!strcmp(cmd, "read") || !strcmp("dump", cmd)) {
    if (argc <= 2) {
      dumpFilename = nullptr;
    } else {
      dumpFilename = argv[2];
    }
    if (dumpFilename) {
      dumpFp = fopen(dumpFilename, "wb");
    } else {
      dumpFp = nullptr;
    }
    isCmdDump = strcmp("dump", cmd) == 0;
    result = 0;
    buf = simple_strtoul(strToLower(argv[0]).c_str(), nullptr, 0);
    total_ = buf;
    rom.address = simple_strtoul(strToLower(argv[1]).c_str(), 0LL, 0);
    offset = (unsigned int) rom.address;
    DownloadProgressInfo v46(total_, "DUMP");
    buffer = new char[0x20000];
    while (buf) {
      if (isCmdDump) {
        if (buf > 0xFFFF) {
          dataLen = 0x10000;
        } else if (buf > 0xFFF) {
          dataLen = 4096;
        } else {
          dataLen = buf;
          if (buf > 0x200) {
            dataLen = 512;
          }
        }
      } else {
        dataLen = buf;
        if (buf > 0x200) {
          dataLen = 512;
        }
      }
      rom.buffer = (char *) buffer;
      rom.bufferLen = dataLen;
      rom.pDataSize = (unsigned int *) &v34;
      if (AmlUsbReadLargeMem::AmlUsbReadLargeMem(&rom) != 0) {
        aml_printf("[update]ERR(L%d):", 630);
        aml_printf("read device failed\n");
        result = -631;
        goto finish;
      }

      if (dumpFp) {
        dumpFileSize = fwrite(buffer, 1, dataLen, dumpFp);
        if (dumpFileSize != dataLen) {
          aml_printf("[update]ERR(L%d):", 639);
          aml_printf("Want to write %dB to path[%s], but only %dB\n", dataLen,
                     dumpFilename, dumpFileSize);
          result = -640;
          break;
        }
        v46.update_progress(dataLen);
      } else {
        v44 = (unsigned int *) buffer;
        v26 = 0;
        for (int i = rom.address;; i += 16) {
          v13 = dataLen + 15;
          if ((signed int) (dataLen + 15) < 0) {
            v13 = dataLen + 30;
          }
          if (v13 >> 4 <= v26) {
            break;
          }
          if (16 * (v26 + 1) <= dataLen) {
            v14 = 16;
          } else {
            v14 = dataLen & 0xF;
          }
          v33 = v14;
          printf("\n%08X: ", i);
          for (int j = 0; (unsigned int) (v33 + 3) >> 2 > j; ++j) {
            printf("%08x ", v44[4 * v26 + j]);
          }
          ++v26;
        }
        putchar('\n');
      }
      buf -= v34;
      rom.address += v34;
    }
    if (dumpFp) {
      fclose(dumpFp);
      dumpFp = nullptr;
    }
    goto finish;
  }

  v38 = argv[0];
  if (strcmp(cmd, "write") && strcmp(cmd, "boot") && strcmp(cmd, "cwr") &&
      strcmp(cmd, "write2")) {
    result = 0;
    goto finish;
  }

  fp = fopen(v38, "rb");
  if (!fp) {
    aml_printf("[update]ERR(L%d):", 681);
    aml_printf("ERR: can not open the %s file\n", v38);
    result = -682;
    goto finish;
  }

  v15 = !strcmp(cmd, "write") ? 17 : 1;
  v29 = v15;
  v16 = v15 == 1 ? 64 : 0x10000;
  buffer = malloc(0x10008u);
  v40 = (char *) buffer + 8;
  fseek(fp, 0LL, 2);
  v21 = ftell(fp);
  fseek(fp, 0LL, 0);
  while (v21) {
    v17 = min(v21, v16);
    fread(v40, 1, v17, fp);
    rom.buffer = (char *) v40 - 4;
    rom.bufferLen = v17;
    rom.pDataSize = (unsigned int *) &v34;
    if (v29 == 1) {
      *(int *) rom.buffer = htole32(rom.address);
      rom.bufferLen += 4;
      resulta = AmlUsbCtrlWr(&rom);
    } else {
      rom.buffer = (char *) v40;
      resulta = AmlUsbWriteLargeMem::AmlUsbWriteLargeMem(&rom);
    }
    if (resulta) {
      puts("ERR: write data to device failed");
      result = -717;
      goto finish;
    }
    v21 -= v17;
    rom.address += v17;
    totalSize += v17;
    fseek(fp, totalSize, 0);
    printf("..");
  }
  printf("\nTransfer Complete! total size is %d Bytes\n", totalSize);
  if (!strcmp(cmd, "boot")) {
    rom.address = offset;
    rom.buffer = (char *) &rom.address;
    result = AmlUsbRunBinCode(&rom);
    if (result) {
      puts("ERR: Run cmd failed");
      goto finish;
    }
  }
  result = 0;
  goto finish;

  finish:
  if (fp) {
    fclose(fp);
  }
  if (buffer) {
    free(buffer);
  }
  if (rom.device) {
    rom.device = nullptr;
  }
  return result;
}

int update_sub_cmd_identify_host (AmlUsbRomRW &rom, int idLen, char *id) {
  if (idLen <= 3) {
    aml_printf("[update]ERR(L%d):", 757);
    aml_printf("idLen %d invalid\n", idLen);
    return -758;
  }

  unsigned int dataLen = 0;
   char id_buf[16];

  rom.buffer = id ? id : id_buf;
  rom.bufferLen = idLen;
  rom.pDataSize = &dataLen;
  if (AmlUsbIdentifyHost(&rom)) {
    puts("ERR: get info from device failed");
    return -767;
  }

  printf("This firmware version is ");
  bool fw_ver_check_error = true;
  for (int dataPtr = 0; dataPtr < dataLen; dataPtr++) {
    printf("%d%c", id[dataPtr], dataPtr + 1 >= dataLen ? '\n' : '-');
    if (dataPtr <= 3 && id[dataPtr] != '\0') {
      fw_ver_check_error = false;
    }
  }
  if (fw_ver_check_error) {
    aml_printf("[update]ERR(L%d):", 776);
    aml_printf("fw ver is error! Maybe SOC wrong state\n");
    return -777;
  }

  if (id[3] == 0) {
    if (id[4] == 1 && idLen > 5) {
      printf("Need Password...Password check %s\n", id[5] == 1 ? "OK" : "NG");
    }
    if (id[7] && idLen > 7) {
      printf("SupportedPageMap--\t map 6 0x%02x, map 7 0x%02x\n", id[6], id[7]);
    }
  }
  return 0;
}

//----- (000000000040AFAF) ----------------------------------------------------
int update_sub_cmd_get_chipid (AmlUsbRomRW &rom, const char **a2) {
  char id[16];

  int ret = update_sub_cmd_identify_host(rom, 4LL, id);
  if (ret) {
    aml_printf("[update]ERR(L%d):", 847);
    aml_printf("Fail in identifyHost, ret=%d\n", ret);
    return -848;
  }

  if (id[3] && id[3] != 8) {
    aml_printf("[update]ERR(L%d):", 852);
    aml_printf("romStage not bl1/bl2, cannot support chipid\n");
    return -853;
  }

  int idVer = id[1] | (id[0] << 8);
  aml_printf("[update]idVer is 0x%x\n", idVer);

  if (idVer < 0x200 || (idVer > 0x205 && idVer != 0x300)) {
    aml_printf("[update]ERR(L%d):", 890);
    aml_printf("idVer(0x%x) cannot support chipid\n", idVer);
    return -891;
  }

  char chipID[16];

  if (idVer == 0x203) {
    char buf[40];
    aml_printf("[update]get chpid by chip info page\n");
    int actual = Aml_Libusb_get_chipinfo(&rom.device, buf, 0x40, 1, 8000);
    if (actual != 64) {
      aml_printf("[update]ERR(L%d):", 882);
      aml_printf("Fail in get chipinfo, want %d, actual %d\n", 64, actual);
    }
    memcpy(chipID, &buf[20], AML_CHIP_ID_LEN);
  } else {
    unsigned int offset;
    switch (idVer) {
      case 0x200:
      case 0x201:
        offset = 0xD9013C24;
        break;
      case 0x202:
        offset = 0xC8013C24;
        break;
      case 0x204:
        offset = 0xD900D400;
        break;
      case 0x205:
      case 0x300:
        offset = 0xFFFCD400;
        break;
      default:
        offset = 0;
    }
    if (Aml_Libusb_Ctrl_RdWr(&rom.device, offset, chipID, 12, 1, 5000)) {
      aml_printf("[update]ERR(L%d):", 898);
      aml_printf("Fail in read chip id via ReadMem\n");
      return -899;
    }
  }

  printf("ChipID is:0x");
  for (int i = 0; i < AML_CHIP_ID_LEN; ++i) {
    printf("%02x", chipID[i]);
  }
  putchar('\n');
  return 0;
}

int update_sub_cmd_tplcmd (AmlUsbRomRW &rom, const char *tplCmd) {
  unsigned int dataSize;
   char dest[128] = {};

  memcpy(dest, tplCmd, strlen(tplCmd) + 1);
  dest[66] = 1;
  AmlUsbRomRW cmd = {.device = rom
                                   .device,.bufferLen = 68, .buffer = dest, .pDataSize = &dataSize };
  if (AmlUsbTplCmd(&cmd) == 0) {
     char reply[64] = {};
    AmlUsbRomRW cmd = {.device = rom.device, .bufferLen = 64, .buffer = reply};

    for (int retry = 5; retry > 0; --retry) {
      if (AmlUsbReadStatus(&cmd) == 0) {
        printf("reply %s \n", reply);
        return 0;
      }
    }
  }
  puts("ERR: AmlUsbTplCmd failed!");
  return 948;
}

//----- (000000000040B502) ----------------------------------------------------
int update_sub_cmd_mread (AmlUsbRomRW &rom, int argc, const char **argv) {
  if (argc <= 3) {
    update_help();
    return 955;
  }

  const char *storeOrMem = argv[0];
  const char *partition = argv[1];
  const char *filetype = argv[2];
  long readSize = strtoll(argv[3], nullptr, 0);
  if (strcmp("normal", filetype) != 0 || !readSize) {
    aml_printf("Err args in mread: check filetype and readSize\n");
    return 968;
  }

   char buffer[128] = {};
  snprintf((char *) buffer, sizeof(buffer), "upload %s %s %s 0x%lx", storeOrMem,
           partition, filetype, readSize);
  buffer[66] = 1;
  rom.buffer = buffer;
  rom.bufferLen = 68;
  if (AmlUsbBulkCmd(&rom)) {
    aml_printf("ERR: AmlUsbBulkCmd failed!\n");
    return 983;
  }

  rom.bufferLen = readSize;
  if (ReadMediaFile(&rom, argc <= 4 ? nullptr : argv[4], readSize)) {
    aml_printf("ERR: ReadMediaFile failed!\n");
    return 990;
  }

  return 0;
}

//----- (000000000040B721) ----------------------------------------------------
int main (int argc, const char **argv) {
  signed int v6; // ebx
  signed int v7; // ebx
  const char *v8; // rax
  char *v9; // rax
  size_t v14; // rax
  int v15; // [rsp+18h] [rbp-3D8h]
  int a4; // [rsp+1Ch] [rbp-3D4h]
  int dev_no; // [rsp+20h] [rbp-3D0h]
  int v18; // [rsp+24h] [rbp-3CCh]
  int result; // [rsp+28h] [rbp-3C8h]
  signed int cmdArgc; // [rsp+2Ch] [rbp-3C4h]
  int v22; // [rsp+34h] [rbp-3BCh]
  int v23; // [rsp+38h] [rbp-3B8h]
  int v24; // [rsp+3Ch] [rbp-3B4h]
  char v25; // [rsp+40h] [rbp-3B0h]
  const char *s1; // [rsp+48h] [rbp-3A8h]
  const char **cmdArgv; // [rsp+50h] [rbp-3A0h]
  const char *str_dev_no; // [rsp+58h] [rbp-398h]
  FILE *stream; // [rsp+60h] [rbp-390h]
  void *ptr; // [rsp+68h] [rbp-388h]
  const char *cmd; // [rsp+70h] [rbp-380h]
  __int64 v32; // [rsp+78h] [rbp-378h]
  AmlUsbRomRW drv = {}; // [rsp+80h] [rbp-370h]
  std::string a1; // [rsp+E0h] [rbp-310h]
  char scan_mass_storage[4] = {}; // [rsp+140h] [rbp-2B0h]
  char v40; // [rsp+144h] [rbp-2ACh]
  char v41[128]; // [rsp+150h] [rbp-2A0h]
  char dest[512]; // [rsp+1D0h] [rbp-220h]

  aml_init();
  dev_no = -2;
  v15 = 0;
  v18 = 0;
  str_dev_no = nullptr;
  stream = nullptr;
  ptr = nullptr;
  a4 = 0;
  v22 = 0;
  v40 = 0;
  memset(dest, 0, 0x200u);
  result = -1015;
  cmd = argv[1];
  cmdArgv = argv + 3;
  cmdArgc = argc - 3;

  if (argc == 1) {
    update_help();
    return 0;
  }

  if (!strcmp(cmd, "help")) {
    update_help();
    return 0;
  }

  if (!strcmp(cmd, "scan")) {
    if (argc == 2) {
      update_scan(nullptr, 1, -2, &a4, nullptr);
    } else if (argc == 3) {
      str_dev_no = argv[2];
      if (!strncmp(str_dev_no, "mptool", 7uLL)) {
        update_scan(nullptr, 1, -2, &a4, nullptr);
      } else if (!strncmp(str_dev_no, "msdev", 6uLL) &&
                 update_scan(nullptr, 1, 0xFFFFFFFE, &a4, scan_mass_storage) != 0) {
        puts("can not find device");
      }
    }
    return 0;
  }

  if (argc > 2) {
    std::string a1(argv[2]);
    if (a1.compare(0, 3, "dev") == 0) {
      if (a1.length() > 5) {
        aml_printf("[update]ERR(L%d):", 1062);
        aml_printf("devPara(%s) err\n", a1.c_str());
        goto finish;
      }

      dev_no = strtol(a1.substr(3, -1).c_str(), nullptr, 10);
      if (dev_no > 16) {
        aml_printf("[update]ERR(L%d):", 1068);
        aml_printf("dev_no(%d) too large\n", dev_no);
        goto finish;
      }

      cmdArgv = argv + 3;
    } else if (a1.compare(0, 5, "path-") == 0) {
      aml_printf("[update]devPath is [%s]\n", a1.substr(5, -1).c_str());
      drv.device = AmlGetDeviceHandle("WorldCup Device",(char*) a1.substr(5, -1).c_str());
    } else {
      dev_no = 0;
      cmdArgv = argv + 2;
      ++cmdArgc;
    }
  } else {
    dev_no = 0;
    cmdArgc = 0;
  }

  if (!drv.device && update_scan((void**)&drv.device, 0, dev_no, &a4, nullptr) != 0) {
    aml_printf("[update]ERR(L%d):", 1090);
    aml_printf("can not find device\n");
    goto finish;
  }

  if (!drv.device) {
    aml_printf("[update]ERR(L%d):", 1094);
    aml_printf("can not open dev[%d] device, maybe it not exist!\n", dev_no);
    goto finish;
  }

  if (!strcmp(cmd, "run") || !strcmp(cmd, "rreg")) {
    return update_sub_cmd_run_and_rreg(drv, cmd, cmdArgv, cmdArgc);
  }

  if (!strcmp("password", cmd)) {
    result = update_sub_cmd_set_password(drv, cmdArgv, cmdArgc);
    goto finish;
  }

  if (!strcmp("chipinfo", cmd)) {
    if (cmdArgc <= 0) {
      aml_printf("[update]ERR(L%d):", 1112);
      aml_printf("paraNum(%d) too small for chipinfo\n", argc);
    } else {
      result = update_sub_cmd_get_chipinfo(drv, cmdArgv, cmdArgc);
    }
    goto finish;
  }

  if (!strcmp(cmd, "chipid")) {
    result = update_sub_cmd_get_chipid(drv, cmdArgv);
    goto finish;
  }

  if (!strcmp(cmd, "write") || !strcmp(cmd, "read") || !strcmp(cmd, "wreg") ||
      !strcmp(cmd, "dump") || !strcmp(cmd, "boot") || !strcmp(cmd, "cwr") ||
      !strcmp(cmd, "write2")) {
    result = update_sub_cmd_read_write(drv, cmd, cmdArgv, cmdArgc);
    goto finish;
  }

  if (!strcmp(cmd, "msdev") || !strcmp(cmd, "msget") || !strcmp(cmd, "msset")) {
    if (!strcmp(cmd, "msset")) {
      dev_no = 0;
      if (argc == 4) {
        str_dev_no = argv[2];
        if (strlen(str_dev_no) != 4 || strncmp(str_dev_no, "dev", 3) != 0) {
          puts("please input dev_no like 'dev0'");
          goto finish;
        }
        dev_no = str_dev_no[3] - 48;
      } else if (argc != 3) {
        update_help();
        goto finish;
      }
      s1 = argv[argc - 1];
      if (argc == 3 && !strncmp(s1, "dev", 3)) {
        char yesno;
        printf("if you want input cmd \"%s\" (Y/N): ", s1);
        scanf("%c", &yesno);
        if (strncasecmp(&yesno, "y", 1) != 0) {
          goto finish;
        }
      }
    } else if (argc == 2) {
      dev_no = 0;
    } else {
      if (argc != 3) {
        update_help();
        return 0;
      }
      str_dev_no = argv[2];
      if (strlen(str_dev_no) != 4 || strncmp(str_dev_no, "dev", 3)) {
        puts("please input dev_no like 'dev0'");
        goto finish;
      }
      dev_no = str_dev_no[3] - 48;
    }
    if (update_scan(nullptr, 0, dev_no, &a4, scan_mass_storage) <= 0) {
      puts("can not find device");
    } else {
      printf(" %s\n", scan_mass_storage);
      if (!strcmp(cmd, "msget")) {
        memset(v41, 0, sizeof(v41));
        if (AmlGetUpdateComplete(nullptr) != 0) {
          puts("AmlGetUpdateComplete error");
        } else {
          // printf("AmlGetUpdateComplete = %s  %lu\n", v41, rom.device);
          printf("AmlGetUpdateComplete = %s  %lu\n", v41, 0);
        }
      }
      if (!strcmp(cmd, "msset")) {
        strlen(s1);
        if (AmlSetFileCopyCompleteEx(nullptr) != 0) {
          printf("AmlSetFileCopyCompleteEx error--%s\n", s1);
        } else {
          printf("AmlSetFileCopyCompleteEx success--%s\n", s1);
        }
      }
    }
    goto finish;
  }

  if (!strcmp(cmd, "identify")) {
    update_sub_cmd_identify_host(drv, cmdArgc ? atoi(cmdArgv[0]) : 4, nullptr);
    goto finish;
  }

  if (!strcmp(cmd, "reset")) {
    if (AmlResetDev(&drv)) {
      aml_printf("[update]ERR(L%d):", 1216);
      aml_printf("ERR: get info from device failed\n");
    } else {
      aml_printf("[update]reset succesful\n");
    }
    goto finish;
  }

  if (!strcmp(cmd, "tplcmd")) {
    update_sub_cmd_tplcmd(drv, argv[argc - 1]);
    goto finish;
  }

  if (!strcmp(cmd, "burn")) {
    if (AmlUsbburn(drv.device, "d:/u-boot.bin", 0x49000000, "nand", 0x12345678, 0x2000,
                   0) < 0) {
      puts("ERR: get info from device failed");
    }
    goto finish;
  }

  if (!strcmp(cmd, "tplstat")) {
    memset(v41, 0, 0x40u);
    AmlUsbRomRW rom = {.device = drv.device, .bufferLen = 64, .buffer = v41};
    if (AmlUsbReadStatus(&rom) == 0) {
      printf("reply %s \n", v41);
    } else {
      puts("ERR: AmlUsbReadStatus failed!");
    }
    goto finish;
  }

  if (!strcmp(cmd, "skscan") || !strcmp(cmd, "skgsn") || !strcmp(cmd, "skssn")) {
    char *v38[8] = {};
    aml_scan_init();
    v18 = !strcmp(cmd, "skssn");
    if (v18 + 2 == argc) {
      dev_no = 0;
    } else if (v18 + 3 == argc) {
      str_dev_no = argv[2];
      if (strncmp(str_dev_no, "dev", 3uLL) != 0) {
        puts("please input dev_no like 'dev0'");
        goto finish;
      }
      dev_no = str_dev_no[3] - 48;
    } else if (strcmp(cmd, "skscan") != 0) {
      update_help();
      goto finish;
    }

    printf("发现 %d 个设备\n", aml_scan_usbdev(v38));
    if (!strcmp(cmd, "skgsn")) {
      v24 = aml_get_sn(v38[dev_no], dest);
      if (v24 == 0) {
        puts("此设备没有写过SN ");
      } else if (v24 == -1) {
        printf("没有此设备[设备号%d]\n", dev_no);
      } else if (v24 > 0) {
        dest[v24] = 0;
        printf("此设备的SN :%s\n", dest);
      }
    } else if (!strcmp(cmd, "skssn")) {
      v24 = aml_set_sn(v38[dev_no], (char *) argv[argc - 1]);
      if (v24 == 0) {
        puts("写此设备的SN失败!!!");
      } else if (v24 == -1) {
        printf("没有此设备[设备号%d]\n", dev_no);
      } else if (v24 > 0) {
        puts("写此设备的SN成功");
      }
    }
    aml_scan_close();
    goto finish;
  }

  if (!strcmp(cmd, "mwrite")) {
    if (argc > 5) {
      result = do_cmd_mwrtie(cmdArgv, cmdArgc, drv);
    } else {
      update_help();
    }
    goto finish;
  }

  if (!strcmp(cmd, "partition")) {
    if (argc > 3) {
      unsigned int partitionArgc = 4;
      const char *v38[8] = {cmdArgv[1], "store", cmdArgv[0],
                            cmdArgc <= 2 ? is_file_format_sparse(cmdArgv[1]) ? "sparse"
                                                                             : "normal"
                                         : cmdArgv[2]};
      if (cmdArgc > 3) {
        v38[4] = cmdArgv[3];
        ++partitionArgc;
      }
      result = do_cmd_mwrtie(v38, partitionArgc, drv);
    } else {
      update_help();
    }
    goto finish;
  }

  if (!strcmp(cmd, "bulkcmd")) {
    s1 = argv[argc - 1];
    memcpy(dest, s1, strlen(s1));
    memset(v41, 0, sizeof(v41));
    //v32 = nullptr;
    v23 = 5;
    v14 = strlen(dest);
    memcpy(v41, dest, v14);
    v41[66] = 1;
    AmlUsbRomRW rom = {.device = drv.device, .bufferLen = 68, .buffer = v41, .pDataSize = (unsigned int *) &v25};
    result = 0;
    if (AmlUsbBulkCmd(&rom) != 0) {
      puts("ERR: AmlUsbBulkCmd failed!");
      result = -1363;
    }
    goto finish;
  }

  if (!strcmp(cmd, "down")) {
    if (argc == 4) {
      str_dev_no = argv[2];
      if (strlen(str_dev_no) != 4 || strncmp(str_dev_no, "dev", 3uLL)) {
        puts("please input dev_no like 'dev0'");
        goto finish;
      }
      dev_no = str_dev_no[3] - 48;
    } else {
      if (argc != 3) {
        update_help();
        goto finish;
      }
      dev_no = 0;
    }

    str_dev_no = argv[3];
    if (update_scan((void **) &drv.device, 0, 0, &a4, nullptr) <= 0) {
      puts("can not find device");
    } else if (drv.device) {
      if (WriteMediaFile(&drv, str_dev_no) != 0) {
        aml_printf("ERR:write data to media failed\n");
      }
    } else {
      printf("ERR: can not open dev%d device, maybe it not exist!\n", dev_no);
    }
    goto finish;
  }

  if (!strcmp(cmd, "mread")) {
    result = update_sub_cmd_mread(drv, cmdArgc, cmdArgv);
    goto finish;
  }

  update_help();

  finish:
  if (stream) {
    fclose(stream);
  }
  if (ptr) {
    free(ptr);
  }
  if (drv.device) {
    drv.device = nullptr;
  }
  aml_uninit();
  return result;
}


//----- (000000000040CD9F) ----------------------------------------------------
int WriteMediaFile (AmlUsbRomRW *rom, const char *filename) {
  // rax
  unsigned int bufferLen; // eax MAPDST
  unsigned int transferPercentage; // ST2C_4
  // eax MAPDST
  // eax
  int address; // [rsp+14h] [rbp-6Ch]
  // [rsp+20h] [rbp-60h]
  int startTime; // [rsp+24h] [rbp-5Ch]
  unsigned int v13; // [rsp+28h] [rbp-58h]
  unsigned int
  v14; // [rsp+30h] [rbp-50h]
  long long transferSize; // [rsp+40h] [rbp-40h]
  long long
  v17; // [rsp+48h] [rbp-38h]
  void *buffer; // [rsp+58h] [rbp-28h]
  int
  v20; // [rsp+60h] [rbp-20h]

  transferSize = 0;
  buffer = nullptr;
  v14 = 0;
  v20 = 0;
  address = 0;

  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    aml_printf("Open file %s failed\n", filename);
    return -1;
  }

  fseeko64(fp, 0LL, 2);
  off_t fileSize = ftello(fp);
  v17 = 0;
  int v3 = fileSize*41 /1600;
  if (v3 <= 0x3FFFFF) {
    v3 = 0x400000;
  }
  fseek(fp, 0, 0);
  startTime = timeGetTime();
  buffer = malloc(0x10000);
  while (fileSize) {
    bufferLen = fileSize > 0x10000 ? 0x10000 : fileSize;
    fread(buffer, 1uLL, bufferLen, fp);
    rom->buffer = (char *) buffer;
    rom->bufferLen = bufferLen;
    rom->pDataSize = (unsigned int *) &v14;
    rom->address = address;
    if (AmlWriteMedia(rom) != 0) {
      aml_printf("AmlWriteMedia failed\n");
      break;
    }
    transferSize += bufferLen;
    fileSize -= bufferLen;
    ++address;
    v13 = transferSize - v17;
    if (address == 1) {
      puts("Downloading....");
    }
    if (v13 >= v3) {
      transferPercentage = 100 * transferSize / fileSize;
      printf("[%3d%%/%5uMB]\r", transferPercentage, (unsigned int) (transferSize >> 20));
      fflush(stdout);
      v17 = v3 * (signed int) transferPercentage;
    }
  }
  aml_printf("[update]Cost time %dSec            \n",
             (timeGetTime() - startTime) / 1000u);
  aml_printf("[update]Transfer size 0x%llxB(%lluMB)\n", transferSize, transferSize >> 20);
  free(buffer);
  fclose(fp);
  return fileSize ? -1 : 0;
}

//----- (000000000040D0B1) ----------------------------------------------------
int ReadMediaFile (AmlUsbRomRW *rom, const char *filename, long size) {
  // ebx MAPDST
  unsigned int bufferLen; // eax
  bool v7a;
  int v7b;
  size_t dataSize; // [rsp+30h] [rbp-90h]
  FILE *fp; // [rsp+38h] [rbp-88h]
  unsigned int
  offset; // [rsp+48h] [rbp-78h]
  char *buffer; // [rsp+50h] [rbp-70h]
  // [rsp+58h] [rbp-68h]
  DownloadProgressInfo info(size, "Uploading"); // [rsp+60h] [rbp-60h]

  fp = nullptr;
  offset = 0;
  buffer = nullptr;
  dataSize = 0;
  v7b = 0;
  v7a = filename != nullptr;
  if (v7a) {
    fp = fopen(filename, "wb");
    if (!fp) {
      aml_printf("Open file %s failed\n", filename);
      return -1;
    }
  }

  buffer = (char *) malloc(0x10000uLL);
    while (size) {
      if (size > 0x10000) {
        bufferLen = 0x10000;
      } else {
        bufferLen = size;
      }
      rom->buffer = buffer;
      rom->bufferLen = bufferLen;
      rom->pDataSize = (unsigned int *) &dataSize;
      if ((unsigned int) AmlReadMedia(rom) != 0) {
        aml_printf("AmlReadMedia failed\n");
        break;
      }
      if (v7a) {
        fwrite(buffer, 1uLL, dataSize, fp);
        info.update_progress(dataSize);
      } else {
        _print_memory_view(buffer, dataSize, offset);
      }
      offset += dataSize;
      size -= dataSize;
      ++v7b;
    }
  if (buffer) {
      free(buffer);
    }
    if (fp) {
      fclose(fp);
    }
  return size ? -1 : 0;
}


//----- (000000000040D472) ----------------------------------------------------
DownloadProgressInfo::DownloadProgressInfo (long long total_, const char *prompt_) {
  int v5; // rdx

  nBytes = total_;
  percentage_0 = 0;
  percentage_100 = 100;
  percentage = 0;
  nBytesDownloaded = 0;
  percentage_100_remain = percentage_100 - percentage_0;
  memset(prompt, 0, 0x10uLL);
  memcpy(prompt, prompt_, 0xFuLL);
  if (nBytes < (unsigned int) percentage_100_remain) {
    nBytes = (unsigned int) percentage_100_remain;
  }
  total_div_100 = nBytes / (unsigned int) percentage_100_remain;
  v5 = 0x400000;
  if (total_div_100 >= 0x400000) {
    *(short *) &(v5) = total_div_100;
  }
  max_4k_total_div_100 = v5;
  max_1_4k_div_total_mul_100 = (unsigned int) max_4k_total_div_100 / total_div_100;
}

//----- (000000000040D58A) ----------------------------------------------------
int DownloadProgressInfo::update_progress (unsigned int dataLen) {
  nBytesDownloaded += dataLen;
  if (nBytesDownloaded == dataLen && percentage_0 == percentage) {
    startTime = timeGetTime();
  }
  if ((unsigned int) max_4k_total_div_100 > nBytesDownloaded) {
    return 0LL;
  }
  percentage += nBytesDownloaded / (unsigned int) total_div_100;
  nBytesDownloaded %= total_div_100;
  printf("%s %%%d\r", prompt, percentage);
  fflush(stdout);
  if ((unsigned int) (max_1_4k_div_total_mul_100 + percentage) >= percentage_100) {
    printf("\b\b\b\b\b\b\b\b\b\r");
    printf("[%s]OK:<%ld>MB in <%u>Sec\n", prompt, nBytes >> 20,
           (timeGetTime() - startTime) / 1000);
  }
  return 0LL;
}