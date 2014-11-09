/******************************************************************************\
* Project:  Module Subsystem Interface to SP Interpreter Core                  *
* Authors:  Iconoclast                                                         *
* Release:  2014.10.09                                                         *
* License:  CC0 Public Domain Dedication                                       *
*                                                                              *
* To the extent possible under law, the author(s) have dedicated all copyright *
* and related and neighboring rights to this software to the public domain     *
* worldwide. This software is distributed without any warranty.                *
*                                                                              *
* You should have received a copy of the CC0 Public Domain Dedication along    *
* with this software.                                                          *
* If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.             *
\******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "module.h"
#include "su.h"

RSP_INFO RSP;

#define RSP_CXD4_VERSION 0x0101

#if defined(M64P_PLUGIN_API)

#include <stdarg.h>

#define RSP_PLUGIN_API_VERSION 0x020000
#define CONFIG_API_VERSION       0x020100
#define CONFIG_PARAM_VERSION     1.00

static void (*l_DebugCallback)(void *, int, const char *) = NULL;
static void *l_DebugCallContext = NULL;
static int l_PluginInit = 0;
static m64p_handle l_ConfigRsp;

#define VERSION_PRINTF_SPLIT(x) (((x) >> 16) & 0xffff), (((x) >> 8) & 0xff), ((x) & 0xff)

ptr_ConfigOpenSection      ConfigOpenSection = NULL;
ptr_ConfigDeleteSection    ConfigDeleteSection = NULL;
ptr_ConfigSaveSection      ConfigSaveSection = NULL;
ptr_ConfigSetParameter     ConfigSetParameter = NULL;
ptr_ConfigGetParameter     ConfigGetParameter = NULL;
ptr_ConfigSetDefaultFloat  ConfigSetDefaultFloat;
ptr_ConfigSetDefaultBool   ConfigSetDefaultBool = NULL;
ptr_ConfigGetParamBool     ConfigGetParamBool = NULL;

NOINLINE void update_conf(const char* source)
{
    memset(conf, 0, sizeof(conf));

    CFG_HLE_GFX = ConfigGetParamBool(l_ConfigRsp, "DisplayListToGraphicsPlugin");
    CFG_HLE_AUD = ConfigGetParamBool(l_ConfigRsp, "AudioListToAudioPlugin");
    CFG_WAIT_FOR_CPU_HOST = ConfigGetParamBool(l_ConfigRsp, "WaitForCPUHost");
    CFG_MEND_SEMAPHORE_LOCK = ConfigGetParamBool(l_ConfigRsp, "SupportCPUSemaphoreLock");
}

static void DebugMessage(int level, const char *message, ...)
{
  char msgbuf[1024];
  va_list args;

  if (l_DebugCallback == NULL)
      return;

  va_start(args, message);
  vsprintf(msgbuf, message, args);

  (*l_DebugCallback)(l_DebugCallContext, level, msgbuf);

  va_end(args);
}

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context,
                                     void (*DebugCallback)(void *, int, const char *))
{
    ptr_CoreGetAPIVersions CoreAPIVersionFunc;

    int ConfigAPIVersion, DebugAPIVersion, VidextAPIVersion, bSaveConfig;
    float fConfigParamsVersion = 0.0f;

    if (l_PluginInit)
        return M64ERR_ALREADY_INIT;

    /* first thing is to set the callback function for debug info */
    l_DebugCallback = DebugCallback;
    l_DebugCallContext = Context;

    /* attach and call the CoreGetAPIVersions function, check Config API version for compatibility */
    CoreAPIVersionFunc = (ptr_CoreGetAPIVersions) osal_dynlib_getproc(CoreLibHandle, "CoreGetAPIVersions");
    if (CoreAPIVersionFunc == NULL)
    {
        DebugMessage(M64MSG_ERROR, "Core emulator broken; no CoreAPIVersionFunc() function found.");
        return M64ERR_INCOMPATIBLE;
    }

    (*CoreAPIVersionFunc)(&ConfigAPIVersion, &DebugAPIVersion, &VidextAPIVersion, NULL);
    if ((ConfigAPIVersion & 0xffff0000) != (CONFIG_API_VERSION & 0xffff0000))
    {
        DebugMessage(M64MSG_ERROR, "Emulator core Config API (v%i.%i.%i) incompatible with plugin (v%i.%i.%i)",
                VERSION_PRINTF_SPLIT(ConfigAPIVersion), VERSION_PRINTF_SPLIT(CONFIG_API_VERSION));
        return M64ERR_INCOMPATIBLE;
    }

    /* Get the core config function pointers from the library handle */
    ConfigOpenSection = (ptr_ConfigOpenSection) osal_dynlib_getproc(CoreLibHandle, "ConfigOpenSection");
    ConfigDeleteSection = (ptr_ConfigDeleteSection) osal_dynlib_getproc(CoreLibHandle, "ConfigDeleteSection");
    ConfigSaveSection = (ptr_ConfigSaveSection) osal_dynlib_getproc(CoreLibHandle, "ConfigSaveSection");
    ConfigSetParameter = (ptr_ConfigSetParameter) osal_dynlib_getproc(CoreLibHandle, "ConfigSetParameter");
    ConfigGetParameter = (ptr_ConfigGetParameter) osal_dynlib_getproc(CoreLibHandle, "ConfigGetParameter");
    ConfigSetDefaultFloat = (ptr_ConfigSetDefaultFloat) osal_dynlib_getproc(CoreLibHandle, "ConfigSetDefaultFloat");
    ConfigSetDefaultBool = (ptr_ConfigSetDefaultBool) osal_dynlib_getproc(CoreLibHandle, "ConfigSetDefaultBool");
    ConfigGetParamBool = (ptr_ConfigGetParamBool) osal_dynlib_getproc(CoreLibHandle, "ConfigGetParamBool");

    if (!ConfigOpenSection || !ConfigDeleteSection || !ConfigSetParameter || !ConfigGetParameter ||
        !ConfigSetDefaultBool || !ConfigGetParamBool || !ConfigSetDefaultFloat)
        return M64ERR_INCOMPATIBLE;

    /* ConfigSaveSection was added in Config API v2.1.0 */
    if (ConfigAPIVersion >= 0x020100 && !ConfigSaveSection)
        return M64ERR_INCOMPATIBLE;

    /* get a configuration section handle */
    if (ConfigOpenSection("rsp-cxd4", &l_ConfigRsp) != M64ERR_SUCCESS)
    {
        DebugMessage(M64MSG_ERROR, "Couldn't open config section 'rsp-cxd4'");
        return M64ERR_INPUT_NOT_FOUND;
    }

    /* check the section version number */
    bSaveConfig = 0;
    if (ConfigGetParameter(l_ConfigRsp, "Version", M64TYPE_FLOAT, &fConfigParamsVersion, sizeof(float)) != M64ERR_SUCCESS)
    {
        DebugMessage(M64MSG_WARNING, "No version number in 'rsp-cxd4' config section. Setting defaults.");
        ConfigDeleteSection("rsp-cxd4");
        ConfigOpenSection("rsp-cxd4", &l_ConfigRsp);
        bSaveConfig = 1;
    }
    else if (((int) fConfigParamsVersion) != ((int) CONFIG_PARAM_VERSION))
    {
        DebugMessage(M64MSG_WARNING, "Incompatible version %.2f in 'rsp-cxd4' config section: current is %.2f. Setting defaults.", fConfigParamsVersion, (float) CONFIG_PARAM_VERSION);
        ConfigDeleteSection("rsp-cxd4");
        ConfigOpenSection("rsp-cxd4", &l_ConfigRsp);
        bSaveConfig = 1;
    }
    else if ((CONFIG_PARAM_VERSION - fConfigParamsVersion) >= 0.0001f)
    {
        /* handle upgrades */
        float fVersion = CONFIG_PARAM_VERSION;
        ConfigSetParameter(l_ConfigRsp, "Version", M64TYPE_FLOAT, &fVersion);
        DebugMessage(M64MSG_INFO, "Updating parameter set version in 'rsp-cxd4' config section to %.2f", fVersion);
        bSaveConfig = 1;
    }

    /* set the default values for this plugin */
    ConfigSetDefaultFloat(l_ConfigRsp, "Version", CONFIG_PARAM_VERSION,  "Mupen64Plus cxd4 RSP Plugin config parameter version number");
    ConfigSetDefaultBool(l_ConfigRsp, "DisplayListToGraphicsPlugin", 0, "Send display lists to the graphics plugin");
    ConfigSetDefaultBool(l_ConfigRsp, "AudioListToAudioPlugin", 0, "Send audio lists to the audio plugin");
    ConfigSetDefaultBool(l_ConfigRsp, "WaitForCPUHost", 0, "Force CPU-RSP signals synchronization");
    ConfigSetDefaultBool(l_ConfigRsp, "SupportCPUSemaphoreLock", 0, "Support CPU-RSP semaphore lock");

    if (bSaveConfig && ConfigAPIVersion >= 0x020100)
        ConfigSaveSection("rsp-cxd4");

    l_PluginInit = 1;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void)
{
    if (!l_PluginInit)
        return M64ERR_NOT_INIT;

    l_PluginInit = 0;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion, int *APIVersion, const char **PluginNamePtr, int *Capabilities)
{
    /* set version info */
    if (PluginType != NULL)
        *PluginType = M64PLUGIN_RSP;

    if (PluginVersion != NULL)
        *PluginVersion = RSP_CXD4_VERSION;

    if (APIVersion != NULL)
        *APIVersion = RSP_PLUGIN_API_VERSION;

    if (PluginNamePtr != NULL)
        *PluginNamePtr = "Static Interpreter";

    if (Capabilities != NULL)
    {
        *Capabilities = 0;
    }

    return M64ERR_SUCCESS;
}

EXPORT int CALL RomOpen(void)
{
    if (!l_PluginInit)
        return 0;

    update_conf(CFG_FILE);
    return 1;
}

#else

static const char DLL_about[] =
    "RSP Interpreter by Iconoclast&&ECHO."\
    "&&ECHO "\
    "Thanks for test RDP:  Jabo, ziggy, angrylion\n"\
    "RSP driver examples:  bpoint, zilmar, Ville Linde";

EXPORT void CALL CloseDLL(void)
{
    RSP.RDRAM = NULL; /* so DllTest benchmark doesn't think ROM is still open */
    return;
}
EXPORT void CALL DllAbout(struct_p hParent)
{
    hParent = NULL;
    message(DLL_about);
    return;
}
EXPORT void CALL DllConfig(struct_p hParent)
{
    hParent = NULL;
    system("sp_cfgui"); /* This launches an EXE by default (if not, BAT/CMD). */
    update_conf(CFG_FILE);
    if (RSP.DMEM == RSP.IMEM || *RSP.SP_PC_REG == 0x00000000)
        return;

    export_SP_memory();
    return;
}

#endif

EXPORT unsigned int CALL DoRspCycles(unsigned int cycles)
{
    if (*RSP.SP_STATUS_REG & 0x00000003)
    {
        message("SP_STATUS_HALT");
        return 0x00000000;
    }
    switch (*(unsigned int *)(RSP.DMEM + 0xFC0))
    { /* Simulation barrier to redirect processing externally. */
#ifdef EXTERN_COMMAND_LIST_GBI
        case 0x00000001:
            if (CFG_HLE_GFX == 0)
                break;
            if (*(unsigned int *)(RSP.DMEM + 0xFF0) == 0x00000000)
                break; /* Resident Evil 2 */
            if (RSP.ProcessDlistList == NULL) {/*branch next*/} else
                RSP.ProcessDlistList();
            *RSP.SP_STATUS_REG |= 0x00000203;
            if (*RSP.SP_STATUS_REG & 0x00000040) /* SP_STATUS_INTR_BREAK */
            {
                *RSP.MI_INTR_REG |= 0x00000001; /* VR4300 SP interrupt */
                RSP.CheckInterrupts();
            }
            if (*RSP.DPC_STATUS_REG & 0x00000002) /* DPC_STATUS_FREEZE */
            {
                message("DPC_CLR_FREEZE");
                *RSP.DPC_STATUS_REG &= ~0x00000002;
            }
            return 0;
#endif
#ifdef EXTERN_COMMAND_LIST_ABI
        case 0x00000002: /* OSTask.type == M_AUDTASK */
            if (CFG_HLE_AUD == 0)
                break;
            if (RSP.ProcessAlistList == 0) {} else
                RSP.ProcessAlistList();
            *RSP.SP_STATUS_REG |= 0x00000203;
            if (*RSP.SP_STATUS_REG & 0x00000040) /* SP_STATUS_INTR_BREAK */
            {
                *RSP.MI_INTR_REG |= 0x00000001; /* VR4300 SP interrupt */
                RSP.CheckInterrupts();
            }
            return 0;
#endif
    }
    run_task();
    return (cycles);
}

EXPORT void CALL GetDllInfo(PLUGIN_INFO *PluginInfo)
{
    PluginInfo -> Version = 0x0101; /* zilmar #1.1 (only standard RSP spec) */
    PluginInfo -> Type = PLUGIN_TYPE_RSP;
strcpy(
    PluginInfo -> Name, "Static Interpreter");
    PluginInfo -> NormalMemory = 0;
    PluginInfo -> MemoryBswaped = 1;
    return;
}

EXPORT void CALL InitiateRSP(RSP_INFO Rsp_Info, unsigned int *CycleCount)
{
    if (CycleCount != NULL) /* cycle-accuracy not doable with today's hosts */
        *CycleCount = 0x00000000;
    update_conf(CFG_FILE);

    if (Rsp_Info.DMEM == Rsp_Info.IMEM) /* usually dummy RSP data for testing */
        return; /* DMA is not executed just because plugin initiates. */
    while (Rsp_Info.IMEM != Rsp_Info.DMEM + 4096)
        message("Virtual host map noncontiguity.");

    RSP = Rsp_Info;
    *RSP.SP_PC_REG = 0x04001000 & 0x00000FFF; /* task init bug on Mupen64 */
    CR[0x0] = RSP.SP_MEM_ADDR_REG;
    CR[0x1] = RSP.SP_DRAM_ADDR_REG;
    CR[0x2] = RSP.SP_RD_LEN_REG;
    CR[0x3] = RSP.SP_WR_LEN_REG;
    CR[0x4] = RSP.SP_STATUS_REG;
    CR[0x5] = RSP.SP_DMA_FULL_REG;
    CR[0x6] = RSP.SP_DMA_BUSY_REG;
    CR[0x7] = RSP.SP_SEMAPHORE_REG;
    CR[0x8] = RSP.DPC_START_REG;
    CR[0x9] = RSP.DPC_END_REG;
    CR[0xA] = RSP.DPC_CURRENT_REG;
    CR[0xB] = RSP.DPC_STATUS_REG;
    CR[0xC] = RSP.DPC_CLOCK_REG;
    CR[0xD] = RSP.DPC_BUFBUSY_REG;
    CR[0xE] = RSP.DPC_PIPEBUSY_REG;
    CR[0xF] = RSP.DPC_TMEM_REG;
    return;
}
EXPORT void CALL RomClosed(void)
{
    *RSP.SP_PC_REG = 0x00000000;
    return;
}

#if !defined(M64P_PLUGIN_API)

NOINLINE void message(const char* body)
{ /* Avoid SHELL32/ADVAPI32/USER32 dependencies by using standard C to print. */
    char argv[4096] = "CMD /Q /D /C \"TITLE RSP Message&&ECHO ";
    int i = 0;
    int j = strlen(argv);

/*
 * I'm just using system() to call the Windows command shell to print text.
 * When the subsystem permits, use printf to trace messages, not this crap.
 * I don't use WIN32 MessageBox because that's just extra OS dependencies. :P
 */
    while (body[i] != '\0')
    {
        if (body[i] == '\n')
        {
            strcat(argv, "&&ECHO ");
            ++i;
            j += 7;
            continue;
        }
        argv[j++] = body[i++];
    }
    strcat(argv, "&&PAUSE&&EXIT\"");
    system(argv);
    return;
}
#else
NOINLINE void message(const char* body)
{
    printf("%s\n", body);
}
#endif

#if !defined(M64P_PLUGIN_API)
NOINLINE void update_conf(const char* source)
{
    FILE* stream;
    char line[CHARACTERS_PER_LINE] = "";
    char key[CHARACTERS_PER_LINE], value[CHARACTERS_PER_LINE];
    register int i, test;

    stream = fopen(source, "r");
    if (stream == NULL)
    { /* try GetModulePath or whatever to correct the path? */
        message("Failed to read config.");
        return;
    }
    do
    {
        int bvalue;

        line[0] = '\0';
        key[0] = '\0';
        value[0] = '\0';
        for (i = 0; i < CHARACTERS_PER_LINE; i++)
        {
            test = fgetc(stream);
            if (test < 0) /* either EOF or invalid ASCII characters */
                break;
            line[i] = (char)(test);
            if (line[i] == '\n')
                break;
        }
        line[i] = '\0';

        for (i = 0; i < CHARACTERS_PER_LINE && line[i] != '='; i++);
        line[i] = '\0';
        strcpy(key, line);
        strcpy(value, line + i + 1);

        bvalue = atoi(value);
        if (strcmp(key, "DisplayListToGraphicsPlugin") == 0)
            CFG_HLE_GFX = (u8)(bvalue);
        else if (strcmp(key, "AudioListToAudioPlugin") == 0)
            CFG_HLE_AUD = (u8)(bvalue);
        else if (strcmp(key, "WaitForCPUHost") == 0)
            CFG_WAIT_FOR_CPU_HOST = bvalue;
        else if (strcmp(key, "SupportCPUSemaphoreLock") == 0)
            CFG_MEND_SEMAPHORE_LOCK = bvalue;
    } while (test >= 0);
    fclose(stream);
    return;
}
#endif

#ifdef SP_EXECUTE_LOG
void step_SP_commands(uint32_t inst)
{
    if (output_log)
    {
        const char digits[16] = {
            '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'
        };
        char text[256];
        char offset[4] = "";
        char code[9] = "";
        unsigned char endian_swap[4];

        endian_swap[00] = (unsigned char)(inst >> 24);
        endian_swap[01] = (unsigned char)(inst >> 16);
        endian_swap[02] = (unsigned char)(inst >>  8);
        endian_swap[03] = (unsigned char)inst;
        offset[00] = digits[(*RSP.SP_PC_REG & 0xF00) >> 8];
        offset[01] = digits[(*RSP.SP_PC_REG & 0x0F0) >> 4];
        offset[02] = digits[(*RSP.SP_PC_REG & 0x00F) >> 0];
        code[00] = digits[(inst & 0xF0000000) >> 28];
        code[01] = digits[(inst & 0x0F000000) >> 24];
        code[02] = digits[(inst & 0x00F00000) >> 20];
        code[03] = digits[(inst & 0x000F0000) >> 16];
        code[04] = digits[(inst & 0x0000F000) >> 12];
        code[05] = digits[(inst & 0x00000F00) >>  8];
        code[06] = digits[(inst & 0x000000F0) >>  4];
        code[07] = digits[(inst & 0x0000000F) >>  0];
        strcpy(text, offset);
        strcat(text, "\n");
        strcat(text, code);
        message(text); /* PC offset, MIPS hex. */
        if (output_log == NULL) {} else /* Global pointer not updated?? */
            fwrite(endian_swap, 4, 1, output_log);
    }
}
#endif

NOINLINE void export_data_cache(void)
{
    FILE* out;
    register uint32_t addr;

    out = fopen("rcpcache.dhex", "wb");
#if (0)
    for (addr = 0x00000000; addr < 0x00001000; addr += 0x00000001)
        fputc(RSP.DMEM[BES(addr & 0x00000FFF)], out);
#else
    for (addr = 0x00000000; addr < 0x00001000; addr += 0x00000004)
    {
        fputc(RSP.DMEM[addr + 0x000 + BES(0x000)], out);
        fputc(RSP.DMEM[addr + 0x001 + MES(0x000)], out);
        fputc(RSP.DMEM[addr + 0x002 - MES(0x000)], out);
        fputc(RSP.DMEM[addr + 0x003 - BES(0x000)], out);
    }
#endif
    fclose(out);
    return;
}
NOINLINE void export_instruction_cache(void)
{
    FILE* out;
    register uint32_t addr;

    out = fopen("rcpcache.ihex", "wb");
#if (0)
    for (addr = 0x00000000; addr < 0x00001000; addr += 0x00000001)
        fputc(RSP.IMEM[BES(addr & 0x00000FFF)], out);
#else
    for (addr = 0x00000000; addr < 0x00001000; addr += 0x00000004)
    {
        fputc(RSP.IMEM[addr + 0x000 + BES(0x000)], out);
        fputc(RSP.IMEM[addr + 0x001 + MES(0x000)], out);
        fputc(RSP.IMEM[addr + 0x002 - MES(0x000)], out);
        fputc(RSP.IMEM[addr + 0x003 - BES(0x000)], out);
    }
#endif
    fclose(out);
    return;
}
void export_SP_memory(void)
{
    export_data_cache();
    export_instruction_cache();
    return;
}

/*
 * Microsoft linker defaults to an entry point of `_DllMainCRTStartup',
 * which attaches several CRT dependencies.  To eliminate CRT dependencies,
 * we direct the linker to cursor the entry point to the lower-level
 * `DllMain' symbol or, alternatively, link with /NOENTRY for no entry point.
 */
#ifdef WIN32
int __stdcall DllMain(void* hModule, u32 ul_reason_for_call, void* lpReserved)
{
    hModule = lpReserved = NULL; /* unused */
    switch (ul_reason_for_call)
    {
case 1: /* DLL_PROCESS_ATTACH */
        break;
case 2: /* DLL_THREAD_ATTACH */
        break;
case 3: /* DLL_THREAD_DETACH */
        break;
case 0: /* DLL_PROCESS_DETACH */
        break;
    }
    return 1; /* TRUE */
}
#endif
