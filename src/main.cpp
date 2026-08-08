#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <libmc.h>
#include <libcdvd.h>
#include <iopheap.h>
#include <iopcontrol.h>
#include <smod.h>
#include <audsrv.h>
#include <sys/stat.h>

#include <dirent.h>

#include <sbv_patches.h>
#include <smem.h>

#include "include/graphics.h"
#include "include/sound.h"
#include "include/luaplayer.h"
#include "include/pad.h"

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <fileio.h>

extern "C"{
#include <libds34bt.h>
#include <libds34usb.h>
}

extern char bootString[];
extern unsigned int size_bootString;

#define IMPORT_BIN2C(_T) \
    extern unsigned char _T[]; \
    extern unsigned int size_##_T

IMPORT_BIN2C(iomanX_irx);
IMPORT_BIN2C(fileXio_irx);
IMPORT_BIN2C(sio2man_irx);
IMPORT_BIN2C(mcman_irx);
IMPORT_BIN2C(mcserv_irx);
IMPORT_BIN2C(padman_irx);
IMPORT_BIN2C(libsd_irx);
IMPORT_BIN2C(cdvd_irx); // <-- ADDED: Hardware CD/DVD driver
IMPORT_BIN2C(cdfs_irx);
IMPORT_BIN2C(usbd_irx);
IMPORT_BIN2C(bdm_irx);
IMPORT_BIN2C(bdmfs_fatfs_irx);
IMPORT_BIN2C(usbmass_bd_irx);
IMPORT_BIN2C(audsrv_irx);
IMPORT_BIN2C(ds34usb_irx);
IMPORT_BIN2C(ds34bt_irx);

#ifdef POWERPC_UART
IMPORT_BIN2C(ppctty_irx);
#endif

char boot_path[255];

void initMC(void)
{
   int ret;
   int mc_Type, mc_Free, mc_Format;

   printf("initMC: Initializing Memory Card\n");

   ret = mcInit(MC_TYPE_XMC);
   
   if( ret < 0 ) {
    printf("initMC: failed to initialize memcard server.\n");
   } else {
       printf("initMC: memcard server started successfully.\n");
   }
   
   mcGetInfo(0, 0, &mc_Type, &mc_Free, &mc_Format); 
   mcSync(MC_WAIT, NULL, &ret);
}

// Function to properly initialize CD/DVD hardware and check disc status
void initCDVD(void)
{
    printf("initCDVD: Initializing CD/DVD Subsystem...\n");
    
    // Fixed: SCECdINIT initializes RPC services (SCECdINoD disables it!)
    sceCdInit(SCECdINIT);
    sceCdMmode(SCECdCD); // Set media mode to standard

    // Wait until disc drive status is ready (max 3 seconds)
    int retries = 300;
    int disc_stat;

    while (retries > 0) {
        disc_stat = sceCdGetDiskType();
        if (disc_stat != SCECdNODISC && disc_stat != SCECdDETCT) {
            break; // Disc detected and ready
        }
        nopdelay();
        usleep(10000); // 10ms wait
        retries--;
    }

    if (disc_stat == SCECdNODISC) {
        printf("initCDVD: No disc found in drive.\n");
    } else {
        printf("initCDVD: Disc ready. Media type code: 0x%X\n", disc_stat);
        sceCdDiskReady(0); // Spindown/Readiness call
    }
}

#ifdef DONT_LOAD_FILEXIO_ON_HOST_DEVICE
int HAVE_FILEXIO = 1;
#else
int HAVE_FILEXIO = 0;
#endif

#define LOAD_IRX(_irx, argc, arglist) \
    ID = SifExecModuleBuffer(&_irx, size_##_irx, argc, arglist, &RET); \
    printf("%s: id:%d, ret:%d\n", #_irx, ID, RET)
#define LOAD_IRX_NARG(_irx) LOAD_IRX(_irx, 0, NULL)

int main(int argc, char * argv[])
{
    int ID, RET;
    const char * errMsg;

    #ifdef RESET_IOP  
    SifInitRpc(0);
    while (!SifIopReset("", 0)){};
    while (!SifIopSync()){};
    SifInitRpc(0);
    #endif
    
    printf("Installing SBV Patches...\n");
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
    sbv_patch_fileio();

#ifdef POWERPC_UART
    LOAD_IRX_NARG(ppctty_irx);
#endif

#ifdef DONT_LOAD_FILEXIO_ON_HOST_DEVICE
    DIR *directorytoverify;
    directorytoverify = opendir("host:.");
    if (directorytoverify==NULL) {
#endif
        LOAD_IRX_NARG(iomanX_irx);
        LOAD_IRX_NARG(fileXio_irx);
        fileXioInit();
        if (ID > 0 && RET != 1) HAVE_FILEXIO = 1;
#ifdef DONT_LOAD_FILEXIO_ON_HOST_DEVICE
        closedir(directorytoverify);
    }
#endif
  
    LOAD_IRX_NARG(sio2man_irx);
    LOAD_IRX_NARG(mcman_irx);
    LOAD_IRX_NARG(mcserv_irx);
    printf("Initialize mcserv\n");
    initMC();

    LOAD_IRX_NARG(padman_irx);
    LOAD_IRX_NARG(libsd_irx);
    LOAD_IRX_NARG(audsrv_irx);

    // load USB modules    
    LOAD_IRX_NARG(usbd_irx);

    int ds3pads = 1;
    LOAD_IRX(ds34usb_irx, 4, (char *)&ds3pads);
    LOAD_IRX(ds34bt_irx, 4, (char *)&ds3pads);
    printf("starting ds34 RPCs...\n");
    ds34usb_init();
    ds34bt_init();

    LOAD_IRX_NARG(bdm_irx);
    LOAD_IRX_NARG(bdmfs_fatfs_irx);
    LOAD_IRX_NARG(usbmass_bd_irx);
    
    // --- CD/DVD HARDWARE AND FILESYSTEM INIT ---
    LOAD_IRX_NARG(cdvd_irx); // Load low-level hardware IRX first
    LOAD_IRX_NARG(cdfs_irx); // Load filesystem IRX
    initCDVD();              // Initialize RPC & drive status

    // Wait until USB mass storage device is ready
    struct stat buffer;
    int ret = -1;
    int retries = 50;

    while(ret != 0 && retries > 0)
    {
        ret = stat("mass:/", &buffer);
        nopdelay();
        retries--;
    }
    
    // graphics (gsKit)
    initGraphics();
    pad_init();

    // set base path luaplayer
    getcwd(boot_path, sizeof(boot_path));

    printf("boot path : %s\n", boot_path);
    dbgprintf("boot path : %s\n", boot_path);
    
    while (1)
    {
        if (argc < 2) {
            errMsg = runScript(bootString, true); 
        } else {
            errMsg = runScript(argv[1], false);
        }   

        init_scr();

        if (errMsg != NULL)
        {
            scr_setfontcolor(0x0000ff);
            sleep(1);
            scr_clear();
            scr_setXY(5, 2);
            scr_printf("Enceladus ERROR!\n");
            scr_printf(errMsg);
            puts(errMsg);
            scr_printf("\nPress [start] to restart\n");
            while (!isButtonPressed(PAD_START)) {
                sleep(1);
            }
            scr_setfontcolor(0xffffff);
        }
    }

    return 0;
}
