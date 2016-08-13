#include <stdio.h>
#include <string.h>
#include <sys/iosupport.h>

#include <3ds.h>

#include <py3DS/Python.h>

#define min(a,b) ((a)<(b)?(a):(b))
#define hangmacro() \
({\
    puts("Press a key to exit...");\
    while(aptMainLoop())\
    {\
        hidScanInput();\
        if(hidKeysDown())\
        {\
            goto killswitch;\
        }\
        gspWaitForVBlank();\
    }\
})

static vu32 running = 0;
static int exitcode = 0;

static SwkbdState swkbd;
static char* kbdbuf;
static size_t kbdoffs = 0;
static size_t kbdrem = 0;
static vu32 kbdwait = 0;
static int kbdTRIM = 0;

ssize_t stdread(struct _reent* r, int fd, char* ptr, size_t len)
{
    while(aptMainLoop() && !kbdrem)
    {
        if(kbdTRIM)
        {
            kbdTRIM = 0;
            return -1;
        }
        kbdwait = 1;
        do
        {
            u32* vbuf = gfxGetFramebuffer(GFX_BOTTOM, 0, NULL, NULL);
            u32* bakup = malloc(16 * 16 * sizeof(u32));
            int i,j;
            for(i = 0; i != 16; i++)
            {
                for(j = 0; j != 16; j++)
                {
                    bakup[(i * 16) + j] = vbuf[(i * 240) + j];
                    vbuf[(i * 240) + j] = 0x1AEAAEFF;
                }
            }
            gfxFlushBuffers();
            
            u32 kDown, kHeld;
            touchPosition tp;
            
            while(aptMainLoop())
            {
                hidScanInput();
                kDown = hidKeysDown();
                kHeld = hidKeysHeld();
                if(hidKeysHeld() & KEY_TOUCH) hidTouchRead(&tp);
                
                if(kHeld & KEY_SELECT)
                {
                    running = 0;
                    break;
                }
                
                if((kDown & KEY_TOUCH) && tp.px <= 16 && tp.py >= 224)
                {
                    break;
                }
                
                gspWaitForVBlank();
            }
            
            for(i = 0; i != 16; i++)
            {
                for(j = 0; j != 16; j++)
                {
                    vbuf[(i * 240) + j] = bakup[(i * 16) + j];
                }
            }
            gfxFlushBuffers();
            
            free(bakup);
        }
        while(0);
        kbdwait = 0;
        
        if(!running) return -1;
        
        swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 3, 0x7FFF);
        swkbdSetButton(&swkbd, SWKBD_BUTTON_MIDDLE, "Buffer", 1);
        swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "Enter", 1);
        swkbdSetFeatures(&swkbd, SWKBD_MULTILINE);
        SwkbdButton btn = swkbdInputText(&swkbd, kbdbuf, 0xFFFF);
        if(btn == SWKBD_BUTTON_LEFT || btn == SWKBD_BUTTON_NONE)
        {
            continue;
        }
        kbdTRIM = btn == SWKBD_BUTTON_RIGHT;
        strcat(kbdbuf, "\n");
        kbdoffs = 0;
        kbdrem = strlen(kbdbuf);
    }
    
    if(!running) return -1;
    
    size_t remaining = min(len, kbdrem);
    //if(kbdTRIM)
    if(1)
    {
        size_t trimsize = strchr(kbdbuf + kbdoffs, '\n') - (kbdbuf + kbdoffs) + 1;
        if(trimsize < remaining) remaining = trimsize;
    }
    memcpy(ptr, kbdbuf + kbdoffs, remaining);
    printf("%.*s", remaining, kbdbuf + kbdoffs);
    kbdrem -= remaining;
    kbdoffs += remaining;
    
    return remaining;
}

int main()
{

    gfxInit(GSP_RGBA8_OES, GSP_RGBA8_OES, false);
    gfxSetDoubleBuffering(GFX_BOTTOM, false);
    consoleInit(GFX_TOP, NULL);

    u32 kDown, kHeld, kUp;
    touchPosition tp;

    puts("hi");

    osSetSpeedupEnable(1);
    
    hidScanInput();
    kDown = hidKeysDown();
    kHeld = hidKeysHeld();
    kUp   = hidKeysUp();
    
    kbdbuf = malloc(0x10000 * sizeof(char));
    static devoptab_t std_in =
    {
        "stdin",
        0,
        NULL,
        NULL,
        NULL,
        stdread,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
    };
    devoptab_list[STD_IN] = &std_in;

    Py_FrozenFlag = 1;
    Py_NoSiteFlag = 1;
    Py_InspectFlag = 1;
    Py_InteractiveFlag = 1;
    if(kHeld & KEY_A)
    {
        puts("-- Debugging activated");
        Py_VerboseFlag = 1;
        Py_DebugFlag = 1;
    }
    Py_SetProgramName("<stdin>");
    Py_Initialize();
    
    Result res = romfsInit();
    if(res)
    {
        printf("Error initializing ROMFS: %08X\n", res);
        hangmacro();
    }
    u32* soc_shared = memalign(0x1000, 0x100000);
    if(soc_shared)
    {
        res = socInit(soc_shared, 0x100000);
    }
    else
    {
        printf("Failed to allocate networking memory!\nSockets won't function!");
    }
    
    running = 1;
    
    PyRun_SimpleString("import sys");
    PyRun_SimpleString("sys.path=['romfs:/python.zip']");
    PyRun_SimpleString("import zipimport");

    do
    {
        FILE* pyinit = fopen("romfs:/init.py", "rb");
        if(pyinit > 0)
        {
            puts("> __init__");
            exitcode = PyRun_SimpleFile(pyinit, "romfs:/init.py");
            fclose(pyinit);
        }
    }
    while(0);
    
    u32 isnew3ds;APT_CheckNew3DS(&isnew3ds);
    printf("Python %s on arm-none-eabi-%cTR\n", Py_GetVersion(), isnew3ds ? 'K' : 'C');
    
    if(!exitcode && !(hidKeysHeld() & KEY_SELECT))
    while (aptMainLoop())
    {
        hidScanInput();
        kDown = hidKeysDown();
        kHeld = hidKeysHeld();
        kUp   = hidKeysUp();
        if(kHeld & KEY_TOUCH) hidTouchRead(&tp);

        if(exitcode || (kHeld & KEY_SELECT))
        {
            running = 0;
            break;
        }
        
        exitcode = PyRun_SimpleFile(stdin, "<stdin>");
        
        gfxFlushBuffers();
        gspWaitForVBlank();
    }

    killswitch:
    
    if(exitcode)
    {
        printf("Error running program, exit code %08X\n", exitcode);
        exitcode = 0;
        hangmacro();
    }
    
    puts("-- Freeing Python interpreter");
    
    Py_Finalize();
    socExit();
    romfsExit();
    gfxExit();
    return 0;
}
