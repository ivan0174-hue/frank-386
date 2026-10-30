/*
 * FRANK 386 – Raspberry Pi 4 port
 * FatFs low-level disk I/O on top of the backend block device
 * (Pi 4 EMMC2 controller under Circle, or an image file under SDL).
 */
#include "ff.h"
#include "diskio.h"
#include "port_hal.h"

static DSTATUS stat = STA_NOINIT;

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0)
        return STA_NOINIT;
    if (stat & STA_NOINIT) {
        if (port_disk_init())
            stat = 0;
    }
    return stat;
}

DSTATUS disk_status(BYTE pdrv)
{
    return pdrv == 0 ? stat : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || !count)
        return RES_PARERR;
    if (stat & STA_NOINIT)
        return RES_NOTRDY;
    return port_disk_read((uint32_t)sector, count, buff) ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || !count)
        return RES_PARERR;
    if (stat & STA_NOINIT)
        return RES_NOTRDY;
    return port_disk_write((uint32_t)sector, count, buff) ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0)
        return RES_PARERR;
    if (stat & STA_NOINIT)
        return RES_NOTRDY;
    switch (cmd) {
    case CTRL_SYNC:
        port_disk_sync();
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(LBA_t *)buff = port_disk_sector_count();
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = 512;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

/* FF_FS_NORTC is set, but some FatFs builds still reference it. */
DWORD get_fattime(void)
{
    /* 2026-01-01 00:00:00 */
    return ((DWORD)(2026 - 1980) << 25) | ((DWORD)1 << 21) | ((DWORD)1 << 16);
}
