#include "ff.h"
#include "xil_cache.h"
#include "xil_io.h"
#include "xil_printf.h"
#include "xstatus.h"

#include <string.h>

/*
 * JTAG SD-card recovery utility for the RFSoC 4x2 V46 image.
 *
 * Before this program is started, XSCT loads the verified BOOT.BIN at
 * IMAGE_ADDRESS.  The utility replaces only 0:/BOOT.BIN.  It deliberately
 * does not modify BOOT.PYNQ.BIN or any Linux/rootfs file.
 */
#define IMAGE_ADDRESS       ((UINTPTR)0x10000000U)
#define IMAGE_SIZE_BYTES    34742152U
#define RESULT_ADDRESS      ((UINTPTR)0x0FF01000U)
#define COPY_CHUNK_BYTES    (64U * 1024U)
#define PROGRESS_BYTES      (4U * 1024U * 1024U)

#define RESULT_RUNNING      0x53445501U
#define RESULT_PASS         0x534455A5U
#define RESULT_BAD_SOURCE   0x534455E1U
#define RESULT_MOUNT_FAIL   0x534455E2U
#define RESULT_PREP_FAIL    0x534455E3U
#define RESULT_WRITE_FAIL   0x534455E4U
#define RESULT_VERIFY_FAIL  0x534455E5U
#define RESULT_RENAME_FAIL  0x534455E6U

#define BOOT_NAME           "0:/BOOT.BIN"
#define NEW_NAME            "0:/BOOT.NEW"
#define BAD_NAME            "0:/BOOT.BAD"

static FATFS FileSystem;
static FIL BootFile;
static u8 VerifyBuffer[COPY_CHUNK_BYTES] __attribute__((aligned(64)));

static void set_result(u32 value)
{
    Xil_Out32(RESULT_ADDRESS, value);
    Xil_DCacheFlushRange(RESULT_ADDRESS, sizeof(value));
}

static int file_exists(const char *name)
{
    FILINFO info;
    return f_stat(name, &info) == FR_OK;
}

static void restore_old_boot(void)
{
    if ((!file_exists(BOOT_NAME)) && file_exists(BAD_NAME)) {
        const FRESULT fr = f_rename(BAD_NAME, BOOT_NAME);
        xil_printf("Recovery restore old BOOT.BIN: fr=%d\r\n", (int)fr);
    }
}

static int source_is_valid(void)
{
    const volatile u32 *source = (const volatile u32 *)IMAGE_ADDRESS;

    Xil_DCacheInvalidateRange(IMAGE_ADDRESS, IMAGE_SIZE_BYTES);
    if ((source[0x20U / sizeof(u32)] != 0xAA995566U) ||
        (source[0x24U / sizeof(u32)] != 0x584C4E58U)) {
        xil_printf("FAIL: source BOOT header width=0x%08lx image=0x%08lx\r\n",
                   (unsigned long)source[0x20U / sizeof(u32)],
                   (unsigned long)source[0x24U / sizeof(u32)]);
        return XST_FAILURE;
    }
    return XST_SUCCESS;
}

static int prepare_destination(void)
{
    FRESULT fr;
    DWORD free_clusters = 0U;
    FATFS *mounted_fs = NULL;
    u64 free_bytes = 0U;

    if (file_exists(NEW_NAME)) {
        fr = f_unlink(NEW_NAME);
        if (fr != FR_OK) {
            xil_printf("FAIL: cannot remove stale BOOT.NEW fr=%d\r\n", (int)fr);
            return XST_FAILURE;
        }
    }
    if (file_exists(BAD_NAME)) {
        fr = f_unlink(BAD_NAME);
        if (fr != FR_OK) {
            xil_printf("FAIL: cannot remove stale BOOT.BAD fr=%d\r\n", (int)fr);
            return XST_FAILURE;
        }
    }

    if (file_exists(BOOT_NAME)) {
        fr = f_rename(BOOT_NAME, BAD_NAME);
        if (fr != FR_OK) {
            xil_printf("FAIL: cannot preserve old BOOT.BIN fr=%d\r\n", (int)fr);
            return XST_FAILURE;
        }
        xil_printf("Old BOOT.BIN renamed to BOOT.BAD.\r\n");
    }

    fr = f_getfree("0:/", &free_clusters, &mounted_fs);
    if ((fr == FR_OK) && (mounted_fs != NULL)) {
        free_bytes = (u64)free_clusters * (u64)mounted_fs->csize * 512ULL;
        xil_printf("FAT free bytes before copy: %lu MB\r\n",
                   (unsigned long)(free_bytes / (1024ULL * 1024ULL)));
    }

    /* The boot partition is only 130 MB.  If it cannot hold both images,
     * remove only BOOT.BAD (the already-proven-broken V46 image).  The PYNQ
     * recovery file remains untouched. */
    if (file_exists(BAD_NAME) &&
        ((fr != FR_OK) || (free_bytes < ((u64)IMAGE_SIZE_BYTES + 1048576ULL)))) {
        xil_printf("Insufficient duplicate-image space; removing BOOT.BAD.\r\n");
        fr = f_unlink(BAD_NAME);
        if (fr != FR_OK) {
            xil_printf("FAIL: cannot free old image fr=%d\r\n", (int)fr);
            restore_old_boot();
            return XST_FAILURE;
        }
    }
    return XST_SUCCESS;
}

static int write_new_boot(void)
{
    const u8 *source = (const u8 *)IMAGE_ADDRESS;
    u32 offset = 0U;
    u32 next_progress = PROGRESS_BYTES;
    FRESULT fr;

    fr = f_open(&BootFile, NEW_NAME, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        xil_printf("FAIL: f_open BOOT.NEW for write fr=%d\r\n", (int)fr);
        return XST_FAILURE;
    }

    while (offset < IMAGE_SIZE_BYTES) {
        const u32 remaining = IMAGE_SIZE_BYTES - offset;
        const UINT requested = (remaining > COPY_CHUNK_BYTES) ?
                               COPY_CHUNK_BYTES : (UINT)remaining;
        UINT written = 0U;

        fr = f_write(&BootFile, source + offset, requested, &written);
        if ((fr != FR_OK) || (written != requested)) {
            xil_printf("FAIL: write offset=%lu requested=%u written=%u fr=%d\r\n",
                       (unsigned long)offset, requested, written, (int)fr);
            (void)f_close(&BootFile);
            return XST_FAILURE;
        }
        offset += written;
        if (offset >= next_progress) {
            xil_printf("Write progress: %lu/%lu bytes\r\n",
                       (unsigned long)offset, (unsigned long)IMAGE_SIZE_BYTES);
            next_progress += PROGRESS_BYTES;
        }
    }

    fr = f_sync(&BootFile);
    if (fr == FR_OK) {
        fr = f_close(&BootFile);
    } else {
        (void)f_close(&BootFile);
    }
    if (fr != FR_OK) {
        xil_printf("FAIL: sync/close BOOT.NEW fr=%d\r\n", (int)fr);
        return XST_FAILURE;
    }
    return XST_SUCCESS;
}

static int verify_new_boot(void)
{
    const u8 *source = (const u8 *)IMAGE_ADDRESS;
    u32 offset = 0U;
    u32 next_progress = PROGRESS_BYTES;
    FRESULT fr;

    fr = f_open(&BootFile, NEW_NAME, FA_READ);
    if (fr != FR_OK) {
        xil_printf("FAIL: f_open BOOT.NEW for verify fr=%d\r\n", (int)fr);
        return XST_FAILURE;
    }
    if (f_size(&BootFile) != (FSIZE_t)IMAGE_SIZE_BYTES) {
        xil_printf("FAIL: BOOT.NEW size=%lu expected=%lu\r\n",
                   (unsigned long)f_size(&BootFile),
                   (unsigned long)IMAGE_SIZE_BYTES);
        (void)f_close(&BootFile);
        return XST_FAILURE;
    }

    while (offset < IMAGE_SIZE_BYTES) {
        const u32 remaining = IMAGE_SIZE_BYTES - offset;
        const UINT requested = (remaining > COPY_CHUNK_BYTES) ?
                               COPY_CHUNK_BYTES : (UINT)remaining;
        UINT read_bytes = 0U;

        fr = f_read(&BootFile, VerifyBuffer, requested, &read_bytes);
        if ((fr != FR_OK) || (read_bytes != requested) ||
            (memcmp(VerifyBuffer, source + offset, requested) != 0)) {
            xil_printf("FAIL: verify offset=%lu requested=%u read=%u fr=%d\r\n",
                       (unsigned long)offset, requested, read_bytes, (int)fr);
            (void)f_close(&BootFile);
            return XST_FAILURE;
        }
        offset += read_bytes;
        if (offset >= next_progress) {
            xil_printf("Verify progress: %lu/%lu bytes\r\n",
                       (unsigned long)offset, (unsigned long)IMAGE_SIZE_BYTES);
            next_progress += PROGRESS_BYTES;
        }
    }
    fr = f_close(&BootFile);
    return (fr == FR_OK) ? XST_SUCCESS : XST_FAILURE;
}

int main(void)
{
    FRESULT fr;

    set_result(RESULT_RUNNING);
    xil_printf("\r\nV46 RFSoC4x2 JTAG SD0 BOOT updater\r\n");
    xil_printf("Source=0x%08lx bytes=%lu destination=%s\r\n",
               (unsigned long)IMAGE_ADDRESS, (unsigned long)IMAGE_SIZE_BYTES,
               BOOT_NAME);

    if (source_is_valid() != XST_SUCCESS) {
        set_result(RESULT_BAD_SOURCE);
        return XST_FAILURE;
    }

    fr = f_mount(&FileSystem, "0:/", 1U);
    if (fr != FR_OK) {
        xil_printf("FAIL: f_mount SD0 fr=%d\r\n", (int)fr);
        set_result(RESULT_MOUNT_FAIL);
        return XST_FAILURE;
    }
    xil_printf("SD0 FAT mount PASS.\r\n");

    if (prepare_destination() != XST_SUCCESS) {
        set_result(RESULT_PREP_FAIL);
        return XST_FAILURE;
    }
    if (write_new_boot() != XST_SUCCESS) {
        (void)f_unlink(NEW_NAME);
        restore_old_boot();
        set_result(RESULT_WRITE_FAIL);
        return XST_FAILURE;
    }
    if (verify_new_boot() != XST_SUCCESS) {
        (void)f_unlink(NEW_NAME);
        restore_old_boot();
        set_result(RESULT_VERIFY_FAIL);
        return XST_FAILURE;
    }

    fr = f_rename(NEW_NAME, BOOT_NAME);
    if (fr != FR_OK) {
        xil_printf("FAIL: activate BOOT.NEW fr=%d\r\n", (int)fr);
        restore_old_boot();
        set_result(RESULT_RENAME_FAIL);
        return XST_FAILURE;
    }
    (void)f_mount(NULL, "0:/", 0U);
    xil_printf("V46_SD0_BOOT_UPDATE_PASS\r\n");
    xil_printf("BOOT.BIN was byte-for-byte verified before activation.\r\n");
    set_result(RESULT_PASS);
    return XST_SUCCESS;
}
