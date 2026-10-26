// hefile.irx: il ponte fra l'IOP della PS2 e Highly Experimental, al posto
// del modulo "he" di hebios. Registra il dispositivo hefile:, i cui file sono
// quelli del filesystem virtuale del PSF2; manda la console (tty e Kprintf)
// all'emulatore; avvia hefile:/psf2.irx, che carica i moduli del brano e non
// torna più. Se torna, o non parte, chiede all'emulatore di fermarsi.
//
// Per chiamare l'emulatore si scrive a 0xBFC17120 l'indirizzo di cinque
// parole (tipo, file, indirizzo, due argomenti); il risultato arriva nella
// prima (psx_emucall in Highly Experimental).
#include "irx_imports.h"

IRX_ID("hefile", 1, 1);

enum {
    kEmuConsole = 0,
    kEmuQuit = 1,
    kEmuOpen = 3,
    kEmuClose = 4,
    kEmuRead = 5,
    kEmuLseek = 7,
};

static int emucall(int type, int fd, const void *address, int arg1, int arg2)
{
    volatile int call[5];
    call[0] = type;
    call[1] = fd;
    call[2] = (int)address;
    call[3] = arg1;
    call[4] = arg2;
    *(volatile u32 *)0xBFC17120 = (u32)call;
    return call[0];
}

// --- hefile: ---

static int hefile_open(iop_file_t *f, const char *name, int flags)
{
    int fd;
    (void)flags;
    fd = emucall(kEmuOpen, 0, name, 0, 0);
    if (fd < 0)
        return fd;
    f->privdata = (void *)fd;
    return 0;
}

static int hefile_close(iop_file_t *f)
{
    return emucall(kEmuClose, (int)f->privdata, 0, 0, 0);
}

static int hefile_read(iop_file_t *f, void *buffer, int size)
{
    return emucall(kEmuRead, (int)f->privdata, buffer, size, 0);
}

static int hefile_lseek(iop_file_t *f, int offset, int whence)
{
    return emucall(kEmuLseek, (int)f->privdata, 0, offset, whence);
}

IOMAN_RETURN_VALUE_IMPL(0);
IOMAN_RETURN_VALUE_IMPL(EIO);

static iop_device_ops_t hefile_ops = {
    IOMAN_RETURN_VALUE(0),   // init
    IOMAN_RETURN_VALUE(0),   // deinit
    IOMAN_RETURN_VALUE(EIO), // format
    &hefile_open,
    &hefile_close,
    &hefile_read,
    IOMAN_RETURN_VALUE(EIO), // write
    &hefile_lseek,
    IOMAN_RETURN_VALUE(EIO), // ioctl
    IOMAN_RETURN_VALUE(EIO), // remove
    IOMAN_RETURN_VALUE(EIO), // mkdir
    IOMAN_RETURN_VALUE(EIO), // rmdir
    IOMAN_RETURN_VALUE(EIO), // dopen
    IOMAN_RETURN_VALUE(EIO), // dclose
    IOMAN_RETURN_VALUE(EIO), // dread
    IOMAN_RETURN_VALUE(EIO), // getstat
    IOMAN_RETURN_VALUE(EIO), // chstat
};

static iop_device_t hefile_device = {"hefile", IOP_DT_FS, 1, "Virtual PSF2 device", &hefile_ops};

// --- La console: tty e Kprintf ---

static int tty_write(iop_file_t *f, void *buffer, int size)
{
    (void)f;
    return emucall(kEmuConsole, 0, buffer, size, 0);
}

static iop_device_ops_t tty_ops = {
    IOMAN_RETURN_VALUE(0), // init
    IOMAN_RETURN_VALUE(0), // deinit
    IOMAN_RETURN_VALUE(0), // format
    IOMAN_RETURN_VALUE(0), // open
    IOMAN_RETURN_VALUE(0), // close
    IOMAN_RETURN_VALUE(0), // read
    &tty_write,
    IOMAN_RETURN_VALUE(0), // lseek
    IOMAN_RETURN_VALUE(0), // ioctl
    IOMAN_RETURN_VALUE(0), // remove
    IOMAN_RETURN_VALUE(0), // mkdir
    IOMAN_RETURN_VALUE(0), // rmdir
    IOMAN_RETURN_VALUE(0), // dopen
    IOMAN_RETURN_VALUE(0), // dclose
    IOMAN_RETURN_VALUE(0), // dread
    IOMAN_RETURN_VALUE(0), // getstat
    IOMAN_RETURN_VALUE(0), // chstat
};

static iop_device_t tty_device = {"tty", IOP_DT_CHAR | IOP_DT_CONS, 1, "Highly Experimental console", &tty_ops};

struct line {
    int length;
    char text[64];
};

static void kprintf_char(void *context, int c)
{
    struct line *line = context;
    if (c == 512) { // inizio
        line->length = 0;
        return;
    }
    if (c != 513) // 513: fine
        line->text[line->length++] = (char)c;
    if ((c == 513 || line->length == (int)sizeof line->text) && line->length > 0) {
        emucall(kEmuConsole, 0, line->text, line->length, 0);
        line->length = 0;
    }
}

static int kprintf_handler(void *context, const char *format, va_list ap)
{
    struct line line;
    (void)context;
    if (format == NULL)
        return 0;
    line.length = 0;
    return prnt(kprintf_char, &line, format, ap);
}

// --- L'avvio del brano ---

static void boot(void *arg)
{
    int result = 0;
    int id;
    (void)arg;
    id = LoadStartModule("hefile:/psf2.irx", 0, NULL, &result);
    if (id < 0)
        printf("hefile: psf2.irx non parte (%d)\n", id);
    emucall(kEmuQuit, 0, 0, 0, 0);
}

int _start(int argc, char *argv[])
{
    iop_thread_t thread;
    int id;
    (void)argc;
    (void)argv;

    KprintfSet(kprintf_handler, NULL);
    // Il tty di ioman non scrive da nessuna parte: si sostituisce e si
    // riaprono stdin e stdout, come fa ioman all'avvio.
    DelDrv(tty_device.name);
    AddDrv(&tty_device);
    close(0);
    close(1);
    open("tty00:", 3);
    open("tty00:", 2);

    DelDrv(hefile_device.name);
    if (AddDrv(&hefile_device) < 0) {
        printf("hefile: dispositivo non registrato\n");
        return MODULE_NO_RESIDENT_END;
    }

    thread.attr = TH_C;
    thread.option = 0;
    thread.thread = boot;
    thread.stacksize = 0x4000;
    thread.priority = 32;
    id = CreateThread(&thread);
    if (id < 0 || StartThread(id, NULL) < 0) {
        printf("hefile: thread non avviato (%d)\n", id);
        return MODULE_NO_RESIDENT_END;
    }
    return MODULE_RESIDENT_END;
}
