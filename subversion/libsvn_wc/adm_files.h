/*
 * adm_files.h :  handles locations inside the wc adm area
 *                (This should be the only code that actually knows
 *                *where* things are in .svn/.  If you can't get to
 *                something via these interfaces, something's wrong.)
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */


#ifndef SVN_LIBSVN_WC_ADM_FILES_H
#define SVN_LIBSVN_WC_ADM_FILES_H

#include <apr_pools.h>
#include "svn_types.h"

#include "props.h"
#include "wc_db.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* Return a path to CHILD in the administrative area of PATH. If CHILD is
   NULL, then the path to the admin area is returned. The result is
   allocated in RESULT_POOL. */
const char *svn_wc__adm_child(const char *path,
                              const char *child,
                              apr_pool_t *result_pool);

/* Return TRUE if the administrative area exists for this directory. */
svn_boolean_t svn_wc__adm_area_exists(const char *adm_abspath,
                                      apr_pool_t *pool);


/* Atomically rename a temporary text-base file TMP_TEXT_BASE_ABSPATH to its
   canonical location.  LOCAL_ABSPATH is the path of the working file whose
   text-base is to be moved.  The tmp file should be closed already. */
svn_error_t *
svn_wc__sync_text_base(const char *local_abspath,
                       const char *tmp_text_base_path,
                       apr_pool_t *pool);


/* Set *RESULT_ABSPATH to the absolute path to where LOCAL_ABSPATH's
   text-base file is or should be created.  The file does not necessarily
   exist. */
svn_error_t *
svn_wc__text_base_path(const char **result_abspath,
                       svn_wc__db_t *db,
                       const char *local_abspath,
                       apr_pool_t *pool);

/* Set *RESULT_ABSPATH to the deterministic absolute path to where
   LOCAL_ABSPATH's temporary text-base file is or should be created. */
svn_error_t *
svn_wc__text_base_deterministic_tmp_path(const char **result_abspath,
                                         svn_wc__db_t *db,
                                         const char *local_abspath,
                                         apr_pool_t *pool);

/* Set *CONTENTS to a readonly stream on the pristine text of the working
 * version of the file LOCAL_ABSPATH in DB.  If the file is locally copied
 * or moved to this path, this means the pristine text of the copy source,
 * even if the file replaces a previously existing base node at this path.
 *
 * Set *CONTENTS to NULL if there is no pristine text because the file is
 * locally added (even if it replaces an existing base node).  Return an
 * error if there is no pristine text for any other reason.
 *
 * For more detail, see the description of svn_wc_get_pristine_contents2().
 */
svn_error_t *
svn_wc__get_pristine_contents(svn_stream_t **contents,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool);


/* Set *CONTENTS to a readonly stream on the pristine text of the base
 * version of LOCAL_ABSPATH in DB.  If LOCAL_ABSPATH is locally replaced,
 * this is distinct from svn_wc__get_pristine_contents(), otherwise it is
 * the same.
 *
 * (In WC-1 terminology, this was known as "the revert base" if the node is
 * replaced by a copy, otherwise simply as "the base".)
 *
 * If the base version of LOCAL_ABSPATH is not present (e.g. because the
 * file is locally added), set *CONTENTS to NULL.
 * The base version of LOCAL_ABSPATH must be a file. */
svn_error_t *
svn_wc__get_pristine_base_contents(svn_stream_t **contents,
                                   svn_wc__db_t *db,
                                   const char *local_abspath,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool);


/* Set *RESULT_ABSPATH to the absolute path to LOCAL_ABSPATH's revert file. */
svn_error_t *
svn_wc__text_revert_path(const char **result_abspath,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         apr_pool_t *pool);

/* Set *PROP_PATH to PATH's PROPS_KIND properties file.
   PATH can be a directory or file, and even have changed w.r.t. the
   working copy's adm knowledge. Valid values for NODE_KIND are svn_node_dir
   and svn_node_file. */
svn_error_t *svn_wc__prop_path(const char **prop_path,
                               const char *path,
                               svn_wc__db_kind_t node_kind,
                               svn_wc__props_kind_t props_kind,
                               apr_pool_t *pool);

/* Set *RESULT_ABSPATH to the absolute path to a readable file containing
   the WORKING_NODE pristine text of LOCAL_ABSPATH in DB.

   The implementation might create the file as an independent copy on
   demand, or it might return the path to a shared file.  The file will
   remain readable until RESULT_POOL is cleared or until LOCAL_ABSPATH's WC
   metadata is next changed, whichever is sooner.
     (### The latter doesn't sound like a totally reasonable condition.)
   After that, if the implementation provided a separate file then the file
   will automatically be removed, or if it provided a shared file then
   no guarantees are made about it after this time.

   ### The present implementation just returns the path to the file in the
     pristine store and does not make any attempt to ensure its lifetime is
     as promised.

   If the node LOCAL_ABSPATH has no pristine text, return an error.

   Allocate *RESULT_PATH in RESULT_POOL.  */
svn_error_t *
svn_wc__get_working_node_pristine_file(const char **result_abspath,
                                       svn_wc__db_t *db,
                                       const char *local_abspath,
                                       apr_pool_t *result_pool);



/*** Opening all kinds of adm files ***/

/* Open `PATH/<adminstrative_subdir>/FNAME'. */
svn_error_t *svn_wc__open_adm_stream(svn_stream_t **stream,
                                     const char *dir_abspath,
                                     const char *fname,
                                     apr_pool_t *result_pool,
                                     apr_pool_t *scratch_pool);


/* Open a writable stream to a temporary (normal or revert) text base,
   associated with the versioned file LOCAL_ABSPATH in DB.  Set *STREAM to
   the opened stream and *TEMP_BASE_ABSPATH to the path to the temporary
   file.  The temporary file will have an arbitrary unique name, in contrast
   to the deterministic name that svn_wc__text_base_deterministic_tmp_path()
   returns.

   Arrange that, on stream closure, *MD5_CHECKSUM and *SHA1_CHECKSUM will be
   set to the MD-5 and SHA-1 checksums respectively of that file.
   MD5_CHECKSUM and/or SHA1_CHECKSUM may be NULL if not wanted.

   Allocate the new stream, path and checksums in RESULT_POOL.
 */
svn_error_t *
svn_wc__open_writable_base(svn_stream_t **stream,
                           const char **temp_base_abspath,
                           svn_checksum_t **md5_checksum,
                           svn_checksum_t **sha1_checksum,
                           svn_wc__db_t *db,
                           const char *local_abspath,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);


/* Blow away the admistrative directory associated with DIR_ABSPATH */
svn_error_t *svn_wc__adm_destroy(svn_wc__db_t *db,
                                 const char *dir_abspath,
                                 apr_pool_t *scratch_pool);


/* Cleanup the temporary storage area of the administrative
   directory (assuming temp and admin areas exist). */
svn_error_t *
svn_wc__adm_cleanup_tmp_area(svn_wc__db_t *db,
                             const char *adm_abspath,
                             apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_ADM_FILES_H */
