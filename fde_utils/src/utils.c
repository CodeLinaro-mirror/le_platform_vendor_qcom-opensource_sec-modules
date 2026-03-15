/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.  
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <fcntl.h>
#include <dirent.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include "fde_core.h"
#include "utils.h"

// Converts an array of bytes to hex.  The output string will be
// (2*num_bytes)+1 characters long including the null terminator.
int bytes_to_hex(const uint8_t *bytes, size_t num_bytes, char *hex, size_t hex_size)
{
    static const char lut[] = "0123456789abcdef";
    if (!bytes || !hex || hex_size < (2 * num_bytes + 1))
        return -1;

    for (size_t i = 0; i < num_bytes; i++) {
        uint8_t tmp = bytes[i];
        hex[2 * i] = lut[tmp >> 4];
        hex[2 * i + 1] = lut[tmp & 0x0F];
    }
    hex[2 * num_bytes] = '\0';
    return 0;
}

bool pathExists(const char* path)
{
    return access(path, F_OK) == 0;
}

bool is_real_block_device(const char *blk_dev)
{
    bool is_blk = false;

    int fd = open(blk_dev, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return is_blk;

    struct stat st;
    is_blk = (fstat(fd, &st) == 0) && S_ISBLK(st.st_mode);

    close(fd);
    return is_blk;
}

static int get_block_device_size(const char *path, uint64_t *size)
{
    int fd, ret = 0;

    if (!path)
        return -1;

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -errno;

    if (ioctl(fd, BLKGETSIZE64, size))
        ret = -errno;

    close(fd);
    return ret;
}

int get_block_device_sectors_512(const char *path, uint64_t *nr_sec)
{
    int ret;
    uint64_t size;

    ret = get_block_device_size(path, &size);
    if (ret)
        return ret;

    *nr_sec = size / 512;

    return ret;
}

mount_status_t is_blockdev_mounted(const char *blkdev)
{
    FILE *fp;
    char line[1024];
    char *real_blkdev;

    if (!blkdev || !*blkdev)
        return INVALID_MOUNT_STATUS;

    real_blkdev = realpath(blkdev, NULL);
    if (!real_blkdev) {
        LOGE("Error getting real device: %s!.\n", strerror(errno));
        return INVALID_MOUNT_STATUS;
    }

    fp = fopen("/proc/self/mountinfo", "r");
    if (!fp) {
        LOGE("Error opening mountinfo: %s.\n", strerror(errno));
        return INVALID_MOUNT_STATUS;
    }

    /*
     * mountinfo format:
     * 36 25 8:1 / /data rw,relatime - ext4 /dev/dm-1 rw
     */
    while (fgets(line, sizeof(line), fp)) {
        char *sep = strstr(line, " - ");
        if (!sep)
            continue;

        /* After " - " comes: fs_type source mount_opts */
        char *savep;
        char *source = sep + 3;
        char *fs = strtok_r(source, " ", &savep);
        char *dev = strtok_r(NULL, " ", &savep);

        if (!dev)
            continue;

        if (strcmp(dev, real_blkdev) == 0) {
            fclose(fp);
            return MOUNTED;
        }
    }

    fclose(fp);
    return UMOUNTED;

}

static int run_e2fsck(const char *blkdev)
{

    pid_t pid;
    int rc, status;

    /*
     * argv for e2fsck
     *  -f : force check
     *  -y : auto-fix, non-interactive
     */
    char *argv[] = {
        (char *)"e2fsck",
        (char *)"-f",
        (char *)"-y",
        (char *)blkdev,
        NULL
    };

    char *envp[] = {
        (char *)"PATH=/sbin:/bin:/usr/sbin:/usr/bin",
        NULL
    };

    rc = posix_spawnp(&pid, "e2fsck",
                      NULL, NULL, argv, envp);
    if (rc != 0) {
        LOGE("%s: posix_spawnp failed: %s.\n",
             __func__, strerror(rc));
        return -1;
    }

    pid_t w;
    do {
        w = waitpid(pid, &status, 0);
    } while (w < 0 && errno == EINTR);

    if (w < 0) {
        LOGE("%s: waitpid failed: %s.\n",
             __func__, strerror(errno));
        return -1;
    }

    if (!WIFEXITED(status)) {
        LOGE("e2fsck terminated abnormally!\n");
        return -1;
    }

    /*
     * e2fsck exit codes:
     *  0 - no errors
     *  1 - errors corrected
     *  2 - corrected, reboot needed
     *  3 - corrected, filesystem changed
     * >=4 - fatal / uncorrected error
     */
    int exitcode = WEXITSTATUS(status);
    if (exitcode >= 4) {
        LOGE("e2fsck failed, exit=%d\n", exitcode);
        return -1;
    }

    return 0;
}

static int mkdir_p(const char *path, mode_t mode) {
    if (!path || path[0] != '/') {
        errno = EINVAL;
        return -1;
    }

    char tmp[4096];
    if (snprintf(tmp, sizeof(tmp), "%s", path) >= (int)sizeof(tmp)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    // Remove '/' at the end of the string
    size_t n = strlen(tmp);
    while (n > 1 && tmp[n - 1] == '/') tmp[--n] = '\0';

    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) < 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, mode) < 0 && errno != EEXIST) return -1;
    return 0;
}

int mount_blkdev(const char *target, const char *blkdev, const char *fs_type)
{
    if (!target || !blkdev || !fs_type) {
        LOGE("Mounted invalid args!.\n");
        goto err;
    }

    if (run_e2fsck(blkdev)) {
        LOGE("Failed to run e2fsck on %s.\n", blkdev);
        goto err;
    }

    if (!pathExists(target) && mkdir_p(target, 0755) < 0) {
        LOGE("Mkidr %s failed!\n", target);
        goto err;
    }

    unsigned long flags = MS_NOATIME | MS_NOSUID;
    const char *fs_opts = "discard";

    LOGI("mount: blkdev: %s, target: %s, fs_type: %s.\n", blkdev, target, fs_type);

    if (mount(blkdev, target, fs_type, flags, fs_opts) < 0) {
        if (errno == EBUSY) {
            LOGI("%s already mounted on %s\n", blkdev, target);
            return 0;
        }
        LOGE("Mount %s -> %s failed: %s.\n",
             blkdev, target, strerror(errno));
        goto err;
    }

    LOGD("Mounted %s -> %s done!.\n", blkdev, target);
    return 0;

err:
    return -1;
}

static int format_ext4(const char *blkdev)
{
    if (!blkdev || !*blkdev)
        return -1;

    char *const argv[] = {
        (char *)"mkfs.ext4",
        "-F",
        (char *)blkdev,
        NULL
    };
    char *envp[] = {
        (char *)"PATH=/sbin:/bin:/usr/sbin:/usr/bin",
        NULL
    };

    LOGI("%s: formatting %s\n", __func__, blkdev);

    pid_t pid;
    int rc = posix_spawnp(&pid, "mkfs.ext4",
                          NULL, NULL, argv, envp);
    if (rc != 0) {
        errno = rc;
        LOGE("%s: posix_spawnp failed: %s.\n",
             __func__, strerror(rc));
        return -1;
    }

    int status = 0;
    pid_t w;
    do {
        w = waitpid(pid, &status, 0);
    } while (w < 0 && errno == EINTR);

    if (w < 0) {
        LOGE("%s: waitpid failed: %s.\n",
             __func__, strerror(errno));
        return -1;
    }

    if (WIFEXITED(status)) {
        int ec = WEXITSTATUS(status);
        if (ec == 0) {
            LOGI("%s: mkfs.ext4 succeeded on %s.\n",
                 __func__, blkdev);
            return 0;
        }
        errno = EIO;
        LOGE("%s: mkfs.ext4 exited with code %d on %s.\n",
             __func__, ec, blkdev);
        return -1;
    }

    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        errno = EINTR;
        LOGE("%s: mkfs.ext4 killed by signal %d on %s.\n",
             __func__, sig, blkdev);
        return -1;
    }

    errno = EIO;
    LOGE("%s: mkfs.ext4 ended unexpectedly (status=0x%x) on %s\n",
         __func__, status, blkdev);
    return -1;
}

int format_blkdev(const char *blkdev, const char *fs_type)
{
    if (!strcmp(fs_type, "ext4")) {
        return format_ext4(blkdev);
    }

    LOGE("Unsupported file system type: %s", fs_type);
    return -1;
}
