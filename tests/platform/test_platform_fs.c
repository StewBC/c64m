#include "platform_fs.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#define c64m_mkdir(path) _mkdir(path)
#define c64m_rmdir _rmdir
#else
#include <unistd.h>
#define c64m_mkdir(path) mkdir(path, 0777)
#define c64m_rmdir rmdir
#endif

static void expect_true(const char *name, bool value) {
    if (!value) {
        fprintf(stderr, "%s: expected true\n", name);
        exit(1);
    }
}

static void expect_false(const char *name, bool value) {
    if (value) {
        fprintf(stderr, "%s: expected false\n", name);
        exit(1);
    }
}

static void expect_str(const char *name, const char *expected, const char *actual) {
    if (strcmp(expected, actual) != 0) {
        fprintf(stderr, "%s: expected `%s`, got `%s`\n", name, expected, actual);
        exit(1);
    }
}

static void write_small_file(const char *path) {
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        fprintf(stderr, "failed to create scratch file %s\n", path);
        exit(1);
    }
    fwrite("hi", 1, 2, fp);
    fclose(fp);
}

static void test_path_join(void) {
    char out[256];

    platform_fs_path_join(out, sizeof(out), "some/dir", "file.txt");
    expect_true("joined without trailing separator", strstr(out, "file.txt") != NULL);
    expect_true("joined keeps dir prefix", strstr(out, "some") != NULL);

    platform_fs_path_join(out, sizeof(out), "some/dir/", "file.txt");
    expect_false("no doubled separator", strstr(out, "//") != NULL || strstr(out, "\\\\") != NULL);
}

static void test_get_cwd(void) {
    char out[1024];

    expect_true("get_cwd succeeds", platform_fs_get_cwd(out, sizeof(out)));
    expect_true("get_cwd non-empty", out[0] != '\0');
}

static void test_list_dir_missing_returns_false(void) {
    platform_fs_listing listing;

    expect_false(
        "listing a nonexistent directory fails",
        platform_fs_list_dir("test_platform_fs_does_not_exist", &listing));
}

static void test_list_dir_sorts_and_reports_type(void) {
    platform_fs_listing listing;
    int i;
    int dot_dot_index = -1;
    int first_dir_index = -1;
    int first_file_index = -1;

    c64m_mkdir("test_platform_fs_scratch");
    c64m_mkdir("test_platform_fs_scratch/zzz_dir");
    c64m_mkdir("test_platform_fs_scratch/AAA_dir");
    write_small_file("test_platform_fs_scratch/b.txt");
    write_small_file("test_platform_fs_scratch/A.txt");

    expect_true(
        "listing the scratch directory succeeds",
        platform_fs_list_dir("test_platform_fs_scratch", &listing));
    expect_false("listing is not truncated", listing.truncated);

    for (i = 0; i < listing.count; i++) {
        if (strcmp(listing.entries[i].name, "..") == 0) {
            dot_dot_index = i;
        } else if (listing.entries[i].is_dir && first_dir_index < 0) {
            first_dir_index = i;
        } else if (!listing.entries[i].is_dir && first_file_index < 0) {
            first_file_index = i;
        }
    }

    expect_true("scratch listing includes ..", dot_dot_index >= 0);
    expect_true("scratch listing includes a directory", first_dir_index >= 0);
    expect_true("scratch listing includes a file", first_file_index >= 0);
    expect_true(".. sorts before directories", dot_dot_index < first_dir_index);
    expect_true("directories sort before files", first_dir_index < first_file_index);
    expect_str("directories sort case-insensitively", "AAA_dir", listing.entries[first_dir_index].name);
    expect_str("files sort case-insensitively", "A.txt", listing.entries[first_file_index].name);

    remove("test_platform_fs_scratch/b.txt");
    remove("test_platform_fs_scratch/A.txt");
    c64m_rmdir("test_platform_fs_scratch/zzz_dir");
    c64m_rmdir("test_platform_fs_scratch/AAA_dir");
    c64m_rmdir("test_platform_fs_scratch");
}

static void test_is_dir(void) {
    c64m_mkdir("test_platform_fs_isdir");
    write_small_file("test_platform_fs_isdir_file.txt");

    expect_true("directory reports is_dir", platform_fs_is_dir("test_platform_fs_isdir"));
    expect_false("regular file is not is_dir", platform_fs_is_dir("test_platform_fs_isdir_file.txt"));
    expect_false("missing path is not is_dir", platform_fs_is_dir("test_platform_fs_does_not_exist"));

    remove("test_platform_fs_isdir_file.txt");
    c64m_rmdir("test_platform_fs_isdir");
}

int main(void) {
    test_path_join();
    test_get_cwd();
    test_list_dir_missing_returns_false();
    test_list_dir_sorts_and_reports_type();
    test_is_dir();
    return 0;
}
