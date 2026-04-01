#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Window meta ──────────────────────────────────────────────────── */
#define APP_TITLE     "OpenWebUI - Setup"
#define CONFIG_SUBKEY "Software\\OpenWebUI-Dashboard"
#define OPEN_WEBUI_ENV_NAME   "omx-open-webui"
#define OPEN_WEBUI_PYTHON_VER "3.12"

/* ── Colour palette (0xRRGGBB, converted to COLORREF with RGB_HEX) ── */
#define COL_BG            0x050505
#define COL_BG_ALT        0x0B0B0B
#define COL_SURFACE       0x111111
#define COL_SURFACE2      0x171717
#define COL_SURFACE3      0x1D1D1D
#define COL_BORDER        0x2A2A2A
#define COL_BORDER_SOFT   0x202020
#define COL_TEXT          0xE0E0E0
#define COL_TEXT_BRIGHT   0xFAFAFA
#define COL_MUTED         0x9A9A9A
#define COL_GREEN         0x4FD1A1
#define COL_RED           0xFF6B6B
#define COL_YELLOW        0xF2C66D
#define COL_ACCENT        0xB8B8B8
#define COL_ACCENT_SOFT   0x303030
#define COL_CARD_GREEN    0x102A28
#define COL_CARD_RED      0x34181B
#define COL_CARD_GREEN_B  0x1E6054
#define COL_CARD_RED_B    0x7B3840
#define COL_BTN_TEXT      0xFFFFFF
#define COL_SCROLL_TRACK  0x111111
#define COL_SCROLL_THUMB  0x4C4C4C
#define COL_SCROLL_HOVER  0x737373

#define RGB_HEX(h) RGB(((h)>>16)&0xFF, ((h)>>8)&0xFF, (h)&0xFF)

/* ── Control IDs ─────────────────────────────────────────────────── */
#define IDC_COPY_BTN   1001
#define IDC_NEXT_BTN    1002
#define IDC_HOST_BTN   1003
#define IDC_AUTH_BTN   1004
#define IDC_TOKEN_PROMPT_EDIT 1006
#define IDC_TOKEN_PROMPT_SAVE 1007
#define IDC_TOKEN_PROMPT_SKIP 1008

/* ── Layout ──────────────────────────────────────────────────────── */
#define HEADER_H     90
#define PAD          24
#define CARD_H       58
#define CARD_GAP     10
#define SECTION_GAP  22
#define BTN_W        150
#define BTN_H        38
#define OPT_W        120
#define OPT_H        30
#define LOG_LINE_H   18

#define TIMER_COPY           1
#define TIMER_SERVER_CHECK   2
#define TIMER_SERVER_LOGS    4
#define TIMER_PORT_SYNC      5
#define WM_APP_CONTINUE_START (WM_APP + 1)

#define OPENWEBUI_START_TIMEOUT_MS      180000
#define OPENWEBUI_START_IDLE_GRACE_MS    45000
#define OPENWEBUI_START_MAX_TIMEOUT_MS  600000

/* ═══════════════════════════════════════════════════════════════════
   Data model
   ═══════════════════════════════════════════════════════════════════ */
typedef struct { int miniconda; int python312; int ffmpeg; int hfToken; } CheckResult;
typedef enum   { PHASE_INSTALL_PREREQS, PHASE_SETUP_ENV, PHASE_READY } Phase;

typedef struct {
    CheckResult deps;
    Phase       phase;
    char        message[512];
    char        commands[2048];
    int         cmd_count;
} AppState;

static AppState  g_state     = {0};
static HWND      g_hCopyBtn  = NULL;
static HWND      g_hNextBtn = NULL;
static HWND      g_hHostBtn = NULL;
static HWND      g_hAuthBtn = NULL;
static HWND      g_hMainWnd = NULL;
static HINSTANCE g_hInst     = NULL;
static HFONT     g_fTitle    = NULL;
static HFONT     g_fSubtitle = NULL;
static HFONT     g_fLabel    = NULL;
static HFONT     g_fMono     = NULL;
static HFONT     g_fBtn      = NULL;
static int       g_cmdScroll = 0;
static int       g_cmdLines  = 0;
static int       g_cmdBoxY   = 0;
static int       g_cmdBoxH   = 0;
static int       g_useLanHost = 0;
static int       g_enableAuth = 0;
static int       g_serverStarting = 0;
static int       g_serverOpened = 0;
static DWORD     g_serverStartTick = 0;
static DWORD     g_serverLastLogTick = 0;
static char      g_serverUrl[128] = {0};
static PROCESS_INFORMATION g_serverProc = {0};
static char      g_lastError[256] = {0};
static HANDLE    g_serverLogRead = NULL;
static HANDLE    g_serverLogWrite = NULL;
static char      g_logPartial[512] = {0};
static char      g_serverLogLines[200][320];
static int       g_serverLogCount = 0;
static int       g_logScroll = 0;
static RECT      g_logPanelRect = {0, 0, 0, 0};
static RECT      g_logViewportRect = {0, 0, 0, 0};
static RECT      g_logScrollbarRect = {0, 0, 0, 0};
static RECT      g_logThumbRect = {0, 0, 0, 0};
static int       g_logDragging = 0;
static int       g_logDragOffset = 0;
static char      g_savedHfToken[256] = {0};
static HWND      g_hTokenPrompt = NULL;
static HWND      g_hTokenPromptEdit = NULL;
static int       g_pendingServerStart = 0;
static HBRUSH    g_hPromptBgBrush = NULL;
static HBRUSH    g_hPromptEditBrush = NULL;

static void refresh_ready_command_preview(void);
static void refresh_action_buttons(void);
static void continue_ready_server_start(HWND hwnd);
static int stop_openwebui_server(void);
static void sync_server_open_state(void);
static DWORD get_pid_for_tcp_port(int port);
static int get_local_ipv4(char *out, size_t outSz);
static void FillRectColor(HDC hdc, int x, int y, int w, int h, DWORD col);
static void FillVerticalGradient(HDC hdc, int x, int y, int w, int h, DWORD topCol, DWORD bottomCol);
static void DrawRoundRect(HDC hdc, int x, int y, int w, int h, int rx, DWORD fill, DWORD border);
static void DrawDialogButton(HDC hdc, const RECT *rc, const char *text, int focused, int primary, int disabled);

static void close_server_log_pipe(void) {
    if (g_serverLogRead) {
        CloseHandle(g_serverLogRead);
        g_serverLogRead = NULL;
    }
    if (g_serverLogWrite) {
        CloseHandle(g_serverLogWrite);
        g_serverLogWrite = NULL;
    }
}

static void clear_server_logs(void) {
    g_logPartial[0] = '\0';
    g_serverLogCount = 0;
    g_logScroll = 0;
    for (int i = 0; i < 200; i++) g_serverLogLines[i][0] = '\0';
}

static int get_log_visible_lines(void) {
    int h = g_logViewportRect.bottom - g_logViewportRect.top;
    if (h <= 0) return 0;
    return h / LOG_LINE_H;
}

static int get_log_max_scroll(void) {
    int visible = get_log_visible_lines();
    int maxScroll = g_serverLogCount - visible;
    return (maxScroll > 0) ? maxScroll : 0;
}

static void clamp_log_scroll(void) {
    int maxScroll = get_log_max_scroll();
    if (g_logScroll < 0) g_logScroll = 0;
    if (g_logScroll > maxScroll) g_logScroll = maxScroll;
}

static void push_server_log_line(const char *line) {
    int wasAtBottom = (g_logScroll >= get_log_max_scroll());
    if (!line || !line[0]) return;
    if (g_serverLogCount < 200) {
        strncpy(g_serverLogLines[g_serverLogCount], line, sizeof(g_serverLogLines[0]) - 1);
        g_serverLogLines[g_serverLogCount][sizeof(g_serverLogLines[0]) - 1] = '\0';
        g_serverLogCount++;
        return;
    }
    for (int i = 1; i < 200; i++) {
        memcpy(g_serverLogLines[i - 1], g_serverLogLines[i], sizeof(g_serverLogLines[0]));
    }
    strncpy(g_serverLogLines[199], line, sizeof(g_serverLogLines[0]) - 1);
    g_serverLogLines[199][sizeof(g_serverLogLines[0]) - 1] = '\0';
    if (wasAtBottom) g_logScroll = get_log_max_scroll();
    else clamp_log_scroll();
}

static void append_server_log_text(const char *text) {
    if (!text) return;
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s%s", g_logPartial, text);

    char *start = buf;
    for (char *p = buf; ; p++) {
        if (*p == '\r') {
            *p = '\0';
            push_server_log_line(start);
            start = p + 1;
            if (*start == '\n') start++;
        } else if (*p == '\n') {
            *p = '\0';
            push_server_log_line(start);
            start = p + 1;
        } else if (*p == '\0') {
            strncpy(g_logPartial, start, sizeof(g_logPartial) - 1);
            g_logPartial[sizeof(g_logPartial) - 1] = '\0';
            break;
        }
    }
}

static void poll_server_logs(void) {
    if (!g_serverLogRead) return;

    DWORD avail = 0;
    while (PeekNamedPipe(g_serverLogRead, NULL, 0, NULL, &avail, NULL) && avail > 0) {
        char chunk[512];
        DWORD toRead = (avail < (DWORD)(sizeof(chunk) - 1)) ? avail : (DWORD)(sizeof(chunk) - 1);
        DWORD got = 0;
        if (!ReadFile(g_serverLogRead, chunk, toRead, &got, NULL) || got == 0) break;
        chunk[got] = '\0';
        append_server_log_text(chunk);
        g_serverLastLogTick = GetTickCount();
        avail = 0;
    }
}

static void read_window_text(HWND hwnd, char *out, size_t outSz) {
    if (!out || outSz == 0) return;
    out[0] = '\0';
    if (!hwnd) return;
    GetWindowTextA(hwnd, out, (int)outSz);
    out[outSz - 1] = '\0';
}

static void trim_in_place(char *text) {
    if (!text || !text[0]) return;
    char *start = text;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
    if (start != text) memmove(text, start, strlen(start) + 1);
    size_t len = strlen(text);
    while (len > 0) {
        char ch = text[len - 1];
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') break;
        text[--len] = '\0';
    }
}

static int has_saved_hf_token(void) {
    char token[sizeof(g_savedHfToken)] = {0};
    strncpy(token, g_savedHfToken, sizeof(token) - 1);
    token[sizeof(token) - 1] = '\0';
    trim_in_place(token);
    return token[0] != '\0';
}

static void get_effective_hf_token(char *out, size_t outSz) {
    if (!out || outSz == 0) return;
    strncpy(out, g_savedHfToken, outSz - 1);
    out[outSz - 1] = '\0';
}

/* ═══════════════════════════════════════════════════════════════════
   System detection  (mirrors mainProcess.js logic)
   ═══════════════════════════════════════════════════════════════════ */
static int file_exists(const char *p) {
    DWORD a = GetFileAttributesA(p);
    return (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY));
}
static int dir_exists(const char *p) {
    DWORD a = GetFileAttributesA(p);
    return (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY));
}
static void join_path(char *out, size_t sz, const char *a, const char *b) {
    snprintf(out, sz, "%s\\%s", a, b);
}
static int in_path(const char *cmd) {
    char buf[MAX_PATH];
    DWORD r = SearchPathA(NULL, cmd, NULL, MAX_PATH, buf, NULL);
    return r > 0 && r < MAX_PATH;
}

static int check_miniconda(void) {
    if (in_path("conda.exe")) return 1;
    char up[MAX_PATH], la[MAX_PATH], cp[MAX_PATH];
    if (GetEnvironmentVariableA("USERPROFILE", up, MAX_PATH)) {
        char p1[MAX_PATH], p2[MAX_PATH];
        join_path(p1, MAX_PATH, up, "miniconda3");
        join_path(p2, MAX_PATH, up, "Miniconda3");
        if (dir_exists(p1) || dir_exists(p2)) return 1;
    }
    if (GetEnvironmentVariableA("LOCALAPPDATA", la, MAX_PATH)) {
        char p1[MAX_PATH], p2[MAX_PATH];
        join_path(p1, MAX_PATH, la, "miniconda3");
        join_path(p2, MAX_PATH, la, "Miniconda3");
        if (dir_exists(p1) || dir_exists(p2)) return 1;
    }
    if (GetEnvironmentVariableA("CONDA_PREFIX", cp, MAX_PATH) && dir_exists(cp)) return 1;
    return 0;
}

static int check_python312(void) {
    if (in_path("python.exe")) return 1;
    char la[MAX_PATH], pf[MAX_PATH];
    if (GetEnvironmentVariableA("LOCALAPPDATA", la, MAX_PATH)) {
        char p[MAX_PATH];
        join_path(p, MAX_PATH, la, "Programs\\Python\\Python312\\python.exe");
        if (file_exists(p)) return 1;
    }
    if (GetEnvironmentVariableA("ProgramFiles", pf, MAX_PATH)) {
        char p1[MAX_PATH], p2[MAX_PATH];
        join_path(p1, MAX_PATH, pf, "Python312\\python.exe");
        join_path(p2, MAX_PATH, pf, "Python\\Python312\\python.exe");
        if (file_exists(p1) || file_exists(p2)) return 1;
    }
    return 0;
}

static int check_ffmpeg(void) {
    if (in_path("ffmpeg.exe")) return 1;
    char up[MAX_PATH], pf[MAX_PATH], la[MAX_PATH];
    if (GetEnvironmentVariableA("USERPROFILE", up, MAX_PATH)) {
        char p1[MAX_PATH], p2[MAX_PATH];
        join_path(p1, MAX_PATH, up, "miniconda3\\Library\\bin\\ffmpeg.exe");
        join_path(p2, MAX_PATH, up, "Miniconda3\\Library\\bin\\ffmpeg.exe");
        if (file_exists(p1) || file_exists(p2)) return 1;
    }
    if (GetEnvironmentVariableA("ProgramFiles", pf, MAX_PATH)) {
        char p[MAX_PATH];
        join_path(p, MAX_PATH, pf, "ffmpeg\\bin\\ffmpeg.exe");
        if (file_exists(p)) return 1;
    }
    if (GetEnvironmentVariableA("LOCALAPPDATA", la, MAX_PATH)) {
        char p[MAX_PATH];
        join_path(p, MAX_PATH, la, "Microsoft\\WinGet\\Links\\ffmpeg.exe");
        if (file_exists(p)) return 1;
    }
    return 0;
}

/* ── Conda environment detection ────────────────────────────────── */
static int find_conda_exe(char *out, size_t outSz) {
    if (in_path("conda.exe")) {
        char buf[MAX_PATH];
        DWORD r = SearchPathA(NULL, "conda.exe", NULL, MAX_PATH, buf, NULL);
        if (r > 0 && r < MAX_PATH) { strncpy(out, buf, outSz-1); return 1; }
    }
    char up[MAX_PATH], la[MAX_PATH], cp[MAX_PATH];
    if (GetEnvironmentVariableA("USERPROFILE", up, MAX_PATH)) {
        char p[MAX_PATH];
        join_path(p, MAX_PATH, up, "miniconda3\\Scripts\\conda.exe");
        if (file_exists(p)) { strncpy(out, p, outSz-1); return 1; }
        join_path(p, MAX_PATH, up, "Miniconda3\\Scripts\\conda.exe");
        if (file_exists(p)) { strncpy(out, p, outSz-1); return 1; }
    }
    if (GetEnvironmentVariableA("LOCALAPPDATA", la, MAX_PATH)) {
        char p[MAX_PATH];
        join_path(p, MAX_PATH, la, "miniconda3\\Scripts\\conda.exe");
        if (file_exists(p)) { strncpy(out, p, outSz-1); return 1; }
        join_path(p, MAX_PATH, la, "Miniconda3\\Scripts\\conda.exe");
        if (file_exists(p)) { strncpy(out, p, outSz-1); return 1; }
    }
    if (GetEnvironmentVariableA("CONDA_PREFIX", cp, MAX_PATH)) {
        char p[MAX_PATH];
        join_path(p, MAX_PATH, cp, "Scripts\\conda.exe");
        if (file_exists(p)) { strncpy(out, p, outSz-1); return 1; }
    }
    out[0] = '\0';
    return 0;
}

static int check_conda_env_exists(const char *condaExe) {
    (void)condaExe;
    /* Check for env directory in common conda locations */
    char envPath[MAX_PATH];
    char up[MAX_PATH], la[MAX_PATH];
    if (GetEnvironmentVariableA("USERPROFILE", up, MAX_PATH)) {
        join_path(envPath, MAX_PATH, up, "miniconda3\\envs\\omx-open-webui");
        if (dir_exists(envPath)) return 1;
        join_path(envPath, MAX_PATH, up, "Miniconda3\\envs\\omx-open-webui");
        if (dir_exists(envPath)) return 1;
    }
    if (GetEnvironmentVariableA("LOCALAPPDATA", la, MAX_PATH)) {
        join_path(envPath, MAX_PATH, la, "miniconda3\\envs\\omx-open-webui");
        if (dir_exists(envPath)) return 1;
        join_path(envPath, MAX_PATH, la, "Miniconda3\\envs\\omx-open-webui");
        if (dir_exists(envPath)) return 1;
    }
    /* Also check CONDA_PREFIX env var for active env */
    char cp[MAX_PATH];
    if (GetEnvironmentVariableA("CONDA_PREFIX", cp, MAX_PATH)) {
        /* If we're currently in the target env, it exists */
        if (strstr(cp, "omx-open-webui")) return 1;
    }
    /* Check under anaconda3 too */
    if (GetEnvironmentVariableA("USERPROFILE", up, MAX_PATH)) {
        join_path(envPath, MAX_PATH, up, "anaconda3\\envs\\omx-open-webui");
        if (dir_exists(envPath)) return 1;
        join_path(envPath, MAX_PATH, up, "Anaconda3\\envs\\omx-open-webui");
        if (dir_exists(envPath)) return 1;
    }
    return 0;
}

static int check_openwebui_installed(void) {
    /* Check for open-webui package in site-packages of the conda env */
    char pkgPath[MAX_PATH];
    char up[MAX_PATH], la[MAX_PATH];
    if (GetEnvironmentVariableA("USERPROFILE", up, MAX_PATH)) {
        join_path(pkgPath, MAX_PATH, up, "miniconda3\\envs\\omx-open-webui\\Lib\\site-packages\\open_webui");
        if (dir_exists(pkgPath)) return 1;
        join_path(pkgPath, MAX_PATH, up, "Miniconda3\\envs\\omx-open-webui\\Lib\\site-packages\\open_webui");
        if (dir_exists(pkgPath)) return 1;
    }
    if (GetEnvironmentVariableA("LOCALAPPDATA", la, MAX_PATH)) {
        join_path(pkgPath, MAX_PATH, la, "miniconda3\\envs\\omx-open-webui\\Lib\\site-packages\\open_webui");
        if (dir_exists(pkgPath)) return 1;
        join_path(pkgPath, MAX_PATH, la, "Miniconda3\\envs\\omx-open-webui\\Lib\\site-packages\\open_webui");
        if (dir_exists(pkgPath)) return 1;
    }
    /* Also check CONDA_PREFIX for currently active env */
    char cp[MAX_PATH];
    if (GetEnvironmentVariableA("CONDA_PREFIX", cp, MAX_PATH) && strstr(cp, "omx-open-webui")) {
        join_path(pkgPath, MAX_PATH, cp, "Lib\\site-packages\\open_webui");
        if (dir_exists(pkgPath)) return 1;
    }
    return 0;
}

/* ── Conda TOS detection ─────────────────────────────────────────── */
#define TOS_CHANNEL_COUNT 3
static const char *TOS_CHANNELS[TOS_CHANNEL_COUNT] = {
    "https://repo.anaconda.com/pkgs/main",
    "https://repo.anaconda.com/pkgs/r",
    "https://repo.anaconda.com/pkgs/msys2"
};

static int tos_accepted[TOS_CHANNEL_COUNT] = {0, 0, 0};
static void scan_tos_json_file(const char *filePath);

static void scan_tos_json_dir(const char *dirPath) {
    WIN32_FIND_DATAA fd;
    char searchPath[MAX_PATH];
    snprintf(searchPath, sizeof(searchPath), "%s\\*", dirPath);

    HANDLE hFind = FindFirstFileA(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;

        char fullPath[MAX_PATH];
        snprintf(fullPath, sizeof(fullPath), "%s\\%s", dirPath, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scan_tos_json_dir(fullPath);
            continue;
        }

        const char *ext = strrchr(fd.cFileName, '.');
        if (ext && _stricmp(ext, ".json") == 0)
            scan_tos_json_file(fullPath);
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
}

static void scan_tos_json_file(const char *filePath) {
    /* Read file and check if it contains tos_accepted: true for our channels */
    FILE *f = fopen(filePath, "r");
    if (!f) return;
    char buf[16384];
    size_t n = fread(buf, 1, sizeof(buf)-1, f);
    buf[n] = '\0';
    fclose(f);

    for (int i = 0; i < TOS_CHANNEL_COUNT; i++) {
        if (tos_accepted[i]) continue;
        /* Check for channel URL in this file */
        if (!strstr(buf, TOS_CHANNELS[i])) continue;
        /* Match with or without space after colon (conda varies by version) */
        if (strstr(buf, "\"tos_accepted\":true")   ||
            strstr(buf, "\"tos_accepted\": true")  ||
            strstr(buf, "\"accepted\":true")        ||
            strstr(buf, "\"accepted\": true")) {
            tos_accepted[i] = 1;
        }
    }
}

static void check_conda_tos(void) {
    /* Reset */
    for (int i = 0; i < TOS_CHANNEL_COUNT; i++) tos_accepted[i] = 0;

    char tosDir[MAX_PATH];
    char up[MAX_PATH], la[MAX_PATH], cp[MAX_PATH];

    /* Scan TOS directories for JSON files */
    if (GetEnvironmentVariableA("USERPROFILE", up, MAX_PATH)) {
        join_path(tosDir, MAX_PATH, up, ".conda\\tos");
        scan_tos_json_dir(tosDir);
    }
    if (GetEnvironmentVariableA("LOCALAPPDATA", la, MAX_PATH)) {
        join_path(tosDir, MAX_PATH, la, "miniconda3\\conda-meta\\tos");
        scan_tos_json_dir(tosDir);
    }
    if (GetEnvironmentVariableA("CONDA_PREFIX", cp, MAX_PATH)) {
        join_path(tosDir, MAX_PATH, cp, "conda-meta\\tos");
        scan_tos_json_dir(tosDir);
    }
}

static int all_tos_accepted(void) {
    for (int i = 0; i < TOS_CHANNEL_COUNT; i++)
        if (!tos_accepted[i]) return 0;
    return 1;
}

/* ── Prepare state (mirrors getOpenWebUiSetupState) ─────────────── */
static void prepare_state(void) {
    CheckResult r = {check_miniconda(), check_python312(), check_ffmpeg(), has_saved_hf_token()};
    g_state.deps  = r;

    memset(g_state.commands, 0, sizeof(g_state.commands));
    g_state.cmd_count = 0;

    if (!r.miniconda || !r.python312 || !r.ffmpeg) {
        /* ── Phase 1: Install prerequisites ── */
        g_state.phase = PHASE_INSTALL_PREREQS;
        int n = 0;
        if (!r.python312) {
            strncat(g_state.commands,
                "winget install -e --id Python.Python.3.12\r\n",
                sizeof(g_state.commands)-strlen(g_state.commands)-1);
            n++;
        }
        if (!r.miniconda) {
            strncat(g_state.commands,
                "winget install -e --id Anaconda.Miniconda3\r\n",
                sizeof(g_state.commands)-strlen(g_state.commands)-1);
            n++;
        }
        if (!r.ffmpeg) {
            strncat(g_state.commands,
                "winget install -e --id Gyan.FFmpeg\r\n",
                sizeof(g_state.commands)-strlen(g_state.commands)-1);
            n++;
        }
        g_state.cmd_count = n;
        strncpy(g_state.message,
            "Open PowerShell, run the commands below, then click Next.",
            sizeof(g_state.message)-1);
    } else {
        /* ── Prerequisites OK, check conda environment ── */
        char condaExe[MAX_PATH];
        find_conda_exe(condaExe, sizeof(condaExe));

        /* Check TOS first */
        check_conda_tos();
        int tosOk = all_tos_accepted();

        int envExists = check_conda_env_exists(condaExe);
        int pkgInstalled = check_openwebui_installed();

        if (!tosOk) {
            /* ── Phase 2a: Accept Conda Terms of Service ── */
            g_state.phase = PHASE_SETUP_ENV;
            g_state.commands[0] = '\0';
            g_state.cmd_count = 0;
            for (int i = 0; i < TOS_CHANNEL_COUNT; i++) {
                if (!tos_accepted[i]) {
                    char cmd[256];
                    snprintf(cmd, sizeof(cmd), "conda tos accept --override-channels --channel %s\r\n",
                             TOS_CHANNELS[i]);
                    strncat(g_state.commands, cmd,
                            sizeof(g_state.commands)-strlen(g_state.commands)-1);
                    g_state.cmd_count++;
                }
            }
            strncpy(g_state.message,
                "Accept the TOS for each Conda channel, then click Next.",
                sizeof(g_state.message)-1);
        } else if (!envExists) {
            /* ── Phase 2b: Create conda environment ── */
            g_state.phase = PHASE_SETUP_ENV;
            snprintf(g_state.commands, sizeof(g_state.commands),
                "conda create -n %s python=%s -y\r\n"
                "conda activate %s\r\n"
                "pip install open-webui\r\n",
                OPEN_WEBUI_ENV_NAME, OPEN_WEBUI_PYTHON_VER,
                OPEN_WEBUI_ENV_NAME);
            g_state.cmd_count = 3;
            strncpy(g_state.message,
                "Run these 3 commands in PowerShell in order, then click Next.",
                sizeof(g_state.message)-1);
        } else if (!pkgInstalled) {
            /* ── Phase 2c: Install Open WebUI package ── */
            g_state.phase = PHASE_SETUP_ENV;
            snprintf(g_state.commands, sizeof(g_state.commands),
                "conda activate %s\r\n"
                "pip install open-webui\r\n",
                OPEN_WEBUI_ENV_NAME);
            g_state.cmd_count = 2;
            strncpy(g_state.message,
                "Run these commands in PowerShell, then click Next.",
                sizeof(g_state.message)-1);
        } else {
            /* ── Phase 3: Everything ready ── */
            g_state.phase = PHASE_READY;
            strncpy(g_state.message,
                "Environment is ready. Choose host/auth, then click Start Server.",
                sizeof(g_state.message)-1);
            refresh_ready_command_preview();
            sync_server_open_state();
        }
    }
}

static int ensure_winsock_ready(void) {
    static int initialized = 0;
    static int ok = 0;
    if (initialized) return ok;
    initialized = 1;
    WSADATA wsa;
    ok = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
    return ok;
}

static int check_tcp_port(const char *host, int port, int timeoutMs) {
    if (!ensure_winsock_ready()) return 0;

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return 0;

    u_long nonBlocking = 1;
    ioctlsocket(sock, FIONBIO, &nonBlocking);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = inet_addr(host);
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        closesocket(sock);
        return 0;
    }

    int connectRes = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    if (connectRes == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
        closesocket(sock);
        return 0;
    }

    fd_set writeSet;
    FD_ZERO(&writeSet);
    FD_SET(sock, &writeSet);
    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    int sel = select(0, NULL, &writeSet, NULL, &tv);
    if (sel <= 0) {
        closesocket(sock);
        return 0;
    }

    int soError = 0;
    int soLen = sizeof(soError);
    getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&soError, &soLen);
    closesocket(sock);
    return soError == 0;
}

static DWORD get_pid_for_tcp_port(int port) {
    DWORD size = 0;
    DWORD pid = 0;
    PMIB_TCPTABLE_OWNER_PID table = NULL;

    if (GetExtendedTcpTable(NULL, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) != ERROR_INSUFFICIENT_BUFFER)
        return 0;

    table = (PMIB_TCPTABLE_OWNER_PID)malloc(size);
    if (!table) return 0;

    if (GetExtendedTcpTable(table, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
        for (DWORD i = 0; i < table->dwNumEntries; i++) {
            MIB_TCPROW_OWNER_PID *row = &table->table[i];
            int localPort = ntohs((u_short)row->dwLocalPort);
            if (localPort != port) continue;

            if (row->dwState == MIB_TCP_STATE_LISTEN) {
                pid = row->dwOwningPid;
                break;
            }
            if (!pid && row->dwState != MIB_TCP_STATE_TIME_WAIT && row->dwState != MIB_TCP_STATE_CLOSED) {
                pid = row->dwOwningPid;
            }
        }
    }

    free(table);
    return pid;
}

static void sync_server_open_state(void) {
    if (g_state.phase != PHASE_READY || g_serverStarting) return;

    if (get_pid_for_tcp_port(8081) != 0) {
        g_serverOpened = 1;
        if (!g_serverUrl[0]) {
            char h[64] = "127.0.0.1";
            if (g_useLanHost) get_local_ipv4(h, sizeof(h));
            snprintf(g_serverUrl, sizeof(g_serverUrl), "http://%s:8081", h);
        }
    } else {
        g_serverOpened = 0;
        g_serverUrl[0] = '\0';
    }
}

static int get_local_ipv4(char *out, size_t outSz) {
    if (!ensure_winsock_ready()) return 0;

    char hostName[256];
    if (gethostname(hostName, sizeof(hostName)) != 0) return 0;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    if (getaddrinfo(hostName, NULL, &hints, &res) != 0) return 0;

    int found = 0;
    for (struct addrinfo *p = res; p != NULL; p = p->ai_next) {
        struct sockaddr_in *addr = (struct sockaddr_in*)p->ai_addr;
        const char *ip = inet_ntoa(addr->sin_addr);
        if (!ip) continue;
        if (strncmp(ip, "127.", 4) == 0) continue;
        strncpy(out, ip, outSz - 1);
        out[outSz - 1] = '\0';
        found = 1;
        break;
    }

    freeaddrinfo(res);
    return found;
}

static int resolve_conda_activate_script(const char *condaExe, char *out, size_t outSz) {
    if (!condaExe || !condaExe[0]) return 0;

    char scriptsDir[MAX_PATH];
    char condaRoot[MAX_PATH];
    strncpy(scriptsDir, condaExe, sizeof(scriptsDir)-1);
    scriptsDir[sizeof(scriptsDir)-1] = '\0';
    char *lastSlash = strrchr(scriptsDir, '\\');
    if (!lastSlash) return 0;
    *lastSlash = '\0';

    strncpy(condaRoot, scriptsDir, sizeof(condaRoot)-1);
    condaRoot[sizeof(condaRoot)-1] = '\0';
    lastSlash = strrchr(condaRoot, '\\');
    if (!lastSlash) return 0;
    *lastSlash = '\0';

    char p1[MAX_PATH], p2[MAX_PATH], p3[MAX_PATH];
    join_path(p1, MAX_PATH, condaRoot, "condabin\\conda.bat");
    join_path(p2, MAX_PATH, scriptsDir, "conda.bat");
    join_path(p3, MAX_PATH, scriptsDir, "activate.bat");

    if (file_exists(p1)) { strncpy(out, p1, outSz-1); out[outSz-1] = '\0'; return 1; }
    if (file_exists(p2)) { strncpy(out, p2, outSz-1); out[outSz-1] = '\0'; return 1; }
    if (file_exists(p3)) { strncpy(out, p3, outSz-1); out[outSz-1] = '\0'; return 1; }
    return 0;
}

static void apply_dark_title_bar(HWND hwnd) {
    HMODULE dwm = LoadLibraryA("dwmapi.dll");
    if (!dwm) return;

    typedef HRESULT (WINAPI *PFNDwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
    PFNDwmSetWindowAttribute setAttr =
        (PFNDwmSetWindowAttribute)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (setAttr) {
        BOOL enabled = TRUE;
        setAttr(hwnd, 20, &enabled, sizeof(enabled));
        setAttr(hwnd, 19, &enabled, sizeof(enabled));
    }
    FreeLibrary(dwm);
}

static int start_openwebui_server(void) {
    clear_server_logs();
    close_server_log_pipe();
    if (g_serverProc.hProcess) { CloseHandle(g_serverProc.hProcess); g_serverProc.hProcess = NULL; }
    if (g_serverProc.hThread) { CloseHandle(g_serverProc.hThread); g_serverProc.hThread = NULL; }

    char condaExe[MAX_PATH] = {0};
    if (!find_conda_exe(condaExe, sizeof(condaExe))) {
        strncpy(g_lastError, "Conda executable not found.", sizeof(g_lastError)-1);
        return 0;
    }

    char activateBat[MAX_PATH] = {0};
    if (!resolve_conda_activate_script(condaExe, activateBat, sizeof(activateBat))) {
        strncpy(g_lastError, "Conda activation script not found.", sizeof(g_lastError)-1);
        return 0;
    }

    const char *bindHost = g_useLanHost ? "0.0.0.0" : "127.0.0.1";
    char openHost[64] = "127.0.0.1";
    if (g_useLanHost) {
        if (!get_local_ipv4(openHost, sizeof(openHost))) {
            strncpy(openHost, "127.0.0.1", sizeof(openHost)-1);
        }
    }
    snprintf(g_serverUrl, sizeof(g_serverUrl), "http://%s:8081", openHost);

    const char *authVal = g_enableAuth ? "True" : "False";
    const char *autoLoginVal = g_enableAuth ? "False" : "True";
    const char *corsVal = g_serverUrl[0] ? g_serverUrl : "http://127.0.0.1:8081";
    char hfToken[sizeof(g_savedHfToken)] = {0};
    get_effective_hf_token(hfToken, sizeof(hfToken));

    char prevPyUtf8[64] = {0};
    char prevPyIo[64] = {0};
    char prevWebuiAuth[32] = {0};
    char prevWebuiAuto[32] = {0};
    char prevCors[256] = {0};
    char prevHfToken[256] = {0};
    char prevHfHubToken[256] = {0};
    DWORD prevPyUtf8Len = GetEnvironmentVariableA("PYTHONUTF8", prevPyUtf8, sizeof(prevPyUtf8));
    DWORD prevPyIoLen = GetEnvironmentVariableA("PYTHONIOENCODING", prevPyIo, sizeof(prevPyIo));
    DWORD prevWebuiAuthLen = GetEnvironmentVariableA("WEBUI_AUTH", prevWebuiAuth, sizeof(prevWebuiAuth));
    DWORD prevWebuiAutoLen = GetEnvironmentVariableA("WEBUI_AUTO_LOGIN", prevWebuiAuto, sizeof(prevWebuiAuto));
    DWORD prevCorsLen = GetEnvironmentVariableA("CORS_ALLOW_ORIGIN", prevCors, sizeof(prevCors));
    DWORD prevHfTokenLen = GetEnvironmentVariableA("HF_TOKEN", prevHfToken, sizeof(prevHfToken));
    DWORD prevHfHubTokenLen = GetEnvironmentVariableA("HUGGING_FACE_HUB_TOKEN", prevHfHubToken, sizeof(prevHfHubToken));
    int hadPyUtf8 = (prevPyUtf8Len > 0 && prevPyUtf8Len < sizeof(prevPyUtf8));
    int hadPyIo = (prevPyIoLen > 0 && prevPyIoLen < sizeof(prevPyIo));
    int hadWebuiAuth = (prevWebuiAuthLen > 0 && prevWebuiAuthLen < sizeof(prevWebuiAuth));
    int hadWebuiAuto = (prevWebuiAutoLen > 0 && prevWebuiAutoLen < sizeof(prevWebuiAuto));
    int hadCors = (prevCorsLen > 0 && prevCorsLen < sizeof(prevCors));
    int hadHfToken = (prevHfTokenLen > 0 && prevHfTokenLen < sizeof(prevHfToken));
    int hadHfHubToken = (prevHfHubTokenLen > 0 && prevHfHubTokenLen < sizeof(prevHfHubToken));

    SetEnvironmentVariableA("PYTHONUTF8", "1");
    SetEnvironmentVariableA("PYTHONIOENCODING", "utf-8");
    SetEnvironmentVariableA("WEBUI_AUTH", authVal);
    SetEnvironmentVariableA("WEBUI_AUTO_LOGIN", autoLoginVal);
    SetEnvironmentVariableA("CORS_ALLOW_ORIGIN", corsVal);
    if (hfToken[0]) {
        SetEnvironmentVariableA("HF_TOKEN", hfToken);
        SetEnvironmentVariableA("HUGGING_FACE_HUB_TOKEN", hfToken);
    }

    char cmdExe[MAX_PATH];
    DWORD gotComSpec = GetEnvironmentVariableA("COMSPEC", cmdExe, MAX_PATH);
    if (!gotComSpec || gotComSpec >= MAX_PATH) {
        strncpy(cmdExe, "C:\\Windows\\System32\\cmd.exe", sizeof(cmdExe)-1);
        cmdExe[sizeof(cmdExe)-1] = '\0';
    }

    char cmdArgs[2048];
    snprintf(cmdArgs, sizeof(cmdArgs),
        "/d /c call \"%s\" activate %s && open-webui serve --host %s --port 8081",
        activateBat, OPEN_WEBUI_ENV_NAME, bindHost);

    SECURITY_ATTRIBUTES sa;
    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&g_serverLogRead, &g_serverLogWrite, &sa, 0)) {
        strncpy(g_lastError, "Failed to create log pipe.", sizeof(g_lastError)-1);
        return 0;
    }
    SetHandleInformation(g_serverLogRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = g_serverLogWrite;
    si.hStdError = g_serverLogWrite;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    ZeroMemory(&g_serverProc, sizeof(g_serverProc));

    BOOL ok = CreateProcessA(
        cmdExe,
        cmdArgs,
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &si,
        &g_serverProc
    );

    if (hadPyUtf8) SetEnvironmentVariableA("PYTHONUTF8", prevPyUtf8);
    else SetEnvironmentVariableA("PYTHONUTF8", NULL);
    if (hadPyIo) SetEnvironmentVariableA("PYTHONIOENCODING", prevPyIo);
    else SetEnvironmentVariableA("PYTHONIOENCODING", NULL);
    if (hadWebuiAuth) SetEnvironmentVariableA("WEBUI_AUTH", prevWebuiAuth);
    else SetEnvironmentVariableA("WEBUI_AUTH", NULL);
    if (hadWebuiAuto) SetEnvironmentVariableA("WEBUI_AUTO_LOGIN", prevWebuiAuto);
    else SetEnvironmentVariableA("WEBUI_AUTO_LOGIN", NULL);
    if (hadCors) SetEnvironmentVariableA("CORS_ALLOW_ORIGIN", prevCors);
    else SetEnvironmentVariableA("CORS_ALLOW_ORIGIN", NULL);
    if (hadHfToken) SetEnvironmentVariableA("HF_TOKEN", prevHfToken);
    else SetEnvironmentVariableA("HF_TOKEN", NULL);
    if (hadHfHubToken) SetEnvironmentVariableA("HUGGING_FACE_HUB_TOKEN", prevHfHubToken);
    else SetEnvironmentVariableA("HUGGING_FACE_HUB_TOKEN", NULL);

    if (!ok) {
        DWORD err = GetLastError();
        snprintf(g_lastError, sizeof(g_lastError), "Failed to launch server (error %lu).", (unsigned long)err);
        close_server_log_pipe();
        return 0;
    }

    if (g_serverLogWrite) {
        CloseHandle(g_serverLogWrite);
        g_serverLogWrite = NULL;
    }

    push_server_log_line("[info] OpenWebUI launch started...");
    push_server_log_line(cmdArgs);
    return 1;
}

static int run_taskkill_python(void) {
    char systemDir[MAX_PATH] = {0};
    char taskkillPath[MAX_PATH] = "taskkill.exe";
    char cmdLine[MAX_PATH + 64];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD exitCode = 1;

    UINT got = GetSystemDirectoryA(systemDir, MAX_PATH);
    if (got > 0 && got < MAX_PATH) {
        snprintf(taskkillPath, sizeof(taskkillPath), "%s\\taskkill.exe", systemDir);
    }

    snprintf(cmdLine, sizeof(cmdLine), "\"%s\" /F /IM python.exe", taskkillPath);
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    if (!CreateProcessA(
            NULL,
            cmdLine,
            NULL,
            NULL,
            FALSE,
            CREATE_NO_WINDOW,
            NULL,
            NULL,
            &si,
            &pi)) {
        return 0;
    }

    WaitForSingleObject(pi.hProcess, 10000);
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return exitCode == 0;
}

static int stop_openwebui_server(void) {
    int stopped = 0;
    int wasActive = g_serverOpened || g_serverStarting;

    KillTimer(g_hMainWnd, TIMER_SERVER_CHECK);
    KillTimer(g_hMainWnd, TIMER_SERVER_LOGS);
    close_server_log_pipe();

    stopped = run_taskkill_python();

    if (g_serverProc.hProcess || g_serverProc.hThread) {
        if (g_serverProc.hProcess) {
            CloseHandle(g_serverProc.hProcess);
            g_serverProc.hProcess = NULL;
        }
        if (g_serverProc.hThread) {
            CloseHandle(g_serverProc.hThread);
            g_serverProc.hThread = NULL;
        }
    }
    ZeroMemory(&g_serverProc, sizeof(g_serverProc));

    g_serverStarting = 0;
    g_serverOpened = 0;
    refresh_action_buttons();
    if (g_hMainWnd) InvalidateRect(g_hMainWnd, NULL, FALSE);
    return stopped || !wasActive;
}

static void refresh_ready_command_preview(void) {
    if (g_state.phase != PHASE_READY) return;
    const char *bindHost = g_useLanHost ? "0.0.0.0" : "127.0.0.1";
    const char *authVal = g_enableAuth ? "True" : "False";
    const char *autoLoginVal = g_enableAuth ? "False" : "True";
    char hfToken[sizeof(g_savedHfToken)] = {0};
    get_effective_hf_token(hfToken, sizeof(hfToken));

    snprintf(g_state.commands, sizeof(g_state.commands),
        "conda activate %s\r\n"
        "set WEBUI_AUTH=%s\r\n"
        "set WEBUI_AUTO_LOGIN=%s\r\n",
        OPEN_WEBUI_ENV_NAME, authVal, autoLoginVal);
    g_state.cmd_count = 3;
    if (hfToken[0]) {
        strncat(g_state.commands, "set HF_TOKEN=<saved in app>\r\n", sizeof(g_state.commands) - strlen(g_state.commands) - 1);
        strncat(g_state.commands, "set HUGGING_FACE_HUB_TOKEN=<saved in app>\r\n", sizeof(g_state.commands) - strlen(g_state.commands) - 1);
        g_state.cmd_count += 2;
    }
    {
        char launchLine[256];
        snprintf(launchLine, sizeof(launchLine), "open-webui serve --host %s --port 8081\r\n", bindHost);
        strncat(g_state.commands, launchLine, sizeof(g_state.commands) - strlen(g_state.commands) - 1);
        g_state.cmd_count++;
    }
}

static void continue_ready_server_start(HWND hwnd) {
    const char *checkHost = "127.0.0.1";
    if (check_tcp_port(checkHost, 8081, 250)) {
        if (!g_serverUrl[0]) {
            char h[64] = "127.0.0.1";
            if (g_useLanHost) get_local_ipv4(h, sizeof(h));
            snprintf(g_serverUrl, sizeof(g_serverUrl), "http://%s:8081", h);
        }
        ShellExecuteA(hwnd, "open", g_serverUrl, NULL, NULL, SW_SHOWNORMAL);
        return;
    }

    if (!start_openwebui_server()) {
        MessageBoxA(hwnd, g_lastError[0] ? g_lastError : "Failed to start Open WebUI.",
                    "OpenWebUI", MB_ICONERROR | MB_OK);
        return;
    }

    g_serverStarting = 1;
    g_serverOpened = 0;
    g_serverStartTick = GetTickCount();
    g_serverLastLogTick = g_serverStartTick;
    refresh_action_buttons();
    SetTimer(hwnd, TIMER_SERVER_CHECK, 700, NULL);
    SetTimer(hwnd, TIMER_SERVER_LOGS, 120, NULL);
    InvalidateRect(hwnd, NULL, FALSE);
}

static void refresh_action_buttons(void) {
    int isReady = (g_state.phase == PHASE_READY);
    if (isReady) sync_server_open_state();
    int canStop = (isReady && g_serverOpened);
    int showReadyOptions = isReady;
    int enableReadyOptions = isReady && !g_serverStarting && !g_serverOpened;

    if (g_hNextBtn) {
        if (g_serverStarting)
            SetWindowTextA(g_hNextBtn, "Starting...");
        else if (canStop)
            SetWindowTextA(g_hNextBtn, "Stop Server");
        else if (isReady)
            SetWindowTextA(g_hNextBtn, "Start Server");
        else
            SetWindowTextA(g_hNextBtn, "Next");
    }

    if (g_hNextBtn) ShowWindow(g_hNextBtn, SW_SHOW);
    if (g_hCopyBtn) ShowWindow(g_hCopyBtn, isReady ? SW_HIDE : SW_SHOW);
    if (g_hHostBtn) ShowWindow(g_hHostBtn, showReadyOptions ? SW_SHOW : SW_HIDE);
    if (g_hAuthBtn) ShowWindow(g_hAuthBtn, showReadyOptions ? SW_SHOW : SW_HIDE);
    if (g_hHostBtn) EnableWindow(g_hHostBtn, enableReadyOptions);
    if (g_hAuthBtn) EnableWindow(g_hAuthBtn, enableReadyOptions);
    if (g_hNextBtn) EnableWindow(g_hNextBtn, !g_serverStarting);
}

/* ── Registry helpers ─────────────────────────────────────────────── */
static int load_registry_string(const char *name, char *out, DWORD outSz) {
    HKEY hKey;
    DWORD type = REG_SZ;
    DWORD cb = outSz;
    if (!out || outSz == 0) return 0;
    out[0] = '\0';
    if (RegOpenKeyExA(HKEY_CURRENT_USER, CONFIG_SUBKEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return 0;
    LONG q = RegQueryValueExA(hKey, name, NULL, &type, (LPBYTE)out, &cb);
    RegCloseKey(hKey);
    if (q != ERROR_SUCCESS || type != REG_SZ) {
        out[0] = '\0';
        return 0;
    }
    out[outSz - 1] = '\0';
    return 1;
}

static void save_registry_string(const char *name, const char *value) {
    HKEY hKey;
    DWORD disp;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, CONFIG_SUBKEY, 0, NULL, 0, KEY_WRITE, NULL, &hKey, &disp) == ERROR_SUCCESS) {
        if (value && value[0]) {
            RegSetValueExA(hKey, name, 0, REG_SZ, (const BYTE*)value, (DWORD)strlen(value) + 1);
        } else {
            RegDeleteValueA(hKey, name);
        }
        RegCloseKey(hKey);
    }
}

static int is_first_launch(void) {
    HKEY hKey; DWORD v=0, sz=sizeof(v), t=REG_DWORD;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,CONFIG_SUBKEY,0,KEY_READ,&hKey)!=ERROR_SUCCESS) return 1;
    LONG q=RegQueryValueExA(hKey,"FirstLaunchDone",NULL,&t,(LPBYTE)&v,&sz);
    RegCloseKey(hKey);
    return (q!=ERROR_SUCCESS||v==0);
}
static void mark_first_launch_done(void) {
    HKEY hKey; DWORD disp, v=1;
    if (RegCreateKeyExA(HKEY_CURRENT_USER,CONFIG_SUBKEY,0,NULL,0,KEY_WRITE,NULL,&hKey,&disp)==ERROR_SUCCESS) {
        RegSetValueExA(hKey,"FirstLaunchDone",0,REG_DWORD,(const BYTE*)&v,sizeof(v));
        RegCloseKey(hKey);
    }
}

static void load_preferences(void) {
    load_registry_string("SavedHfToken", g_savedHfToken, sizeof(g_savedHfToken));
    trim_in_place(g_savedHfToken);
}

static void save_hf_token_preference(const char *token) {
    char cleaned[sizeof(g_savedHfToken)] = {0};
    if (token) {
        strncpy(cleaned, token, sizeof(cleaned) - 1);
        cleaned[sizeof(cleaned) - 1] = '\0';
        trim_in_place(cleaned);
    }
    save_registry_string("SavedHfToken", cleaned);
    strncpy(g_savedHfToken, cleaned, sizeof(g_savedHfToken) - 1);
    g_savedHfToken[sizeof(g_savedHfToken) - 1] = '\0';
    g_state.deps.hfToken = has_saved_hf_token();
    refresh_ready_command_preview();
}

static LRESULT CALLBACK TokenPromptProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT labelFont = g_fLabel ? g_fLabel : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HFONT inputFont = g_fMono ? g_fMono : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        if (!g_hPromptBgBrush) g_hPromptBgBrush = CreateSolidBrush(RGB_HEX(COL_SURFACE));
        if (!g_hPromptEditBrush) g_hPromptEditBrush = CreateSolidBrush(RGB_HEX(0x0B0B0B));
        CreateWindowExA(0, "STATIC", "Hugging Face Token", WS_CHILD | WS_VISIBLE,
            22, 18, 220, 22, hwnd, NULL, g_hInst, NULL);
        HWND hint = CreateWindowExA(0, "STATIC",
            "Paste your HF token below and save it before starting Open WebUI.",
            WS_CHILD | WS_VISIBLE, 22, 46, 400, 36, hwnd, NULL, g_hInst, NULL);
        g_hTokenPromptEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_savedHfToken,
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD,
            22, 92, 396, 30, hwnd, (HMENU)(UINT_PTR)IDC_TOKEN_PROMPT_EDIT, g_hInst, NULL);
        HWND saveBtn = CreateWindowExA(0, "BUTTON", "Save and Start",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | BS_OWNERDRAW,
            148, 138, 130, 34, hwnd, (HMENU)(UINT_PTR)IDC_TOKEN_PROMPT_SAVE, g_hInst, NULL);
        HWND skipBtn = CreateWindowExA(0, "BUTTON", "Skip for Now",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
            288, 138, 130, 34, hwnd, (HMENU)(UINT_PTR)IDC_TOKEN_PROMPT_SKIP, g_hInst, NULL);

        SendMessageA(hwnd, WM_SETFONT, (WPARAM)labelFont, TRUE);
        if (hint) SendMessageA(hint, WM_SETFONT, (WPARAM)g_fSubtitle, TRUE);
        if (g_hTokenPromptEdit) {
            SendMessageA(g_hTokenPromptEdit, WM_SETFONT, (WPARAM)inputFont, TRUE);
            SendMessageA(g_hTokenPromptEdit, EM_LIMITTEXT, (WPARAM)(sizeof(g_savedHfToken) - 1), 0);
            SetFocus(g_hTokenPromptEdit);
        }
        if (saveBtn) SendMessageA(saveBtn, WM_SETFONT, (WPARAM)g_fBtn, TRUE);
        if (skipBtn) SendMessageA(skipBtn, WM_SETFONT, (WPARAM)g_fBtn, TRUE);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB_HEX(COL_TEXT_BRIGHT));
        return (LRESULT)(g_hPromptBgBrush ? g_hPromptBgBrush : GetStockObject(BLACK_BRUSH));
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, RGB_HEX(0x0B0B0B));
        SetTextColor(hdc, RGB_HEX(COL_TEXT_BRIGHT));
        return (LRESULT)(g_hPromptEditBrush ? g_hPromptEditBrush : GetStockObject(BLACK_BRUSH));
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT di = (LPDRAWITEMSTRUCT)lParam;
        if (di->CtlType != ODT_BUTTON) break;
        char txt[64] = {0};
        GetWindowTextA(di->hwndItem, txt, (int)sizeof(txt));
        DrawDialogButton(di->hDC, &di->rcItem, txt,
            (di->itemState & ODS_FOCUS) ? 1 : 0,
            di->CtlID == IDC_TOKEN_PROMPT_SAVE,
            (di->itemState & ODS_DISABLED) ? 1 : 0);
        return TRUE;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldBmp = SelectObject(memDC, memBmp);

        FillVerticalGradient(memDC, 0, 0, rc.right, rc.bottom, COL_SURFACE2, COL_SURFACE);
        FillRectColor(memDC, 0, 0, rc.right, 4, COL_GREEN);
        FillRectColor(memDC, 20, 42, rc.right - 40, 34, 0x1A1A1A);
        DrawRoundRect(memDC, 20, 88, rc.right - 40, 38, 6, 0x0B0B0B, COL_BORDER);

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_TOKEN_PROMPT_SAVE) {
            char token[sizeof(g_savedHfToken)] = {0};
            read_window_text(g_hTokenPromptEdit, token, sizeof(token));
            trim_in_place(token);
            if (!token[0]) {
                MessageBoxA(hwnd, "Paste a Hugging Face token or choose Skip for Now.", "OpenWebUI",
                    MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            save_hf_token_preference(token);
            if (g_hMainWnd) InvalidateRect(g_hMainWnd, NULL, TRUE);
            if (g_pendingServerStart && g_hMainWnd) PostMessageA(g_hMainWnd, WM_APP_CONTINUE_START, 0, 0);
            g_pendingServerStart = 0;
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == IDC_TOKEN_PROMPT_SKIP) {
            if (g_pendingServerStart && g_hMainWnd) PostMessageA(g_hMainWnd, WM_APP_CONTINUE_START, 0, 0);
            g_pendingServerStart = 0;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        g_pendingServerStart = 0;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (g_hMainWnd) {
            EnableWindow(g_hMainWnd, TRUE);
            SetActiveWindow(g_hMainWnd);
        }
        g_hTokenPrompt = NULL;
        g_hTokenPromptEdit = NULL;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static void show_hf_token_prompt(HWND owner) {
    if (g_hTokenPrompt) {
        ShowWindow(g_hTokenPrompt, SW_SHOW);
        SetForegroundWindow(g_hTokenPrompt);
        SetActiveWindow(g_hTokenPrompt);
        return;
    }

    const char promptClass[] = "OWUIHfTokenPrompt";
    static int registered = 0;
    if (!registered) {
        WNDCLASSEXA wc = {0};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = TokenPromptProc;
        wc.hInstance = g_hInst;
        wc.lpszClassName = promptClass;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon = LoadIconA(g_hInst, MAKEINTRESOURCEA(1));
        wc.hIconSm = LoadIconA(g_hInst, MAKEINTRESOURCEA(1));
        wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
        RegisterClassExA(&wc);
        registered = 1;
    }

    g_hTokenPrompt = CreateWindowExA(
        WS_EX_DLGMODALFRAME,
        promptClass,
        "Save Hugging Face Token",
        WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 450, 220,
        owner, NULL, g_hInst, NULL);
    if (!g_hTokenPrompt) {
        MessageBoxA(owner,
            "The Hugging Face token window could not be opened.",
            "OpenWebUI", MB_OK | MB_ICONWARNING);
        g_pendingServerStart = 0;
        return;
    }
    apply_dark_title_bar(g_hTokenPrompt);

    {
        RECT ownerRc = {0}, promptRc = {0};
        GetWindowRect(owner, &ownerRc);
        GetWindowRect(g_hTokenPrompt, &promptRc);
        int promptW = promptRc.right - promptRc.left;
        int promptH = promptRc.bottom - promptRc.top;
        int x = ownerRc.left + ((ownerRc.right - ownerRc.left) - promptW) / 2;
        int y = ownerRc.top + ((ownerRc.bottom - ownerRc.top) - promptH) / 2;
        SetWindowPos(g_hTokenPrompt, HWND_TOPMOST, x, y, 0, 0,
            SWP_NOSIZE | SWP_SHOWWINDOW);
    }
    EnableWindow(owner, FALSE);
    ShowWindow(g_hTokenPrompt, SW_SHOW);
    UpdateWindow(g_hTokenPrompt);
    SetForegroundWindow(g_hTokenPrompt);
    SetActiveWindow(g_hTokenPrompt);
}

/* ═══════════════════════════════════════════════════════════════════
   GDI helpers
   ═══════════════════════════════════════════════════════════════════ */
static HFONT MakeFont(int pts, int weight, BOOL italic, const char *face) {
    HDC hdc = GetDC(NULL);
    int lgy = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);
    return CreateFontA(-MulDiv(pts, lgy, 72), 0, 0, 0, weight,
        italic, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

static void FillRectColor(HDC hdc, int x, int y, int w, int h, DWORD col) {
    HBRUSH br = CreateSolidBrush(RGB_HEX(col));
    RECT rc = {x, y, x+w, y+h};
    FillRect(hdc, &rc, br);
    DeleteObject(br);
}

static void FillVerticalGradient(HDC hdc, int x, int y, int w, int h, DWORD topCol, DWORD bottomCol) {
    for (int i = 0; i < h; i++) {
        int r = (((topCol >> 16) & 0xFF) * (h - i) + ((bottomCol >> 16) & 0xFF) * i) / (h ? h : 1);
        int g = ((((topCol >> 8) & 0xFF) * (h - i)) + (((bottomCol >> 8) & 0xFF) * i)) / (h ? h : 1);
        int b = (((topCol & 0xFF) * (h - i)) + ((bottomCol & 0xFF) * i)) / (h ? h : 1);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(r, g, b));
        HPEN old = SelectObject(hdc, pen);
        MoveToEx(hdc, x, y + i, NULL);
        LineTo(hdc, x + w, y + i);
        SelectObject(hdc, old);
        DeleteObject(pen);
    }
}

static void DrawRoundRect(HDC hdc, int x, int y, int w, int h,
                          int rx, DWORD fill, DWORD border) {
    HBRUSH br  = CreateSolidBrush(RGB_HEX(fill));
    HPEN   pen = CreatePen(PS_SOLID, 1, RGB_HEX(border));
    HBRUSH ob  = SelectObject(hdc, br);
    HPEN   op  = SelectObject(hdc, pen);
    RoundRect(hdc, x, y, x+w, y+h, rx, rx);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(br); DeleteObject(pen);
}

static void DrawMark(HDC hdc, int cx, int cy, int ok) {
    DWORD col = ok ? COL_GREEN : COL_RED;
    DWORD bg  = ok ? 0x1E301E : 0x301E1E;
    HBRUSH br  = CreateSolidBrush(RGB_HEX(bg));
    HPEN   pen = CreatePen(PS_SOLID, 2, RGB_HEX(col));
    HBRUSH ob  = SelectObject(hdc, br);
    HPEN   op  = SelectObject(hdc, pen);
    Ellipse(hdc, cx-11, cy-11, cx+11, cy+11);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(br);

    pen = CreatePen(PS_SOLID, 2, RGB_HEX(col));
    op  = SelectObject(hdc, pen);
    if (ok) {
        MoveToEx(hdc, cx-4, cy,    NULL);
        LineTo  (hdc, cx-1, cy+4);
        LineTo  (hdc, cx+5, cy-4);
    } else {
        MoveToEx(hdc, cx-4, cy-4, NULL); LineTo(hdc, cx+4, cy+4);
        MoveToEx(hdc, cx+4, cy-4, NULL); LineTo(hdc, cx-4, cy+4);
    }
    SelectObject(hdc, op);
    DeleteObject(pen);
}

static void DrawCard(HDC hdc, int x, int y, int w,
                     const char *label, const char *sub, int ok) {
    DWORD fillCol   = ok ? COL_CARD_GREEN : COL_CARD_RED;
    DWORD borderCol = ok ? COL_CARD_GREEN_B : COL_CARD_RED_B;

    DrawRoundRect(hdc, x+1, y+1, w-2, CARD_H-2, 14, fillCol, borderCol);
    FillRectColor(hdc, x + 1, y + 1, 5, CARD_H - 2, ok ? 0x2C8C77 : 0xA24652);

    DrawMark(hdc, x+30, y+CARD_H/2, ok);

    HFONT bold = MakeFont(12, FW_SEMIBOLD, FALSE, "Segoe UI");
    HFONT old  = SelectObject(hdc, bold);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB_HEX(COL_TEXT));
    RECT r1 = {x+56, y+10, x+w-14, y+CARD_H/2+4};
    DrawTextA(hdc, label, -1, &r1, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    DeleteObject(SelectObject(hdc, old));

    HFONT sub_f = MakeFont(10, FW_NORMAL, FALSE, "Segoe UI");
    old = SelectObject(hdc, sub_f);
    SetTextColor(hdc, ok ? RGB_HEX(COL_GREEN) : RGB_HEX(COL_RED));
    RECT r2 = {x+56, y+CARD_H/2+4, x+w-14, y+CARD_H-8};
    DrawTextA(hdc, sub, -1, &r2, DT_LEFT|DT_TOP|DT_SINGLELINE);
    DeleteObject(SelectObject(hdc, old));
}

static void DrawFlatButton(HDC hdc, int x, int y, int w, int h,
                           const char *text, int hovered, int active, int disabled) {
    DWORD top = active ? 0x438FCC : (hovered ? 0x25364D : 0x1A2840);
    DWORD bot = active ? 0x2A6EA6 : (hovered ? 0x1D2B42 : 0x152035);
    DWORD border = active ? 0x8ED0FF : (hovered ? 0x4A6788 : 0x314766);
    DWORD hi = active ? 0xC8EBFF : 0x7597BA;
    DWORD lo = active ? 0x1A4F7A : 0x111C2D;
    DWORD txt = disabled ? 0x98A1AB : COL_BTN_TEXT;

    if (disabled) {
        top = 0x202833;
        bot = 0x171E28;
        border = 0x384553;
        hi = 0x526171;
        lo = 0x12171E;
    }

    DrawRoundRect(hdc, x, y, w, h, 8, bot, border);
    FillRectColor(hdc, x+1, y+1, w-2, h/2, top);

    HPEN penHi = CreatePen(PS_SOLID, 1, RGB_HEX(hi));
    HPEN oldPen = SelectObject(hdc, penHi);
    MoveToEx(hdc, x+3, y+3, NULL);
    LineTo(hdc, x+w-3, y+3);
    SelectObject(hdc, oldPen);
    DeleteObject(penHi);

    HPEN penLo = CreatePen(PS_SOLID, 1, RGB_HEX(lo));
    oldPen = SelectObject(hdc, penLo);
    MoveToEx(hdc, x+3, y+h-3, NULL);
    LineTo(hdc, x+w-3, y+h-3);
    SelectObject(hdc, oldPen);
    DeleteObject(penLo);

    HFONT old = SelectObject(hdc, g_fBtn);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB_HEX(txt));
    RECT rc = {x, y, x+w, y+h};
    DrawTextA(hdc, text, -1, &rc, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    SelectObject(hdc, old);
}

static void DrawDialogButton(HDC hdc, const RECT *rc, const char *text, int focused, int primary, int disabled) {
    int x = rc->left;
    int y = rc->top;
    int w = rc->right - rc->left;
    int h = rc->bottom - rc->top;

    DWORD fillTop = primary ? 0x242424 : 0x1B1B1B;
    DWORD fillBot = primary ? 0x1B1B1B : 0x141414;
    DWORD border = primary ? 0x4A4A4A : 0x353535;
    DWORD textCol = disabled ? 0x7A7A7A : COL_TEXT_BRIGHT;

    if (focused && !disabled) {
        border = primary ? 0x6A6A6A : 0x555555;
    }

    DrawRoundRect(hdc, x, y, w, h, 8, fillBot, border);
    FillRectColor(hdc, x + 1, y + 1, w - 2, h / 2, fillTop);

    HFONT old = SelectObject(hdc, g_fBtn);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB_HEX(textCol));
    RECT rcText = {x, y, x + w, y + h};
    DrawTextA(hdc, text, -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, old);
}

static void DrawCommandBox(HDC hdc, int x, int y, int w, int h) {
    DrawRoundRect(hdc, x, y, w, h, 14, 0x0C1524, COL_BORDER_SOFT);
    FillRectColor(hdc, x + 1, y + 1, w - 2, 30, 0x101C2E);

    int px = x + 16;
    int py = y + 12;
    /* Draw each command line */
    HFONT old = SelectObject(hdc, g_fMono);
    SetBkMode(hdc, TRANSPARENT);

    const char *p = g_state.commands[0] ? g_state.commands : "(none)";
    int lineH = 22;
    int maxLines = (h - 18) / lineH;
    int drawn = 0;

    g_cmdLines = 0;

    /* Count and draw lines */
    int visibleLine = 0;
    int stepNum = 1;
    while (*p) {
        const char *lineStart = p;
        const char *lineEnd = p;
        while (*lineEnd && *lineEnd != '\r' && *lineEnd != '\n') lineEnd++;

        g_cmdLines++;

        if (visibleLine >= g_cmdScroll && drawn < maxLines) {
            char lineBuf[512];
            int len = (int)(lineEnd - lineStart);
            if (len > 511) len = 511;
            memcpy(lineBuf, lineStart, len);
            lineBuf[len] = '\0';

            /* Trim leading whitespace for display */
            char *trimmed = lineBuf;
            while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

            /* Draw step number */
            char numBuf[16];
            snprintf(numBuf, sizeof(numBuf), "%d.", stepNum);
            SetTextColor(hdc, RGB_HEX(COL_ACCENT));
            RECT rcNum = {px, py + drawn*lineH, px+24, py + drawn*lineH + lineH};
            DrawTextA(hdc, numBuf, -1, &rcNum, DT_LEFT|DT_TOP|DT_SINGLELINE);

            /* Draw command text */
            SetTextColor(hdc, RGB_HEX(COL_TEXT));
            RECT rcText = {px+24, py + drawn*lineH, x+w-28, py + drawn*lineH + lineH};
            DrawTextA(hdc, trimmed, -1, &rcText, DT_LEFT|DT_TOP|DT_SINGLELINE|DT_END_ELLIPSIS);
            drawn++;
        }
        visibleLine++;
        stepNum++;

        p = lineEnd;
        if (*p == '\r') p++;
        if (*p == '\n') p++;
    }

    /* Draw scrollbar if needed */
    if (g_cmdLines > maxLines) {
        int sbX = x + w - 12;
        int sbH = h - 12;
        int sbY = y + 6;
        float thumbRatio = (float)maxLines / (float)g_cmdLines;
        int thumbH = (int)(sbH * thumbRatio);
        if (thumbH < 20) thumbH = 20;
        float scrollRatio = (float)g_cmdScroll / (float)(g_cmdLines - maxLines);
        int thumbY = sbY + (int)(scrollRatio * (sbH - thumbH));

        HBRUSH sbBg = CreateSolidBrush(RGB_HEX(COL_SCROLL_TRACK));
        RECT rcSbBg = {sbX-3, sbY, sbX+5, sbY+sbH};
        FillRect(hdc, &rcSbBg, sbBg);
        DeleteObject(sbBg);

        HBRUSH thumb = CreateSolidBrush(RGB_HEX(COL_SCROLL_THUMB));
        RECT rcThumb = {sbX-3, thumbY, sbX+5, thumbY+thumbH};
        FillRect(hdc, &rcThumb, thumb);
        DeleteObject(thumb);
    }

    SelectObject(hdc, old);
}

static void draw_log_scrollbar(HDC hdc) {
    int visibleLines = get_log_visible_lines();
    int maxScroll = get_log_max_scroll();
    SetRectEmpty(&g_logScrollbarRect);
    SetRectEmpty(&g_logThumbRect);
    if (visibleLines <= 0 || maxScroll <= 0) return;

    g_logScrollbarRect.left = g_logViewportRect.right + 10;
    g_logScrollbarRect.right = g_logScrollbarRect.left + 10;
    g_logScrollbarRect.top = g_logViewportRect.top;
    g_logScrollbarRect.bottom = g_logViewportRect.bottom;

    HBRUSH track = CreateSolidBrush(RGB_HEX(COL_SCROLL_TRACK));
    FillRect(hdc, &g_logScrollbarRect, track);
    DeleteObject(track);

    int trackH = g_logScrollbarRect.bottom - g_logScrollbarRect.top;
    int thumbH = (trackH * visibleLines) / g_serverLogCount;
    if (thumbH < 26) thumbH = 26;
    int travel = trackH - thumbH;
    int thumbY = g_logScrollbarRect.top;
    if (travel > 0 && maxScroll > 0)
        thumbY += (travel * g_logScroll) / maxScroll;

    g_logThumbRect.left = g_logScrollbarRect.left;
    g_logThumbRect.right = g_logScrollbarRect.right;
    g_logThumbRect.top = thumbY;
    g_logThumbRect.bottom = thumbY + thumbH;

    HBRUSH thumb = CreateSolidBrush(RGB_HEX(g_logDragging ? COL_SCROLL_HOVER : COL_SCROLL_THUMB));
    FillRect(hdc, &g_logThumbRect, thumb);
    DeleteObject(thumb);
}

static void DrawLogConsole(HDC hdc, int x, int y, int w, int h) {
    DrawRoundRect(hdc, x, y, w, h, 18, 0x091018, 0x2E445A);
    FillVerticalGradient(hdc, x + 1, y + 1, w - 2, 38, 0x132031, 0x0B1017);
    FillRectColor(hdc, x + 16, y + 38, w - 32, 1, 0x1B293B);

    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, g_fLabel);
    SetTextColor(hdc, RGB_HEX(0xEEF7FF));
    RECT rcTitle = {x + 16, y + 10, x + w - 16, y + 30};
    DrawTextA(hdc, "Open WebUI startup terminal", -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, g_fSubtitle);
    SetTextColor(hdc, RGB_HEX(0x8DA4BD));
    RECT rcHint = {x + 16, y + 34, x + w - 16, y + 54};
    DrawTextA(hdc, "Scroll with the wheel or drag the custom scrollbar on the right.", -1, &rcHint,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    g_logViewportRect.left = x + 16;
    g_logViewportRect.top = y + 64;
    g_logViewportRect.right = x + w - 30;
    g_logViewportRect.bottom = y + h - 18;
    FillRectColor(hdc, g_logViewportRect.left, g_logViewportRect.top,
                  g_logViewportRect.right - g_logViewportRect.left,
                  g_logViewportRect.bottom - g_logViewportRect.top, 0x05090D);

    HFONT old = SelectObject(hdc, g_fMono);
    SetTextColor(hdc, RGB_HEX(0xD8D8D8));
    int visible = get_log_visible_lines();
    clamp_log_scroll();
    for (int i = 0; i < visible; i++) {
        int idx = g_logScroll + i;
        if (idx >= g_serverLogCount) break;
        RECT rcLine = {
            g_logViewportRect.left + 10,
            g_logViewportRect.top + 6 + i * LOG_LINE_H,
            g_logViewportRect.right - 8,
            g_logViewportRect.top + 6 + (i + 1) * LOG_LINE_H
        };
        DrawTextA(hdc, g_serverLogLines[idx], -1, &rcLine,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    SelectObject(hdc, old);
    draw_log_scrollbar(hdc);
}

static void DrawServerStartBackdrop(HDC hdc, int w, int h) {
    FillVerticalGradient(hdc, 0, 0, w, h, 0x03060A, 0x09090B);
    FillRectColor(hdc, w - 240, 36, 300, 220, 0x09111A);
    FillRectColor(hdc, -100, h - 270, 360, 280, 0x081017);
    FillRectColor(hdc, w / 2 - 160, h / 2 - 170, 320, 90, 0x081018);
}


/* ═══════════════════════════════════════════════════════════════════
   Layout helpers
   ═══════════════════════════════════════════════════════════════════ */
static int CardsSectionBottom(void) {
    return HEADER_H + PAD + 4*(CARD_H+CARD_GAP);
}

static void LayoutWidgets(HWND hwnd) {
    RECT crc; GetClientRect(hwnd, &crc);
    int W = crc.right, H = crc.bottom;
    int labelY   = CardsSectionBottom() + SECTION_GAP;
    int optY     = labelY + 24;
    int boxTop   = optY + OPT_H + 8;
    int btnRow   = H - PAD - BTN_H;
    int boxH     = btnRow - boxTop - SECTION_GAP;
    if (boxH < 60) boxH = 60;
    g_cmdBoxY = boxTop;
    g_cmdBoxH = boxH;
    if (g_state.phase == PHASE_READY) {
        int gap = 12;
        int bw = 150;
        int totalW = bw*3 + gap*2;
        int startX = (W - totalW) / 2;
        if (g_hHostBtn) MoveWindow(g_hHostBtn, startX, btnRow, bw, BTN_H, TRUE);
        if (g_hAuthBtn) MoveWindow(g_hAuthBtn, startX + bw + gap, btnRow, bw, BTN_H, TRUE);
        if (g_hNextBtn) MoveWindow(g_hNextBtn, startX + (bw + gap)*2, btnRow, bw, BTN_H, TRUE);
        if (g_hCopyBtn) MoveWindow(g_hCopyBtn, W-PAD-BTN_W, btnRow, BTN_W, BTN_H, TRUE);
    } else {
        if (g_hHostBtn) MoveWindow(g_hHostBtn, PAD, optY, OPT_W, OPT_H, TRUE);
        if (g_hAuthBtn) MoveWindow(g_hAuthBtn, PAD + OPT_W + 10, optY, OPT_W, OPT_H, TRUE);
        if (g_hCopyBtn) MoveWindow(g_hCopyBtn, W-PAD-BTN_W, btnRow, BTN_W, BTN_H, TRUE);
        if (g_hNextBtn) MoveWindow(g_hNextBtn, W-PAD-BTN_W-SECTION_GAP-BTN_W, btnRow, BTN_W, BTN_H, TRUE);
    }
}

/* ═══════════════════════════════════════════════════════════════════
   Window proc
   ═══════════════════════════════════════════════════════════════════ */
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_CREATE: {
        g_hMainWnd = hwnd;
        SetTimer(hwnd, TIMER_PORT_SYNC, 1000, NULL);
        g_hCopyBtn = CreateWindowExA(0,"BUTTON","Copy Commands",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_OWNERDRAW,
            0,0,10,10, hwnd,(HMENU)(UINT_PTR)IDC_COPY_BTN, g_hInst, NULL);

        g_hNextBtn = CreateWindowExA(0,"BUTTON","Next",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_OWNERDRAW,
            0,0,10,10, hwnd,(HMENU)(UINT_PTR)IDC_NEXT_BTN, g_hInst, NULL);

        g_hHostBtn = CreateWindowExA(0,"BUTTON","Host: Local",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_OWNERDRAW,
            0,0,10,10, hwnd,(HMENU)(UINT_PTR)IDC_HOST_BTN, g_hInst, NULL);

        g_hAuthBtn = CreateWindowExA(0,"BUTTON","Auth: Off",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|BS_OWNERDRAW,
            0,0,10,10, hwnd,(HMENU)(UINT_PTR)IDC_AUTH_BTN, g_hInst, NULL);

        refresh_action_buttons();

        break;
    }

    case WM_SIZE:
        LayoutWidgets(hwnd);
        InvalidateRect(hwnd, NULL, TRUE);
        break;

    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE && g_state.phase == PHASE_READY && !g_serverStarting) {
            refresh_action_buttons();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT crc; GetClientRect(hwnd, &crc);
        int W = crc.right, H = crc.bottom;

        /* Double-buffer */
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, W, H);
        HBITMAP oldBmp = SelectObject(memDC, memBmp);

        /* Background */
        FillVerticalGradient(memDC, 0, 0, W, H, COL_BG, COL_BG_ALT);
        FillRectColor(memDC, W - 190, -30, 240, 220, 0x101010);
        FillRectColor(memDC, -40, H - 180, 260, 220, 0x0C0C0C);

        /* Header */
        FillVerticalGradient(memDC, 0, 0, W, HEADER_H, 0x151515, COL_SURFACE);
        /* Header accent stripe */
        DWORD accentCol;
        if (g_state.phase == PHASE_READY) accentCol = COL_GREEN;
        else if (g_state.phase == PHASE_SETUP_ENV) accentCol = COL_ACCENT;
        else accentCol = COL_YELLOW;
        FillRectColor(memDC, 0, HEADER_H-3, W, 3, accentCol);

        /* Title */
        SetBkMode(memDC, TRANSPARENT);
        HFONT oldF = SelectObject(memDC, g_fTitle);
        SetTextColor(memDC, RGB_HEX(COL_TEXT_BRIGHT));
        RECT rcTitle = {PAD, 16, W-220, HEADER_H/2+10};
        DrawTextA(memDC, "OpenWebUI Setup Center", -1, &rcTitle,
                  DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        /* Badge */
        const char *badge;
        DWORD badgeBg, badgeFg;
        if (g_state.phase == PHASE_READY) {
            badge = "  READY";
            badgeBg = 0x16241A;
            badgeFg = COL_GREEN;
        } else if (g_state.phase == PHASE_SETUP_ENV) {
            if (strstr(g_state.commands, "conda tos accept")) {
                badge = "  ACCEPT TOS";
                badgeBg = 0x202020;
                badgeFg = COL_ACCENT;
            } else {
                badge = "  SETUP NEEDED";
                badgeBg = 0x202020;
                badgeFg = COL_ACCENT;
            }
        } else {
            badge = "  ACTION NEEDED";
            badgeBg = 0x3D2F13;
            badgeFg = COL_YELLOW;
        }
        SelectObject(memDC, g_fLabel);
        SIZE tsz; GetTextExtentPoint32A(memDC, badge, (int)strlen(badge), &tsz);
        int bx = W-PAD-tsz.cx-20;
        DrawRoundRect(memDC, bx, 22, tsz.cx+20, 26, 13, badgeBg, badgeFg);
        SetTextColor(memDC, RGB_HEX(badgeFg));
        RECT rcBadge = {bx, 22, bx+tsz.cx+20, 48};
        DrawTextA(memDC, badge, -1, &rcBadge, DT_CENTER|DT_VCENTER|DT_SINGLELINE);

        /* Subtitle */
        SelectObject(memDC, g_fSubtitle);
        SetTextColor(memDC, RGB_HEX(COL_MUTED));
        RECT rcSub = {PAD, HEADER_H/2+10, W-PAD-180, HEADER_H-12};
        DrawTextA(memDC, g_state.message, -1, &rcSub,
                  DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);

        /* ── Status cards ── */
        typedef struct { const char *label; const char *yes; const char *no; int ok; } CD;
        CD cards[4] = {
            {"Python 3.12",
             "Detected on this system",
             "Not found - run install command",
             g_state.deps.python312},
            {"Miniconda",
             "Detected on this system",
             "Not found - run install command",
             g_state.deps.miniconda},
            {"FFmpeg",
             "Detected on this system",
             "Not found - run install command",
             g_state.deps.ffmpeg},
            {"HF Token",
             "Saved and ready to use",
             "Not saved yet - prompt on start",
             g_state.deps.hfToken},
        };
        int cy = HEADER_H+PAD;
        int cw = W-PAD*2;
        for (int i = 0; i < 4; i++) {
            DrawCard(memDC, PAD, cy, cw, cards[i].label,
                     cards[i].ok ? cards[i].yes : cards[i].no,
                     cards[i].ok);
            cy += CARD_H+CARD_GAP;
        }

        /* Section + command list (hidden on ready page) */
        if (g_state.phase != PHASE_READY) {
            int labelY = CardsSectionBottom()+SECTION_GAP;
            SelectObject(memDC, g_fLabel);
            SetTextColor(memDC, RGB_HEX(COL_MUTED));
            RECT rcLbl = {PAD, labelY, W-PAD, labelY+22};
            const char *lbl;
            if (g_state.phase == PHASE_SETUP_ENV) {
                if (strstr(g_state.commands, "conda tos accept"))
                    lbl = "ACCEPT CONDA TOS  -  run these commands in PowerShell, then click Next";
                else
                    lbl = "SETUP COMMANDS  -  run in PowerShell in order, then click Next";
            } else {
                lbl = "INSTALL COMMANDS  -  run in PowerShell, then click Next";
            }
            DrawTextA(memDC, lbl, -1, &rcLbl, DT_LEFT|DT_VCENTER|DT_SINGLELINE);

            DrawCommandBox(memDC, PAD, g_cmdBoxY, W-PAD*2, g_cmdBoxH);
        }

        /* Footer divider */
        HPEN pen = CreatePen(PS_SOLID, 1, RGB_HEX(COL_BORDER));
        HPEN op  = SelectObject(memDC, pen);
        MoveToEx(memDC, PAD, H-PAD-BTN_H-SECTION_GAP/2, NULL);
        LineTo  (memDC, W-PAD, H-PAD-BTN_H-SECTION_GAP/2);
        SelectObject(memDC, op); DeleteObject(pen);

        if (g_serverStarting) {
            DrawServerStartBackdrop(memDC, W, H);
            int boxW = W - 80;
            int boxH = H - 180;
            if (boxW < 500) boxW = 500;
            if (boxH < 220) boxH = 220;
            int bx = (W - boxW) / 2;
            int by = (H - boxH) / 2;
            DrawLogConsole(memDC, bx, by, boxW, boxH);
            g_logPanelRect.left = bx;
            g_logPanelRect.top = by;
            g_logPanelRect.right = bx + boxW;
            g_logPanelRect.bottom = by + boxH;
        } else {
            SetRectEmpty(&g_logPanelRect);
            SetRectEmpty(&g_logViewportRect);
            SetRectEmpty(&g_logScrollbarRect);
            SetRectEmpty(&g_logThumbRect);
        }

        SelectObject(memDC, oldF);

        /* Blit and cleanup */
        BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        break;
    }

    /* Mouse wheel scrolling for command box */
    case WM_MOUSEWHEEL: {
        if (g_serverStarting) {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &pt);
            if (PtInRect(&g_logPanelRect, pt)) {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                int step = (delta > 0) ? 2 : -2;
                g_logScroll += step;
                clamp_log_scroll();
                InvalidateRect(hwnd, NULL, TRUE);
                break;
            }
        }

        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (delta > 0 && g_cmdScroll > 0) {
            g_cmdScroll--;
            InvalidateRect(hwnd, NULL, TRUE);
        } else if (delta < 0) {
            int maxLines = g_cmdBoxH / 22;
            if (g_cmdScroll < g_cmdLines - maxLines) {
                g_cmdScroll++;
                InvalidateRect(hwnd, NULL, TRUE);
            }
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (g_serverStarting && PtInRect(&g_logThumbRect, pt)) {
            g_logDragging = 1;
            g_logDragOffset = pt.y - g_logThumbRect.top;
            SetCapture(hwnd);
            return 0;
        }
        if (g_serverStarting && PtInRect(&g_logScrollbarRect, pt)) {
            int thumbH = g_logThumbRect.bottom - g_logThumbRect.top;
            int trackH = g_logScrollbarRect.bottom - g_logScrollbarRect.top - thumbH;
            if (trackH > 0) {
                int target = pt.y - g_logScrollbarRect.top - thumbH / 2;
                if (target < 0) target = 0;
                if (target > trackH) target = trackH;
                g_logScroll = (target * get_log_max_scroll()) / trackH;
                clamp_log_scroll();
                InvalidateRect(hwnd, NULL, TRUE);
            }
            return 0;
        }
        break;
    }

    case WM_MOUSEMOVE:
        if (g_logDragging) {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            int thumbH = g_logThumbRect.bottom - g_logThumbRect.top;
            int trackH = g_logScrollbarRect.bottom - g_logScrollbarRect.top - thumbH;
            if (trackH > 0) {
                int target = pt.y - g_logScrollbarRect.top - g_logDragOffset;
                if (target < 0) target = 0;
                if (target > trackH) target = trackH;
                g_logScroll = (target * get_log_max_scroll()) / trackH;
                clamp_log_scroll();
                InvalidateRect(hwnd, NULL, TRUE);
            }
            return 0;
        }
        break;

    case WM_LBUTTONUP:
        if (g_logDragging) {
            g_logDragging = 0;
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        }
        break;

    /* Owner-draw buttons */
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT di = (LPDRAWITEMSTRUCT)lParam;
        if (di->CtlType != ODT_BUTTON) break;
        char txt[64] = {0};
        GetWindowTextA(di->hwndItem, txt, (int)sizeof(txt));
        if (!txt[0]) strncpy(txt, "Button", sizeof(txt)-1);
        int active = 0;
        int disabled = (di->itemState & ODS_DISABLED) ? 1 : 0;
        if (di->CtlID == IDC_HOST_BTN) active = g_useLanHost;
        if (di->CtlID == IDC_AUTH_BTN) active = g_enableAuth;
        int hovered = (!disabled && !(di->itemState & ODS_SELECTED) && (di->itemState & ODS_HOTLIGHT)) ? 1 : 0;

        DrawFlatButton(di->hDC, di->rcItem.left, di->rcItem.top,
                       di->rcItem.right - di->rcItem.left,
                       di->rcItem.bottom - di->rcItem.top,
                       txt, hovered, active, disabled);
        return TRUE;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_COPY_BTN && g_state.commands[0]) {
            size_t len = strlen(g_state.commands);
            if (OpenClipboard(hwnd)) {
                EmptyClipboard();
                HGLOBAL hm = GlobalAlloc(GMEM_MOVEABLE, len+1);
                if (hm) {
                    char *p = GlobalLock(hm);
                    memcpy(p, g_state.commands, len+1);
                    GlobalUnlock(hm);
                    SetClipboardData(CF_TEXT, hm);
                }
                CloseClipboard();
            }
            SetWindowTextA(g_hCopyBtn, "Copied!");
            SetTimer(hwnd, TIMER_COPY, 1500, NULL);
        }
        if (id == IDC_HOST_BTN) {
            g_useLanHost = !g_useLanHost;
            SetWindowTextA(g_hHostBtn, g_useLanHost ? "Host: LAN IP" : "Host: Local");
            refresh_ready_command_preview();
            InvalidateRect(hwnd, NULL, TRUE);
        }
        if (id == IDC_AUTH_BTN) {
            g_enableAuth = !g_enableAuth;
            SetWindowTextA(g_hAuthBtn, g_enableAuth ? "Auth: On" : "Auth: Off");
            refresh_ready_command_preview();
            InvalidateRect(hwnd, NULL, TRUE);
        }
        if (id == IDC_NEXT_BTN) {
            if (g_state.phase == PHASE_READY) {
                if (g_serverStarting) break;
                if (g_serverOpened) {
                    if (!stop_openwebui_server()) {
                        MessageBoxA(hwnd,
                            "Open WebUI could not be stopped cleanly.",
                            "OpenWebUI", MB_OK | MB_ICONWARNING);
                    }
                    break;
                }
                if (!has_saved_hf_token()) {
                    g_pendingServerStart = 1;
                    show_hf_token_prompt(hwnd);
                    InvalidateRect(hwnd, NULL, FALSE);
                    break;
                }
                continue_ready_server_start(hwnd);
            } else {
                prepare_state();
                g_cmdScroll = 0;
                refresh_action_buttons();
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        break;
    }

    case WM_APP_CONTINUE_START:
        if (g_state.phase == PHASE_READY && !g_serverStarting) {
            continue_ready_server_start(hwnd);
        }
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_COPY) {
            SetWindowTextA(g_hCopyBtn, "Copy Commands");
            KillTimer(hwnd, TIMER_COPY);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        if (wParam == TIMER_SERVER_LOGS) {
            if (!g_serverStarting) {
                KillTimer(hwnd, TIMER_SERVER_LOGS);
            } else {
                poll_server_logs();
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        if (wParam == TIMER_PORT_SYNC) {
            if (g_state.phase == PHASE_READY && !g_serverStarting) {
                int wasOpened = g_serverOpened;
                sync_server_open_state();
                if (wasOpened != g_serverOpened) {
                    refresh_action_buttons();
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
        }
        if (wParam == TIMER_SERVER_CHECK) {
            if (!g_serverStarting) {
                KillTimer(hwnd, TIMER_SERVER_CHECK);
            } else {
                if (check_tcp_port("127.0.0.1", 8081, 250)) {
                    poll_server_logs();
                    g_serverStarting = 0;
                    g_serverOpened = 1;
                    KillTimer(hwnd, TIMER_SERVER_CHECK);
                    KillTimer(hwnd, TIMER_SERVER_LOGS);
                    close_server_log_pipe();
                    refresh_action_buttons();
                    InvalidateRect(hwnd, NULL, FALSE);
                    ShellExecuteA(hwnd, "open", g_serverUrl[0] ? g_serverUrl : "http://127.0.0.1:8081",
                                  NULL, NULL, SW_SHOWNORMAL);
                } else {
                    DWORD now = GetTickCount();
                    DWORD elapsed = now - g_serverStartTick;
                    DWORD idleFor = now - g_serverLastLogTick;
                    int timedOut = 0;
                    if (elapsed >= OPENWEBUI_START_MAX_TIMEOUT_MS) timedOut = 1;
                    else if (elapsed >= OPENWEBUI_START_TIMEOUT_MS && idleFor >= OPENWEBUI_START_IDLE_GRACE_MS) timedOut = 1;

                    if (timedOut) {
                        poll_server_logs();
                        g_serverStarting = 0;
                        KillTimer(hwnd, TIMER_SERVER_CHECK);
                        KillTimer(hwnd, TIMER_SERVER_LOGS);
                        close_server_log_pipe();
                        refresh_action_buttons();
                        InvalidateRect(hwnd, NULL, FALSE);
                        MessageBoxA(hwnd,
                            "Open WebUI is taking too long to start.\n"
                            "Make sure the conda environment exists and open-webui is installed.",
                            "OpenWebUI", MB_ICONWARNING | MB_OK);
                    }
                }
            }
        }
        break;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_COPY);
        KillTimer(hwnd, TIMER_SERVER_CHECK);
        KillTimer(hwnd, TIMER_SERVER_LOGS);
        KillTimer(hwnd, TIMER_PORT_SYNC);
        close_server_log_pipe();
        if (g_hPromptBgBrush) { DeleteObject(g_hPromptBgBrush); g_hPromptBgBrush = NULL; }
        if (g_hPromptEditBrush) { DeleteObject(g_hPromptEditBrush); g_hPromptEditBrush = NULL; }
        g_hMainWnd = NULL;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

/* ═══════════════════════════════════════════════════════════════════
   WinMain
   ═══════════════════════════════════════════════════════════════════ */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)lpCmdLine;
    g_hInst = hInstance;
    load_preferences();

    /* Scan BEFORE creating window — fixes blank-on-launch bug */
    prepare_state();
    if (is_first_launch()) mark_first_launch_done();

    /* Pre-create shared fonts */
    g_fTitle    = MakeFont(16, FW_BOLD,     FALSE, "Segoe UI");
    g_fSubtitle = MakeFont(10, FW_NORMAL,   FALSE, "Segoe UI");
    g_fLabel    = MakeFont(10, FW_SEMIBOLD, FALSE, "Segoe UI");
    g_fMono     = MakeFont(11, FW_NORMAL,   FALSE, "Consolas");
    g_fBtn      = MakeFont(10, FW_SEMIBOLD, FALSE, "Segoe UI");

    const char CLASS[] = "OWUIDashboardClass";
    WNDCLASSEXA wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wc.hIcon         = LoadIconA(hInstance, MAKEINTRESOURCEA(1));
    wc.hIconSm       = LoadIconA(hInstance, MAKEINTRESOURCEA(1));
    RegisterClassExA(&wc);

    HWND hwnd = NULL;
    {
        int defaultW = 608;
        int defaultH = 594;
        RECT workArea = {0};
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &workArea, 0);
        int x = workArea.left + ((workArea.right - workArea.left) - defaultW) / 2;
        int y = workArea.top + ((workArea.bottom - workArea.top) - defaultH) / 2;

        if (x < workArea.left) x = workArea.left;
        if (y < workArea.top) y = workArea.top;

        hwnd = CreateWindowExA(0, CLASS, APP_TITLE,
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
            x, y, defaultW, defaultH,
            NULL, NULL, hInstance, NULL);
    }

    if (!hwnd) return 1;
    apply_dark_title_bar(hwnd);

    /* Trigger layout immediately after creation */
    RECT crc; GetClientRect(hwnd, &crc);
    SendMessageA(hwnd, WM_SIZE, SIZE_RESTORED,
        MAKELPARAM(crc.right, crc.bottom));

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    if (g_fTitle)    DeleteObject(g_fTitle);
    if (g_fSubtitle) DeleteObject(g_fSubtitle);
    if (g_fLabel)    DeleteObject(g_fLabel);
    if (g_fMono)     DeleteObject(g_fMono);
    if (g_fBtn)      DeleteObject(g_fBtn);
    if (g_serverProc.hProcess) CloseHandle(g_serverProc.hProcess);
    if (g_serverProc.hThread)  CloseHandle(g_serverProc.hThread);
    WSACleanup();

    return (int)msg.wParam;
}
