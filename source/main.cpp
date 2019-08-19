#include <string.h>
#include <stdio.h>
#include <fstream>
#include <sys/stat.h>
#include <filesystem>
#include <ctime>

#include <mbedtls/md5.h>
#include <switch.h>

void md5HashFromFile(std::string filename, unsigned char* out)
{
    FILE *inFile = fopen (filename.c_str(), "rb");
    mbedtls_md5_context md5Context;
    int bytes;
    u64 bufSize = 500000;
    unsigned char data[bufSize];

    if (inFile == NULL)
    {
      printf ("\nThe data.arc file can not be opened.");
      return;
    }
    mbedtls_md5_init (&md5Context);
    mbedtls_md5_starts_ret(&md5Context);

    fseek(inFile, 0, SEEK_END);
    u64 size = ftell(inFile);
    fseek(inFile, 0, SEEK_SET);
    u64 sizeRead = 0;
    int percent = 0;
    while ((bytes = fread (data, 1, bufSize, inFile)) != 0)
    {
      mbedtls_md5_update_ret (&md5Context, data, bytes);
      sizeRead += bytes;
      percent = sizeRead * 100 / size;
      printf("\x1b[s\n%d/100\x1b[u", percent);
      consoleUpdate(NULL);
    }
    fclose(inFile);
    mbedtls_md5_finish_ret (&md5Context, out);
    return;
}

bool isServiceRunning(const char *serviceName) {
  Handle handle;
  bool running = R_FAILED(smRegisterService(&handle, serviceName, false, 1));

  svcCloseHandle(handle);

  if (!running)
    smUnregisterService(serviceName);

  return running;
}

std::string getCFW()
{
  if (isServiceRunning("tx"))
    return "sxos";
  else if (isServiceRunning("rnx"))
    return "reinx";
  return "atmosphere";
}

u64 runningTID()
{
  u64 pid = 0;
  u64 tid = 0;
  pmdmntInitialize();
  pminfoInitialize();
  pmdmntGetApplicationPid(&pid);
  pminfoGetTitleId(&tid, pid);
  pminfoExit();
  pmdmntExit();
  return tid;
}

void copy(const char* from, const char* to, bool exfat = false)
{
    //const u64 fat32Max = 0xFFFFFFFF;
    //const u64 splitSize = 0xFFFF0000;
    const u64 smashTID = 0x01006A800016E000;
    u64 bufSize = 0x0F0F0F0F;

    AppletType at = appletGetAppletType();
    if (at != AppletType_Application && at != AppletType_SystemApplication)
    {
      printf("\nNo applet mode >:(\nYou must override Smash for this application to work properly.\nHold 'R' while launching Smash to do so.");
      return;
    }
    if(runningTID() != smashTID)
    {
      printf("\nYou must override Smash for this application to work properly.\nHold 'R' while launching Smash to do so.");
      return;
    }
    FILE* source = fopen(from, "rb");
    if(source == nullptr)
    {
      printf ("\nThe romfs could not be read.");
	    return;
    }
    fseek(source, 0, SEEK_END);
    u64 size = ftell(source);
    fseek(source, 0, SEEK_SET);

    if(std::filesystem::space(to).available < size)
    {
      printf("\nNot enough storage space on the SD card.");
      return;
    }
    std::string folder(to);
    folder = folder.substr(0, folder.find_last_of("/"));
    //todo: do this better
    if(!std::filesystem::exists(folder))
    {
      mkdir(folder.substr(0, folder.find_last_of("/")).c_str(), 0744);
      mkdir(folder.c_str(), 0744);
    }
    if(!exfat)
      fsdevCreateFile(to, 0, FS_CREATE_BIG_FILE);

    FILE* dest = fopen(to, "wb");
    if(dest == nullptr)
    {
      printf("\nCould not open the destination file.");
      return;
    }

    char* buf = new char[bufSize];
    u64 sizeWritten = 0;
    int percent = 0;
    size_t ret;
    //bool FSChecked = false;

    if(size == 0)
      printf("\nThere might be a problem with the data.arc file on your SD card. Please remove the file manually.");
    while(sizeWritten < size)
    {
      if(sizeWritten + bufSize > size)
      {
        delete[] buf;
        bufSize = size-sizeWritten;
        buf = new char[bufSize];
      }

      /* Broken automatic fat32 detection
      if(!FSChecked && sizeWritten > fat32Max)
      {
        // Write one over the limit
        // delete[] buf;
        // u64 tempBufSize = (fat32Max - sizeWritten) + 1;
        // buf = new char[tempBufSize];
        // source.read(buf, tempBufSize);
        // dest.write(buf, tempBufSize);
        // delete[] buf;
        // buf = new char[bufSize];

        if(dest.bad())  // assuming this is caused by fat32 size limit
        {
          dest.close();
          printf("\x1b[30;2HFat32 detected, making split file");
          std::filesystem::resize_file(to, splitSize);
          std::rename(to, (std::string(to) + "temp").c_str());
          mkdir(to, 0744);
          std::rename((std::string(to) + "temp").c_str(), (std::string(to) + "/00").c_str());

          //fsdevCreateFile((std::string(to) + "/01").c_str(), 0, 0);

          fsdevSetArchiveBit(to);

          // re-write chopped off peice next time
          source.seekg(splitSize);
          if(source.fail())
            printf("\nsource failed after seeking");

          // hopefully that was the only problem with the stream
          dest.clear();
          dest.open(to);
          dest.seekp(splitSize);
          //printf("\nsplit Pos:%lld\nsplitSize:%ld", (long long int)dest.tellp(), splitSize);
          if(dest.fail())
            printf("\ncould not reopen destination file");

          sizeWritten = splitSize;

          source.seekg(splitSize);
          printf("\nafter split: dest pos: %lld, source pos: %lld", (long long int)dest.tellp(), (long long int)source.tellg());
        }
        FSChecked = true;
      }
      */

      fread(buf, sizeof(char), bufSize, source);
      ret = fwrite(buf, sizeof(char), bufSize, dest);
      if(ret != bufSize)
      {
        printf("\nSomething went wrong!");
        return;
      }
      sizeWritten += bufSize;
      percent = sizeWritten * 100 / size;
      printf("\x1b[s\n%d/100\x1b[u", percent);
      //printf("\x1b[20;2Hdest pos: %lld, source pos: %lld", (long long int)dest.tellp(), (long long int)source.tellg());  // Debug log
      //printf("\x1b[22;2H%lu/%lu", sizeWritten, size);  // Debug log
      consoleUpdate(NULL);
    }
    fclose(source);
    fclose(dest);
    delete[] buf;
}
int main(int argc, char **argv)
{
    consoleInit(NULL);

    printf("\n\x1b[1;32mData Arc Dumper\x1b[0m");
    printf("\n\nPress 'A' to dump as a split file (FAT32)");
    printf("\nPress 'B' to dump as a single file (exFAT)");
    printf("\nPress 'X' to generate an MD5 hash of the file");

    bool done = false;
    bool exfat = false;
    std::string outPath = "sdmc:/" + getCFW() + "/titles/01006A800016E000/romfs/data.arc";
    const int MD5_DIGEST_LENGTH = 16;

    while(appletMainLoop())
    {
        hidScanInput();
        u64 kDown = hidKeysDown(CONTROLLER_P1_AUTO);
        if (kDown & KEY_PLUS) break; // break in order to return to hbmenu
        if (kDown & KEY_X)
        {
          if(std::filesystem::exists(outPath))
          {
            printf("\nBeginning hash generation...");
            consoleUpdate(NULL);
            unsigned char out[MD5_DIGEST_LENGTH];
            u64 startTime = std::time(0);
            // Should I block home button here too?
            appletSetMediaPlaybackState(true);
            md5HashFromFile(outPath, out);
            appletSetMediaPlaybackState(false);
            u64 endTime = std::time(0);
            printf("\nmd5:");
            for(int i = 0; i < MD5_DIGEST_LENGTH; i++) printf("%02x", out[i]);
            printf("\nHashing took %.2f minutes", (float)(endTime - startTime)/60);
          }
          else
          {
            printf("\nNo data.arc file found on the SD card.");
          }
        }
        if (kDown & KEY_B && !done) exfat = true;
        if ((kDown & KEY_A || kDown & KEY_B) && !done)
        {
          printf("\nBeginning the dumping process...");
          consoleUpdate(NULL);
          u64 startTime = std::time(0);
          remove(outPath.c_str());
          romfsMountFromCurrentProcess("romfs");
          appletBeginBlockingHomeButton(0);
          appletSetMediaPlaybackState(true);
          copy("romfs:/data.arc", outPath.c_str(), exfat);
          appletEndBlockingHomeButton();
          appletSetMediaPlaybackState(false);
          romfsUnmount("romfs");
          u64 endTime = std::time(0);
          done = true;  // So you don't accidentally dump twice
          printf("\nCompleted in %.2f minutes.", (float)(endTime - startTime)/60);
          printf("\nOptional: Press 'X' generate an MD5 hash of the file");
        }
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return 0;
}
