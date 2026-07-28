// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "unit_file_generator.h"
#include "validate_args.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/file.h>
#include <gg/log.h>
#include <gg/object.h>
#include <gg/types.h>
#include <gg/vector.h>
#include <ggl/recipe.h>
#include <ggl/recipe2unit.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_UNIT_FILE_BUF_SIZE 2048
#define MAX_COMPONENT_FILE_NAME 1024

// Builds the unit file path for a lifecycle phase. The returned path borrows a
// static buffer, so it is only valid until the next call.
static GgError unit_file_path(
    Recipe2UnitArgs *args,
    GgObject **component_name,
    PhaseSelection phase,
    GgBuffer *path
) {
    static uint8_t file_name_array[MAX_COMPONENT_FILE_NAME];
    GgBuffer file_name_buffer = (GgBuffer
    ) { .data = (uint8_t *) file_name_array, .len = MAX_COMPONENT_FILE_NAME };

    GgByteVec file_name_vector
        = { .buf = { .data = file_name_buffer.data, .len = 0 },
            .capacity = file_name_buffer.len };

    GgBuffer root_dir_buffer = (GgBuffer) { .data = (uint8_t *) args->root_dir,
                                            .len = strlen(args->root_dir) };

    GgError ret = gg_byte_vec_append(&file_name_vector, root_dir_buffer);
    gg_byte_vec_chain_append(&ret, &file_name_vector, GG_STR("/"));
    gg_byte_vec_chain_append(&ret, &file_name_vector, GG_STR("ggl."));
    gg_byte_vec_chain_append(
        &ret, &file_name_vector, gg_obj_into_buf(**component_name)
    );
    if (phase == INSTALL) {
        gg_byte_vec_chain_append(&ret, &file_name_vector, GG_STR(".install"));
    } else if (phase == BOOTSTRAP) {
        gg_byte_vec_chain_append(&ret, &file_name_vector, GG_STR(".bootstrap"));
    } else {
        // Incase of startup/run nothing to append
        assert(phase == RUN_STARTUP);
    }
    gg_byte_vec_chain_append(&ret, &file_name_vector, GG_STR(".service\0"));
    if (ret != GG_ERR_OK) {
        return ret;
    }

    *path = file_name_vector.buf;
    return GG_ERR_OK;
}

// Remove the unit file of a phase the recipe no longer declares, so that a
// phase dropped by a recipe revision does not leave a stale unit behind.
static GgError remove_unit_file(
    Recipe2UnitArgs *args, GgObject **component_name, PhaseSelection phase
) {
    GgBuffer path = { 0 };
    GgError ret = unit_file_path(args, component_name, phase, &path);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    if ((remove((const char *) path.data) != 0) && (errno != ENOENT)) {
        // Do nothing. The absence of file is okay.
        GG_LOGE("Failed to remove stale unit file: %d.", errno);
        return GG_ERR_FAILURE;
    }

    return GG_ERR_OK;
}

static GgError create_unit_file(
    Recipe2UnitArgs *args,
    GgObject **component_name,
    PhaseSelection phase,
    GgBuffer *response_buffer
) {
    GgBuffer file_name = { 0 };
    GgError ret = unit_file_path(args, component_name, phase, &file_name);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    int fd = -1;
    ret = gg_file_open(file_name, O_WRONLY | O_CREAT | O_TRUNC, 0644, &fd);
    GG_CLEANUP(cleanup_close, fd);

    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to open/create a unit file");
        return GG_ERR_FAILURE;
    }

    ret = gg_file_write(fd, *response_buffer);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to write to the unit file.");
        return GG_ERR_FAILURE;
    }
    return GG_ERR_OK;
}

GgError convert_to_unit(
    Recipe2UnitArgs *args,
    GgArena *alloc,
    GgObject *recipe_obj,
    GgObject **component_name,
    HasPhase *existing_phases
) {
    GgError ret;
    *component_name = NULL;
    *existing_phases = (HasPhase) { 0 };

    ret = validate_args(args);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = ggl_recipe_get_from_file(
        args->root_path_fd,
        args->component_name,
        args->component_version,
        alloc,
        recipe_obj
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("No recipe found");
        return ret;
    }

    // Note: currently, if we have both run and startup phases,
    // we will only select startup for the script and service file
    static uint8_t unit_file_buffer[MAX_UNIT_FILE_BUF_SIZE];

    GgBuffer bootstrap_response_buffer = GG_BUF(unit_file_buffer);
    bootstrap_response_buffer.len = MAX_UNIT_FILE_BUF_SIZE;

    GG_LOGD("Attempting to find bootstrap phase from recipe");
    ret = generate_systemd_unit(
        gg_obj_into_map(*recipe_obj),
        &bootstrap_response_buffer,
        args,
        component_name,
        BOOTSTRAP
    );
    if (*component_name == NULL) {
        GG_LOGE("Component name was NULL");
        return GG_ERR_FAILURE;
    }

    if (ret == GG_ERR_NOENTRY) {
        GG_LOGD("No bootstrap phase present");

    } else if (ret != GG_ERR_OK) {
        return ret;
    } else {
        ret = create_unit_file(
            args, component_name, BOOTSTRAP, &bootstrap_response_buffer
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to create the bootstrap unit file.");
            return ret;
        }
        existing_phases->has_bootstrap = true;
    }

    GgBuffer install_response_buffer = GG_BUF(unit_file_buffer);
    install_response_buffer.len = MAX_UNIT_FILE_BUF_SIZE;

    GgMap recipe = gg_obj_into_map(*recipe_obj);

    GG_LOGD("Attempting to find install phase from recipe");
    ret = generate_systemd_unit(
        recipe, &install_response_buffer, args, component_name, INSTALL
    );
    if (*component_name == NULL) {
        GG_LOGE("Component name was NULL");
        return GG_ERR_FAILURE;
    }

    if (ret == GG_ERR_NOENTRY) {
        GG_LOGD("No Install phase present");

    } else if (ret != GG_ERR_OK) {
        return ret;
    } else {
        ret = create_unit_file(
            args, component_name, INSTALL, &install_response_buffer
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to create the install unit file.");
            return ret;
        }
        existing_phases->has_install = true;
    }

    GgBuffer run_startup_response_buffer = GG_BUF(unit_file_buffer);
    run_startup_response_buffer.len = MAX_UNIT_FILE_BUF_SIZE;

    GG_LOGD("Attempting to find run phase from recipe");
    ret = generate_systemd_unit(
        recipe, &run_startup_response_buffer, args, component_name, RUN_STARTUP
    );
    if (ret == GG_ERR_NOENTRY) {
        GG_LOGD("Neither run nor startup phase present");
    } else if (ret != GG_ERR_OK) {
        return ret;
    } else {
        ret = create_unit_file(
            args, component_name, RUN_STARTUP, &run_startup_response_buffer
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to create the run or startup unit file.");
            return ret;
        }
        GG_LOGD("Created run or startup unit file.");
        existing_phases->has_run_startup = true;
    }

    if (existing_phases->has_bootstrap == false
        && existing_phases->has_install == false
        && existing_phases->has_run_startup == false) {
        GG_LOGE(
            "Recipes without at least 1 valid lifecycle step aren't currently supported by Greengrass nucleus lite"
        );

        GG_LOGW(
            "Note that in Greengrass nucleus lite, keys are case sensitive. Check the recipe reference for the correct casing."
        );
        return GG_ERR_INVALID;
    }

    // A revised recipe may drop a phase that a previous revision declared.
    // Remove those unit files, otherwise the stale phase is still run.
    // Run/startup is deliberately excluded: its unit is the only one carrying
    // WantedBy=greengrass-lite.target, so removing the file without also
    // dropping that enablement would leave a dangling symlink behind.
    if (!existing_phases->has_bootstrap) {
        (void) remove_unit_file(args, component_name, BOOTSTRAP);
    }
    if (!existing_phases->has_install) {
        (void) remove_unit_file(args, component_name, INSTALL);
    }

    return GG_ERR_OK;
}

#ifdef GG_SDK_TESTING

#include <gg/test.h>
#include <ftw.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <unity.h>

#define TEST_COMPONENT "com.example.StalePhase"

// Deployment 1 from the issue: install and run both present.
static const char *const test_recipe_v1
    = "---\n"
      "RecipeFormatVersion: \"2020-01-25\"\n"
      "ComponentName: " TEST_COMPONENT "\n"
      "ComponentVersion: \"1.0.0\"\n"
      "Manifests:\n"
      "  - Platform:\n"
      "      os: linux\n"
      "      runtime: \"aws_nucleus_lite\"\n"
      "    Lifecycle:\n"
      "      install: \"echo Hello World!\"\n"
      "      run: \"echo Hello World!\"\n";

// Deployment 2: a revision of the above with install dropped and run
// replaced by startup.
static const char *const test_recipe_v1_1
    = "---\n"
      "RecipeFormatVersion: \"2020-01-25\"\n"
      "ComponentName: " TEST_COMPONENT "\n"
      "ComponentVersion: \"1.1.0\"\n"
      "Manifests:\n"
      "  - Platform:\n"
      "      os: linux\n"
      "      runtime: \"aws_nucleus_lite\"\n"
      "    Lifecycle:\n"
      "      startup: \"echo Hello World!\"\n";

// A bootstrap phase alongside run, for the dropped-bootstrap case.
static const char *const test_recipe_bootstrap
    = "---\n"
      "RecipeFormatVersion: \"2020-01-25\"\n"
      "ComponentName: " TEST_COMPONENT "\n"
      "ComponentVersion: \"1.0.0\"\n"
      "Manifests:\n"
      "  - Platform:\n"
      "      os: linux\n"
      "      runtime: \"aws_nucleus_lite\"\n"
      "    Lifecycle:\n"
      "      bootstrap: \"echo Hello World!\"\n"
      "      run: \"echo Hello World!\"\n";

// No supported lifecycle phase, so conversion is rejected.
static const char *const test_recipe_no_phase
    = "---\n"
      "RecipeFormatVersion: \"2020-01-25\"\n"
      "ComponentName: " TEST_COMPONENT "\n"
      "ComponentVersion: \"1.1.0\"\n"
      "Manifests:\n"
      "  - Platform:\n"
      "      os: linux\n"
      "      runtime: \"aws_nucleus_lite\"\n"
      "    Lifecycle:\n"
      "      setEnv:\n"
      "        UNUSED: \"1\"\n";

static void write_test_recipe(
    const char *root_dir, const char *version, const char *body
) {
    char path[PATH_MAX];
    (void) snprintf(
        path,
        sizeof(path),
        "%s/packages/recipes/" TEST_COMPONENT "-%s.yml",
        root_dir,
        version
    );
    FILE *file = fopen(path, "w");
    TEST_ASSERT_NOT_NULL(file);
    size_t len = strlen(body);
    TEST_ASSERT_TRUE(fwrite(body, 1, len, file) == len);
    TEST_ASSERT_TRUE(fclose(file) == 0);
}

static bool test_unit_exists(const char *root_dir, const char *suffix) {
    char path[PATH_MAX];
    (void) snprintf(
        path,
        sizeof(path),
        "%s/ggl." TEST_COMPONENT "%s.service",
        root_dir,
        suffix
    );
    return access(path, F_OK) == 0;
}

static GgError run_test_convert(
    const char *root_dir, GgBuffer version, HasPhase *existing_phases
) {
    static Recipe2UnitArgs args;
    args = (Recipe2UnitArgs) { 0 };

    struct passwd *user_info = getpwuid(getuid());
    TEST_ASSERT_NOT_NULL(user_info);
    struct group *group_info = getgrgid(getgid());
    TEST_ASSERT_NOT_NULL(group_info);
    args.user = user_info->pw_name;
    args.group = group_info->gr_name;

    args.component_name = GG_STR(TEST_COMPONENT);
    args.component_version = version;
    memcpy(args.root_dir, root_dir, strlen(root_dir) + 1);
    memcpy(args.recipe_runner_path, "/bin/sh", sizeof("/bin/sh"));

    GgError ret = gg_dir_open(
        gg_buffer_from_null_term(args.root_dir),
        O_PATH,
        false,
        &args.root_path_fd
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GgObject recipe_obj;
    GgObject *component_name = NULL;
    static uint8_t recipe_mem[50000];
    GgArena alloc = gg_arena_init(GG_BUF(recipe_mem));

    ret = convert_to_unit(
        &args, &alloc, &recipe_obj, &component_name, existing_phases
    );
    (void) gg_close(args.root_path_fd);
    return ret;
}

static int test_unlink_cb(
    const char *path, const struct stat *sb, int type, struct FTW *ftw
) {
    (void) sb;
    (void) type;
    (void) ftw;
    return remove(path);
}

static void remove_test_root(const char *root_dir) {
    (void) nftw(root_dir, test_unlink_cb, 8, FTW_DEPTH | FTW_PHYS);
}

static void make_test_root(char *root_dir, size_t len) {
    (void) snprintf(root_dir, len, "/tmp/ggl-recipe2unit-XXXXXX");
    TEST_ASSERT_NOT_NULL(mkdtemp(root_dir));

    char dir[PATH_MAX];
    (void) snprintf(dir, sizeof(dir), "%s/packages", root_dir);
    TEST_ASSERT_TRUE(mkdir(dir, 0755) == 0);
    (void) snprintf(dir, sizeof(dir), "%s/packages/recipes", root_dir);
    TEST_ASSERT_TRUE(mkdir(dir, 0755) == 0);
}

GG_TEST_DEFINE(recipe_revision_removes_dropped_install_unit) {
    char root_dir[PATH_MAX];
    make_test_root(root_dir, sizeof(root_dir));

    write_test_recipe(root_dir, "1.0.0", test_recipe_v1);
    HasPhase phases = { 0 };
    GG_TEST_ASSERT_OK(run_test_convert(root_dir, GG_STR("1.0.0"), &phases));
    TEST_ASSERT_TRUE(phases.has_install);
    TEST_ASSERT_TRUE(test_unit_exists(root_dir, ".install"));
    TEST_ASSERT_TRUE(test_unit_exists(root_dir, ""));

    write_test_recipe(root_dir, "1.1.0", test_recipe_v1_1);
    HasPhase revised = { 0 };
    GG_TEST_ASSERT_OK(run_test_convert(root_dir, GG_STR("1.1.0"), &revised));
    TEST_ASSERT_TRUE(test_unit_exists(root_dir, ""));
    TEST_ASSERT_FALSE(revised.has_install);
    TEST_ASSERT_FALSE(test_unit_exists(root_dir, ".install"));

    remove_test_root(root_dir);
}

GG_TEST_DEFINE(recipe_revision_removes_dropped_bootstrap_unit) {
    char root_dir[PATH_MAX];
    make_test_root(root_dir, sizeof(root_dir));

    write_test_recipe(root_dir, "1.0.0", test_recipe_bootstrap);
    HasPhase phases = { 0 };
    GG_TEST_ASSERT_OK(run_test_convert(root_dir, GG_STR("1.0.0"), &phases));
    TEST_ASSERT_TRUE(phases.has_bootstrap);
    TEST_ASSERT_TRUE(test_unit_exists(root_dir, ".bootstrap"));

    write_test_recipe(root_dir, "1.1.0", test_recipe_v1_1);
    HasPhase revised = { 0 };
    GG_TEST_ASSERT_OK(run_test_convert(root_dir, GG_STR("1.1.0"), &revised));
    TEST_ASSERT_FALSE(revised.has_bootstrap);
    TEST_ASSERT_FALSE(test_unit_exists(root_dir, ".bootstrap"));

    remove_test_root(root_dir);
}

// A recipe with no supported lifecycle phase is rejected, and must not take
// the previous revision's unit files down with it.
GG_TEST_DEFINE(invalid_recipe_keeps_existing_units) {
    char root_dir[PATH_MAX];
    make_test_root(root_dir, sizeof(root_dir));

    write_test_recipe(root_dir, "1.0.0", test_recipe_v1);
    HasPhase phases = { 0 };
    GG_TEST_ASSERT_OK(run_test_convert(root_dir, GG_STR("1.0.0"), &phases));
    TEST_ASSERT_TRUE(test_unit_exists(root_dir, ".install"));
    TEST_ASSERT_TRUE(test_unit_exists(root_dir, ""));

    write_test_recipe(root_dir, "1.1.0", test_recipe_no_phase);
    HasPhase revised = { 0 };
    TEST_ASSERT_EQUAL(
        GG_ERR_INVALID, run_test_convert(root_dir, GG_STR("1.1.0"), &revised)
    );
    TEST_ASSERT_TRUE(test_unit_exists(root_dir, ".install"));
    TEST_ASSERT_TRUE(test_unit_exists(root_dir, ""));

    remove_test_root(root_dir);
}

#endif
