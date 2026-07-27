#ifndef C64M_TEST_ASSET_H
#define C64M_TEST_ASSET_H

#include <stdio.h>

/* Exit code CTest interprets as "skipped" for tests marked with the
   SKIP_RETURN_CODE property (see CMakeLists.txt). */
#define C64M_TEST_SKIP 77

/* Media under assets/ (disks, tapes, CRTs) is copyrighted and therefore
   gitignored, so it is absent on clean checkouts, git worktrees, and CI. A test
   that requires such a file must SKIP (return C64M_TEST_SKIP) rather than FAIL
   when it is missing, so those environments get a clean run instead of spurious
   failures. Returns 1 (and logs a skip line) when the asset is not present,
   else 0. */
static inline int c64m_test_asset_missing(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file != NULL) {
        fclose(file);
        return 0;
    }
    fprintf(stderr, "SKIP: required asset not present: %s\n", path);
    return 1;
}

#endif
