/*
 * NFSMW Console Keyboard: the console's on-screen keyboard for text entry.
 *
 * The PC speed.exe (v1.3, 6,029,312 bytes) still has the console keyboard
 * screen (Keyboard.fng, constructor 0x5a6e20) but never opens it: every text
 * prompt, the alias one included, uses a PC text field that only takes typing.
 * This ASI opens the console keyboard in its place, so a name can be entered
 * with a controller, then feeds the result to the PC text field as if typed.
 *
 *   1. A screen starts text entry (MenuScreen::StartTextEntry, 0x5865e0): we
 *      keep its arguments, hide the screen's text box, and push Keyboard.fng
 *      instead, with our own accept and cancel message hashes in the
 *      FEDatabase fields it reads.
 *   2. The keyboard posts one of them (cFEng message queue, 0x516c50) and pops
 *      itself; we note the text it left in FEDatabase.
 *   3. On the screen's next tick (MenuScreen notification, 0x59b2c0) we start
 *      the PC text entry with that text, mark it active at once and post Enter
 *      (or Esc) to the window: the game's own commit (0x599100) or cancel
 *      (0x5991b0) follows on the next message pump, and the text box is shown
 *      again just before it, so the PC field is never drawn in between.
 */
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define FEDATABASE        (*(unsigned char **)0x91cf90)
#define FEMANAGER         (*(void **)0x91cadc)
#define KEYBOARD_PACKAGE  (*(const char **)0x8f3c54)      /* "Keyboard.fng" */
#define TEXTENTRY         ((unsigned char *)0x91d4b0)     /* PC text entry */
#define TEXTENTRY_ACTIVE  (*(volatile unsigned char *)(TEXTENTRY + 0x410))
#define TEXTENTRY_FIELD   (*(void *volatile *)(TEXTENTRY + 0x414))
#define TEXTENTRY_KEYS    (*(volatile int *)(TEXTENTRY + 0x408))

/* FEDatabase fields the keyboard screen reads when it is built (0x58dae0). */
#define KB_ACCEPT_MSG  0x318
#define KB_CANCEL_MSG  0x31c
#define KB_MAX_LENGTH  0x324
#define KB_MODE        0x328
#define KB_BUFFER      0x32c
#define KB_BUFFER_SIZE 156

/* MenuScreen: +0x10 its package's name, +0x28 the text entry's state (1, 2,
 * then 3 = active, a tick each: 0x59b2c0). FEObject: +0x04 next, +0x10 hash,
 * +0x18 type (5 = group), +0x1c flags (bit 0 = invisible), +0x60 children,
 * +0x64 first child. FEPackage: +0x4c first object. */
#define SCREEN_PACKAGE(s)     (*(const char **)((char *)(s) + 0x10))
#define SCREEN_ENTRY_STATE(s) (*(unsigned char *)((char *)(s) + 0x28))
#define OBJ_NEXT(o)     (*(unsigned char **)((o) + 0x04))
#define OBJ_HASH(o)     (*(unsigned *)((o) + 0x10))
#define OBJ_TYPE(o)     (*(unsigned *)((o) + 0x18))
#define OBJ_FLAGS(o)    (*(unsigned *)((o) + 0x1c))
#define OBJ_CHILDREN(o) (*(int *)((o) + 0x60))
#define OBJ_CHILD(o)    (*(unsigned char **)((o) + 0x64))
#define PKG_OBJECTS(p)  (*(unsigned char **)((p) + 0x4c))

#define MSG_SCREEN_TICK 0xc98356ba
#define MSG_KB_ACCEPT   0x4b425f41  /* ours: "KB_A" */
#define MSG_KB_CANCEL   0x4b425f43  /* ours: "KB_C" */

typedef void (__fastcall *StartTextEntry_t)(void *screen, void *edx, unsigned field, unsigned flags,
                                            const char *text, unsigned a4, unsigned max_length);
typedef char (__fastcall *ScreenNotify_t)(void *screen, void *edx, unsigned msg);
typedef void (__fastcall *QueueMessage_t)(void *fem, void *edx, unsigned msg, unsigned target, unsigned mask);
typedef void (__fastcall *PushPackage_t)(void *fem, void *edx, const char *name, void *arg, unsigned a3, unsigned a4);
typedef unsigned char *(__fastcall *FindPackage_t)(void *fem, void *edx, const char *name);
typedef void (__fastcall *EntryEnd_t)(void *field);
typedef void (__cdecl *SetVisibility_t)(unsigned char *object);

static StartTextEntry_t orig_start_text_entry;
static ScreenNotify_t orig_screen_notify;
static QueueMessage_t orig_queue_message;
static EntryEnd_t orig_commit, orig_cancel;
static const PushPackage_t push_package = (PushPackage_t)0x5257f0;
static const FindPackage_t find_package = (FindPackage_t)0x516af0;
static const SetVisibility_t set_invisible = (SetVisibility_t)0x514c70;
static const SetVisibility_t set_visible = (SetVisibility_t)0x514cc0;

enum { IDLE, KEYBOARD, RESUME, WAIT_COMMIT };
static int state = IDLE, accepted, bypass;
static void *saved_screen;
static unsigned saved_field, saved_flags, saved_a4, saved_max;
static char text[KB_BUFFER_SIZE + 1];

/* What the keyboard reads through its constructor argument when it closes:
 * arg->[0x3c]->[0xc] is the package its messages go to. Ours go nowhere: the
 * queue hook takes them first. */
static unsigned fake_package[8];
static unsigned fake_parent[32] = { [0x3c / 4] = (unsigned)fake_package };

static FILE *log_file;
static void klog(const char *format, ...)
{
    va_list args;
    if (!log_file) return;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);
    fputc('\n', log_file);
    fflush(log_file);
}

/* The screen's top-level objects holding the text field, hidden while the
 * keyboard is up. */
#define MAX_HIDDEN 8
static unsigned char *hidden[MAX_HIDDEN];
static int hidden_count;

static int holds(unsigned char *object, unsigned hash, int depth)
{
    unsigned char *child;
    int i;

    if (OBJ_HASH(object) == hash) return 1;
    if (OBJ_TYPE(object) != 5 || depth > 8) return 0;
    for (i = 0, child = OBJ_CHILD(object); i < OBJ_CHILDREN(object) && child; i++, child = OBJ_NEXT(child))
        if (holds(child, hash, depth + 1)) return 1;
    return 0;
}

static void hide_text_box(void *screen, unsigned field)
{
    const char *name = SCREEN_PACKAGE(screen);
    unsigned char *package = name ? find_package(FEMANAGER, NULL, name) : NULL, *object;
    int n = 0;

    hidden_count = 0;
    if (!package) { klog("package %s not found", name ? name : "?"); return; }
    for (object = PKG_OBJECTS(package); object && n < 256; object = OBJ_NEXT(object), n++) {
        int mine = holds(object, field, 0);
        klog("  %s object %08x type %u flags %08x%s", name, OBJ_HASH(object), OBJ_TYPE(object),
             OBJ_FLAGS(object), mine ? " <- text field" : "");
        if (mine && !(OBJ_FLAGS(object) & 1) && hidden_count < MAX_HIDDEN) {
            set_invisible(object);
            hidden[hidden_count++] = object;
        }
    }
}

static void show_text_box(void)
{
    while (hidden_count > 0) set_visible(hidden[--hidden_count]);
}

static HWND game_window;
static BOOL CALLBACK find_window(HWND hwnd, LPARAM unused)
{
    (void)unused;
    if (IsWindowVisible(hwnd)) { game_window = hwnd; return FALSE; }
    return TRUE;
}

static void post_key(WPARAM ch)
{
    if (!game_window || !IsWindow(game_window)) {
        game_window = NULL;
        EnumThreadWindows(GetCurrentThreadId(), find_window, 0);
    }
    if (game_window) PostMessageA(game_window, WM_CHAR, ch, 1);
    klog("post %#x to %p", (unsigned)ch, (void *)game_window);
}

static void __fastcall hook_start_text_entry(void *screen, void *edx, unsigned field, unsigned flags,
                                             const char *initial, unsigned a4, unsigned max_length)
{
    unsigned char *db = FEDATABASE;
    unsigned limit = max_length && max_length < KB_BUFFER_SIZE ? max_length : 15;

    if (bypass || state != IDLE || !db) {
        orig_start_text_entry(screen, edx, field, flags, initial, a4, max_length);
        return;
    }
    saved_screen = screen; saved_field = field; saved_flags = flags; saved_a4 = a4; saved_max = max_length;
    *(unsigned *)(db + KB_ACCEPT_MSG) = MSG_KB_ACCEPT;
    *(unsigned *)(db + KB_CANCEL_MSG) = MSG_KB_CANCEL;
    *(unsigned *)(db + KB_MAX_LENGTH) = limit;
    *(unsigned *)(db + KB_MODE) = 0;
    memset(db + KB_BUFFER, 0, KB_BUFFER_SIZE);
    if (initial) strncpy((char *)db + KB_BUFFER, initial, limit);
    klog("text entry field=%#x flags=%#x max=%u: keyboard", field, flags, max_length);
    hide_text_box(screen, field);
    state = KEYBOARD;
    push_package(FEMANAGER, NULL, KEYBOARD_PACKAGE, fake_parent, 0, 0);
}

static void __fastcall hook_queue_message(void *fem, void *edx, unsigned msg, unsigned target, unsigned mask)
{
    if ((msg == MSG_KB_ACCEPT || msg == MSG_KB_CANCEL) && state == KEYBOARD) {
        accepted = msg == MSG_KB_ACCEPT;
        memcpy(text, FEDATABASE + KB_BUFFER, KB_BUFFER_SIZE);
        text[KB_BUFFER_SIZE] = 0;
        klog("keyboard %s \"%s\"", accepted ? "accept" : "cancel", text);
        state = RESUME;
        return;
    }
    orig_queue_message(fem, edx, msg, target, mask);
}

static char __fastcall hook_screen_notify(void *screen, void *edx, unsigned msg)
{
    char result = orig_screen_notify(screen, edx, msg);

    if (msg == MSG_SCREEN_TICK && screen == saved_screen && state == RESUME) {
        bypass = 1;
        orig_start_text_entry(screen, NULL, saved_field, saved_flags, accepted ? text : "", saved_a4, saved_max);
        bypass = 0;
        /* Active now rather than two ticks on (0x59b2c0), Enter not ignored
         * as the first key (0x5a3790); the key lands on the next pump. */
        SCREEN_ENTRY_STATE(screen) = 3;
        TEXTENTRY_ACTIVE = 1;
        TEXTENTRY_KEYS = 1;
        post_key(accepted ? '\r' : 0x1b);
        state = WAIT_COMMIT;
    }
    return result;
}

static void finish(const char *how)
{
    klog("text entry %s", how);
    show_text_box();
    state = IDLE;
    saved_screen = NULL;
}

static void __fastcall hook_commit(void *field)
{
    if (state == WAIT_COMMIT) finish("committed");
    orig_commit(field);
}

static void __fastcall hook_cancel(void *field)
{
    if (state == WAIT_COMMIT) finish("cancelled");
    orig_cancel(field);
}

/* A jump at `at` to `hook`; returns a trampoline running the `length` bytes it
 * replaced (none of them relative) and jumping back. */
static void *detour(unsigned at, const void *expect, unsigned length, void *hook)
{
    unsigned char *code = (unsigned char *)at, *tramp;
    DWORD old;

    if (memcmp(code, expect, length)) {
        klog("unexpected bytes at %#x: not hooked", at);
        return NULL;
    }
    tramp = VirtualAlloc(NULL, length + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    memcpy(tramp, code, length);
    tramp[length] = 0xe9;
    *(int *)(tramp + length + 1) = (int)(code + length) - (int)(tramp + length + 5);
    VirtualProtect(code, length, PAGE_EXECUTE_READWRITE, &old);
    code[0] = 0xe9;
    *(int *)(code + 1) = (int)hook - (int)(code + 5);
    memset(code + 5, 0x90, length - 5);
    VirtualProtect(code, length, old, &old);
    FlushInstructionCache(GetCurrentProcess(), code, length);
    return tramp;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    static const unsigned char start_bytes[] = { 0x6a, 0xff, 0x68, 0x24, 0x23, 0x87, 0x00 };
    static const unsigned char notify_bytes[] = { 0x8b, 0x54, 0x24, 0x04, 0x81, 0xfa, 0xba, 0x56, 0x83, 0xc9 };
    static const unsigned char queue_bytes[] = { 0x8b, 0x44, 0x24, 0x0c, 0x8b, 0x54, 0x24, 0x08 };
    static const unsigned char commit_bytes[] = { 0x83, 0x3d, 0xb4, 0xd8, 0x91, 0x00, 0x06 };
    static const unsigned char cancel_bytes[] = { 0x56, 0x8b, 0xf1, 0x8b, 0x0d, 0x84, 0xcf, 0x91, 0x00 };
    char path[MAX_PATH], *dot;

    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(instance);
    GetModuleFileNameA(instance, path, sizeof(path));
    if ((dot = strrchr(path, '.'))) strcpy(dot, ".log");
    log_file = fopen(path, "w");

    if (memcmp((void *)0x8a0148, "Keyboard.fng", 13)) {
        klog("not NFSMW v1.3 speed.exe: nothing hooked");
        return TRUE;
    }
    orig_queue_message = detour(0x516c50, queue_bytes, sizeof(queue_bytes), hook_queue_message);
    orig_screen_notify = detour(0x59b2c0, notify_bytes, sizeof(notify_bytes), hook_screen_notify);
    orig_commit = detour(0x599100, commit_bytes, sizeof(commit_bytes), hook_commit);
    orig_cancel = detour(0x5991b0, cancel_bytes, sizeof(cancel_bytes), hook_cancel);
    if (orig_queue_message && orig_screen_notify && orig_commit && orig_cancel)
        orig_start_text_entry = detour(0x5865e0, start_bytes, sizeof(start_bytes), hook_start_text_entry);
    klog("hooks: queue=%p notify=%p commit=%p cancel=%p start=%p", (void *)orig_queue_message,
         (void *)orig_screen_notify, (void *)orig_commit, (void *)orig_cancel, (void *)orig_start_text_entry);
    return TRUE;
}
