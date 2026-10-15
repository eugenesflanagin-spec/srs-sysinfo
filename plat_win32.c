/* plat_win32.c - Win32 platform layer for SRS64X.exe / SRS32X.exe and the winelib ELF.
 * Under wine the Linux side is reachable through the Z: drive, so the same
 * image reads real hardware from sysfs; on Windows it uses SetupAPI. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include "srs.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

double srs_now_ms(void)
{
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

int plat_is_wine(void)
{
    static int w = -1;
    if (w < 0) {
        HMODULE nt = GetModuleHandleA("ntdll.dll");
        w = nt && GetProcAddress(nt, "wine_get_version") ? 1 : 0;
    }
    return w;
}

static int SRS_CDECL name_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

int plat_listdir(const char *dir, char ***names)
{
    char pat[1100];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char **v = NULL;
    int n = 0;
    *names = NULL;
    snprintf(pat, sizeof pat, "%s\\*", dir);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    do {
        char **nv;
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        nv = realloc(v, (size_t)(n + 1) * sizeof *nv);
        if (!nv) break;
        v = nv;
        v[n] = _strdup(fd.cFileName);
        if (v[n]) n++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    if (n > 1) qsort(v, (size_t)n, sizeof *v, name_cmp);
    *names = v;
    return n;
}

void plat_freelist(char **names, int n)
{
    int i;
    for (i = 0; i < n; i++) free(names[i]);
    free(names);
}

int plat_isdir(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

int plat_exedir(char *out, size_t outsz)
{
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)outsz);
    char *s;
    if (!n || n >= outsz) return -1;
    if (!(s = strrchr(out, '\\'))) return -1;
    *s = 0;
    return 0;
}

char plat_sep(void) { return '\\'; }

const char *plat_hwdata_dir(void) { return plat_is_wine() ? "Z:\\usr\\share\\hwdata" : NULL; }

const char *plat_sysfs(void)
{
    if (getenv("SRS_FORCE_SETUPAPI")) return NULL;   /* test hook for the Windows path */
    return plat_is_wine() && plat_isdir("Z:\\sys\\bus\\pci") ? "Z:\\sys" : NULL;
}

const char *plat_home(void)
{
    static char h[1024];
    const char *e;
    if (plat_is_wine() && (e = getenv("WINEHOMEDIR"))) {   /* "\??\Z:\home\user" */
        snprintf(h, sizeof h, "%s", strncmp(e, "\\??\\", 4) ? e : e + 4);
        return h;
    }
    if (plat_is_wine() && (e = getenv("HOME")) && e[0] == '/') {
        char *p;
        snprintf(h, sizeof h, "Z:%s", e);
        for (p = h; *p; p++) if (*p == '/') *p = '\\';
        return h;
    }
    return getenv("USERPROFILE");
}

/* Run a Linux helper (wine only): wine can start a native ELF through CreateProcess. */
int plat_run_capture(const char *const argv[], char *out, size_t outsz)
{
    char cmd[2048], exe[512];
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE rd, wr;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD got = 0, n, code = 0;
    int i;
    size_t l;

    out[0] = 0;
    if (!plat_is_wine()) return -1;
    snprintf(exe, sizeof exe, "Z:\\usr\\bin\\%s", argv[0]);
    if (GetFileAttributesA(exe) == INVALID_FILE_ATTRIBUTES) return -1;
    snprintf(cmd, sizeof cmd, "\"%s\"", exe);
    for (i = 1; argv[i]; i++) {
        l = strlen(cmd);
        snprintf(cmd + l, sizeof cmd - l, " \"%s\"", argv[i]);
    }
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(rd); CloseHandle(wr);
        return -1;
    }
    CloseHandle(wr);
    while (got + 1 < outsz && ReadFile(rd, out + got, (DWORD)(outsz - 1 - got), &n, NULL) && n) got += n;
    out[got] = 0;
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, 10000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    if (!got) return -1;   /* wine could not run the ELF helper: let the caller fall back */
    return (int)code;
}

/* ---- SetupAPI: hardware IDs "PCI\VEN_1002&DEV_7550&SUBSYS_88111EAE&REV_C0", "USB\VID_1D6B&PID_0002" ---- */

static const char *field(const char *hw, const char *tag, int len, char *out)
{
    const char *p = strstr(hw, tag);
    int i;
    if (!p) return NULL;
    p += strlen(tag);
    for (i = 0; i < len; i++) {
        if (!isxdigit((unsigned char)p[i])) return NULL;
        out[i] = (char)toupper((unsigned char)p[i]);
    }
    out[len] = 0;
    return out;
}

static int hwid_to_key(enum srs_bus bus, const char *hw, char *key, size_t keysz)
{
    char v[5], d[5], ss[9], rev[3];
    if (bus == BUS_PCI) {
        if (_strnicmp(hw, "PCI\\", 4) || !field(hw, "VEN_", 4, v) || !field(hw, "DEV_", 4, d)) return 0;
        snprintf(key, keysz, "%s:%s", v, d);
        if (field(hw, "SUBSYS_", 8, ss) && strcmp(ss, "00000000")) {
            size_t l = strlen(key);   /* SUBSYS is ssssSSSS: subsystem device then vendor */
            snprintf(key + l, keysz - l, ":%.4s:%.4s", ss + 4, ss);
        }
        if (field(hw, "REV_", 2, rev)) {
            size_t l = strlen(key);
            snprintf(key + l, keysz - l, ":%s", rev);
        }
        return 1;
    }
    if (_strnicmp(hw, "USB\\", 4) || strstr(hw, "&MI_") || !field(hw, "VID_", 4, v) || !field(hw, "PID_", 4, d)) return 0;
    snprintf(key, keysz, "%s:%s", v, d);
    return 1;
}

int plat_setupapi_devices(enum srs_bus bus, srs_dev **out)
{
    HDEVINFO h = SetupDiGetClassDevsA(NULL, bus == BUS_PCI ? "PCI" : "USB", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    SP_DEVINFO_DATA dd;
    DWORD i;
    int cnt = 0;
    srs_dev *v = NULL;
    *out = NULL;
    if (h == INVALID_HANDLE_VALUE) return -1;
    dd.cbSize = sizeof dd;
    for (i = 0; SetupDiEnumDeviceInfo(h, i, &dd); i++) {
        char hw[1024] = "", inst[512] = "", key[40], *slot;
        srs_dev *nv;
        if (!SetupDiGetDeviceRegistryPropertyA(h, &dd, SPDRP_HARDWAREID, NULL, (BYTE *)hw, sizeof hw - 2, NULL)) continue;
        if (!hwid_to_key(bus, hw, key, sizeof key)) continue;
        SetupDiGetDeviceInstanceIdA(h, &dd, inst, sizeof inst, NULL);
        slot = strrchr(inst, '\\');
        nv = realloc(v, (size_t)(cnt + 1) * sizeof *nv);
        if (!nv) break;
        v = nv;
        snprintf(v[cnt].slot, sizeof v[cnt].slot, "%s", slot ? slot + 1 : inst);
        snprintf(v[cnt].key, sizeof v[cnt].key, "%s", key);
        cnt++;
    }
    SetupDiDestroyDeviceInfoList(h);
    *out = v;
    return cnt;
}

int plat_setupapi_info(enum srs_bus bus, const char *id, srs_info *out)
{
    HDEVINFO h = SetupDiGetClassDevsA(NULL, bus == BUS_PCI ? "PCI" : "USB", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    SP_DEVINFO_DATA dd;
    DWORD i;
    char want[40], *p;
    snprintf(want, sizeof want, "%s", id);
    for (p = want; *p; p++) *p = (char)toupper((unsigned char)*p);
    snprintf(out->query, sizeof out->query, "SetupAPI hardware ID match for %s", want);
    if (h == INVALID_HANDLE_VALUE) { snprintf(out->err, sizeof out->err, "SetupDiGetClassDevs failed (%lu)", GetLastError()); return -1; }
    dd.cbSize = sizeof dd;
    for (i = 0; SetupDiEnumDeviceInfo(h, i, &dd); i++) {
        char hw[1024] = "", key[40];
        if (!SetupDiGetDeviceRegistryPropertyA(h, &dd, SPDRP_HARDWAREID, NULL, (BYTE *)hw, sizeof hw - 2, NULL)) continue;
        if (!hwid_to_key(bus, hw, key, sizeof key) || strncmp(key, want, strlen(want))) continue;
        SetupDiGetDeviceRegistryPropertyA(h, &dd, SPDRP_DEVICEDESC, NULL, (BYTE *)out->model, sizeof out->model, NULL);
        SetupDiGetDeviceRegistryPropertyA(h, &dd, SPDRP_MFG, NULL, (BYTE *)out->vendor, sizeof out->vendor, NULL);
        break;
    }
    SetupDiDestroyDeviceInfoList(h);
    if (!out->model[0] && !out->vendor[0]) snprintf(out->err, sizeof out->err, "no present device matches %s", want);
    return 0;
}

/* Build fingerprint read from our own PE header - the same fields SIV shows ("x64 APP 6.00 5.02"). */
const char *plat_build_info(void)
{
    static char b[512];
    const BYTE *base = (const BYTE *)GetModuleHandleA(NULL);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    const IMAGE_NT_HEADERS *nt;
    const IMAGE_OPTIONAL_HEADER *oh;
    const IMAGE_DATA_DIRECTORY *imp;
    char crt[64] = "?";
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return "unknown image";
    nt = (const IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    oh = &nt->OptionalHeader;
    imp = &oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (imp->VirtualAddress && imp->Size) {
        const IMAGE_IMPORT_DESCRIPTOR *d = (const IMAGE_IMPORT_DESCRIPTOR *)(base + imp->VirtualAddress);
        for (; d->Name; d++) {
            const char *nm = (const char *)(base + d->Name);
            if (!_strnicmp(nm, "msvcr", 5) || !_strnicmp(nm, "ucrtbase", 8) || !_strnicmp(nm, "api-ms-win-crt", 14)) {
                snprintf(crt, sizeof crt, "%s", nm);
                break;
            }
        }
    }
    snprintf(b, sizeof b, "%s %s %u.%02u %u.%02u, CRT %s, ImageBase %#llx, Stack %luMB, %s%s",
             oh->Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC ? "x64" : "x32",
             oh->Subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI ? "APP" : "CON",
             oh->MajorOperatingSystemVersion, oh->MinorOperatingSystemVersion,
             oh->MajorSubsystemVersion, oh->MinorSubsystemVersion, crt,
             (unsigned long long)oh->ImageBase, (unsigned long)(oh->SizeOfStackReserve >> 20),
#ifdef __WINE__
             "winelib ELF, ",
#else
             "",
#endif
#if defined(__clang__)
             "clang " __clang_version__
#else
             "gcc " __VERSION__
#endif
    );
    return b;
}
