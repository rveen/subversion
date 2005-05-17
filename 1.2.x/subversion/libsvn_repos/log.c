/* log.c --- retrieving log messages
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */


#define APR_WANT_STRFUNC
#include <apr_want.h>

#include "svn_private_config.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_string.h"
#include "svn_sorts.h"
#include "svn_props.h"
#include "repos.h"
#include "svn_utf.h"



/* Store as keys in CHANGED the paths of all node in ROOT that show a
 * significant change.  "Significant" means that the text or
 * properties of the node were changed, or that the node was added or
 * deleted.
 *
 * The CHANGED hash set and the key are allocated in POOL;
 * the value is (void *) 'U', 'A', 'D', or 'R', for modified, added,
 * deleted, or replaced, respectively.
 * 
 * If optional AUTHZ_READ_FUNC is non-NULL, then use it (with
 * AUTHZ_READ_BATON and FS) to check whether each changed-path (and
 * copyfrom_path) is readable:
 *
 *     - If some paths are readable and some are not, then silently
 *     omit the unreadable paths from the CHANGED hash, and return
 *     SVN_ERR_AUTHZ_PARTIALLY_READABLE.
 *
 *     - If absolutely every changed-path (and copyfrom_path) is
 *     unreadable, then return an empty CHANGED hash and
 *     SVN_ERR_AUTHZ_UNREADABLE.  (This is to distinguish a revision
 *     which truly has no changed paths from a revision in which all
 *     paths are unreadable.)
 */
static svn_error_t *
detect_changed (apr_hash_t **changed,
                svn_fs_root_t *root,
                svn_fs_t *fs,
                svn_repos_authz_func_t authz_read_func,
                void *authz_read_baton,
                apr_pool_t *pool)
{
  apr_hash_t *changes;
  apr_hash_index_t *hi;
  apr_pool_t *subpool = svn_pool_create (pool);
  svn_boolean_t found_readable = FALSE;
  svn_boolean_t found_unreadable = FALSE;

  *changed = apr_hash_make (pool);
  SVN_ERR (svn_fs_paths_changed (&changes, root, pool));

  if (apr_hash_count (changes) == 0)
    /* No paths changed in this revision?  Uh, sure, I guess the
       revision is readable, then.  */
    return SVN_NO_ERROR;

  for (hi = apr_hash_first (pool, changes); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      svn_fs_path_change_t *change;
      const char *path;
      char action;
      svn_log_changed_path_t *item;

      svn_pool_clear (subpool);

      /* KEY will be the path, VAL the change. */
      apr_hash_this (hi, &key, NULL, &val);
      path = (const char *) key;
      change = val;

      /* Skip path if unreadable. */
      if (authz_read_func)
        {
          svn_boolean_t readable;
          SVN_ERR (authz_read_func (&readable,
                                    root, path,
                                    authz_read_baton, subpool));
          if (! readable)
            {
              found_unreadable = TRUE;
              continue;
            }
        }

      /* At least one changed-path was readable. */
      found_readable = TRUE;

      switch (change->change_kind)
        {
        case svn_fs_path_change_reset:
          continue;

        case svn_fs_path_change_add:
          action = SVN_UTF8_A;
          break;

        case svn_fs_path_change_replace:
          action = SVN_UTF8_R;
          break;

        case svn_fs_path_change_delete:
          action = SVN_UTF8_D;
          break;

        case svn_fs_path_change_modify:
        default:
          action = SVN_UTF8_M;
          break;
        }

      item = apr_pcalloc (pool, sizeof (*item));
      item->action = action;
      item->copyfrom_rev = SVN_INVALID_REVNUM;
      if ((action == SVN_UTF8_A) || (action == SVN_UTF8_R))
        {
          const char *copyfrom_path;
          svn_revnum_t copyfrom_rev;

          SVN_ERR (svn_fs_copied_from (&copyfrom_rev, &copyfrom_path,
                                       root, path, subpool));

          if (copyfrom_path && SVN_IS_VALID_REVNUM (copyfrom_rev))
            {
              svn_boolean_t readable = TRUE;

              if (authz_read_func)
                {
                  svn_fs_root_t *copyfrom_root;
                  
                  SVN_ERR (svn_fs_revision_root (&copyfrom_root, fs,
                                                 copyfrom_rev, subpool));
                  SVN_ERR (authz_read_func (&readable,
                                            copyfrom_root, copyfrom_path,
                                            authz_read_baton, subpool));
                  if (! readable)
                    found_unreadable = TRUE;
                }

              if (readable)
                {
                  item->copyfrom_path = apr_pstrdup (pool, copyfrom_path);
                  item->copyfrom_rev = copyfrom_rev;
                }
            }
        }
      apr_hash_set (*changed, apr_pstrdup (pool, path), 
                    APR_HASH_KEY_STRING, item);
    }

  svn_pool_destroy (subpool);

  if (! found_readable)
    /* Every changed-path was unreadable. */
    return svn_error_create (SVN_ERR_AUTHZ_UNREADABLE,
                             NULL, NULL);

  if (found_unreadable)
    /* At least one changed-path was unreadable. */
    return svn_error_create (SVN_ERR_AUTHZ_PARTIALLY_READABLE,
                             NULL, NULL);

  /* Every changed-path was readable. */
  return SVN_NO_ERROR;
}

/* This is used by svn_repos_get_logs3 to get a revision
   root for a path. */
static svn_error_t *
path_history_root (svn_fs_root_t **root,
                   svn_fs_t *fs,
                   const char* path,
                   svn_revnum_t rev,
                   svn_repos_authz_func_t authz_read_func,
                   void* authz_read_baton,
                   apr_pool_t* pool)
{
  /* Get a revision root for REV. */
  SVN_ERR (svn_fs_revision_root (root, fs, rev, pool));

  if (authz_read_func)
    {
      svn_boolean_t readable;
      SVN_ERR (authz_read_func (&readable, *root, path,
                                authz_read_baton, pool));
      if (! readable)
        return svn_error_create (SVN_ERR_AUTHZ_UNREADABLE, NULL, NULL);
    }
  return SVN_NO_ERROR;
}

/* This is used by svn_repos_get_logs3 to keep track of multiple
   path history information while working through history. */
struct path_info
{
  svn_fs_root_t *root;
  const char *path;
  svn_fs_history_t *hist;
  apr_pool_t *newpool;
  apr_pool_t *oldpool;
  svn_revnum_t history_rev;
};

/* This helper to svn_repos_get_logs3 is used to get the path's
   history. */
static svn_error_t *
get_history (struct path_info *info,
             svn_fs_t *fs,
             svn_boolean_t cross_copies,
             svn_repos_authz_func_t authz_read_func,
             void *authz_read_baton,
             svn_revnum_t start)
{
  apr_pool_t *temppool;

  SVN_ERR (svn_fs_history_prev (&info->hist, info->hist, cross_copies,
                                info->newpool));
  if (! info->hist)
    return SVN_NO_ERROR;

  /* Fetch the location information for this history step. */
  SVN_ERR (svn_fs_history_location (&info->path, &info->history_rev,
                                    info->hist, info->newpool));
  
  /* Is the history item readable?  If not, done with path. */
  if (authz_read_func)
    {
      svn_boolean_t readable;
      svn_fs_root_t *history_root;
      SVN_ERR (svn_fs_revision_root (&history_root, fs,
                                     info->history_rev,
                                     info->newpool));
      SVN_ERR (authz_read_func (&readable, history_root,
                                info->path,
                                authz_read_baton,
                                info->newpool));
      if (! readable)
        info->hist = NULL;
    }

  /* Now we can clear the old pool. */
  temppool = info->oldpool;
  info->oldpool = info->newpool;
  svn_pool_clear (temppool);
  info->newpool = temppool;

  /* If this history item predates our START revision then
     don't fetch any more for this path. */
  if (info->history_rev < start)
    info->hist = NULL;
  return SVN_NO_ERROR;
}

/* This helper to svn_repos_get_logs3 is used to advance the path's
   history to the next one *if* the revision it is at is equal to
   or greater than CURRENT. The CHANGED flag is only touched if
   this path has history in the CURRENT rev. */
static svn_error_t *
check_history (svn_boolean_t *changed,
               struct path_info *info,
               svn_fs_t *fs,
               svn_revnum_t current,
               svn_boolean_t cross_copies,
               svn_repos_authz_func_t authz_read_func,
               void *authz_read_baton,
               svn_revnum_t start)
{
  /* If we're already done with histories for this path,
     don't try to fetch any more. */
  if (! info->hist)
    return SVN_NO_ERROR;

  /* If the last rev we got for this path is less than CURRENT,
     then just return and don't fetch history for this path.
     The caller will get to this rev eventually or else reach
     the limit. */
  if (info->history_rev < current)
    return SVN_NO_ERROR;

  /* If the last rev we got for this path is equal to CURRENT
     then set *CHANGED to true and get the next history
     rev where this path was changed. */
  *changed = TRUE;
  SVN_ERR (get_history (info, fs, cross_copies, authz_read_func,
                        authz_read_baton, start));
  return SVN_NO_ERROR;
}

/* Helper to find the next interesting history revision. */
static svn_revnum_t
next_history_rev (apr_array_header_t *histories)
{
  svn_revnum_t next_rev = -1;
  int i;

  for (i = 0; i < histories->nelts; ++i)
    {
      struct path_info *info = APR_ARRAY_IDX (histories, i,
                                              struct path_info *);
      if (! info->hist)
        continue;
      if (info->history_rev > next_rev)
        next_rev = info->history_rev;
    }

  return next_rev;
}

/* Helper function used by svn_repos_get_logs3 to send history
   info to the caller's callback. */
static svn_error_t *
send_change_rev (svn_revnum_t rev,
                 svn_fs_t *fs,
                 svn_boolean_t discover_changed_paths,
                 svn_repos_authz_func_t authz_read_func,
                 void *authz_read_baton,
                 svn_log_message_receiver_t receiver,
                 void *receiver_baton,
                 apr_pool_t *pool)
{
  svn_string_t *author, *date, *message;
  apr_hash_t *r_props, *changed_paths = NULL;

  SVN_ERR (svn_fs_revision_proplist (&r_props, fs, rev, pool));
  author = apr_hash_get (r_props, SVN_PROP_REVISION_AUTHOR,
                         APR_HASH_KEY_STRING);
  date = apr_hash_get (r_props, SVN_PROP_REVISION_DATE,
                       APR_HASH_KEY_STRING);
  message = apr_hash_get (r_props, SVN_PROP_REVISION_LOG,
                          APR_HASH_KEY_STRING);

  /* Discover changed paths if the user requested them
     or if we need to check that they are readable. */
  if ((rev > 0)        
      && (authz_read_func || discover_changed_paths))
    {
      svn_fs_root_t *newroot;
      svn_error_t *patherr;

      SVN_ERR (svn_fs_revision_root (&newroot, fs, rev, pool));
      patherr = detect_changed (&changed_paths,
                                newroot, fs,
                                authz_read_func, authz_read_baton,
                                pool);

      if (patherr
          && patherr->apr_err == SVN_ERR_AUTHZ_UNREADABLE)
        {
          /* All changed-paths are unreadable, so clear all fields. */
          svn_error_clear (patherr);              
          changed_paths = NULL;
          author = NULL;
          date = NULL;
          message = NULL;
        }
      else if (patherr
               && patherr->apr_err == SVN_ERR_AUTHZ_PARTIALLY_READABLE)
        {
          /* At least one changed-path was unreadable, so omit the
             log message.  (The unreadable paths are already
             missing from the hash.) */
          svn_error_clear (patherr);
          message = NULL;
        }
      else if (patherr)
        return patherr;

      /* It may be the case that an authz func was passed in, but
         the user still doesn't want to see any changed-paths. */
      if (! discover_changed_paths)
        changed_paths = NULL;
    }

  SVN_ERR ((*receiver) (receiver_baton,
                        changed_paths,
                        rev,
                        author ? author->data : NULL,
                        date ? date->data : NULL,
                        message ? message->data : NULL,
                        pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos_get_logs3 (svn_repos_t *repos,
                     const apr_array_header_t *paths,
                     svn_revnum_t start,
                     svn_revnum_t end,
                     int limit,
                     svn_boolean_t discover_changed_paths,
                     svn_boolean_t strict_node_history,
                     svn_repos_authz_func_t authz_read_func,
                     void *authz_read_baton,
                     svn_log_message_receiver_t receiver,
                     void *receiver_baton,
                     apr_pool_t *pool)
{
  svn_revnum_t head = SVN_INVALID_REVNUM;
  apr_pool_t *subpool = svn_pool_create (pool);
  apr_pool_t *sendpool = svn_pool_create (pool);
  svn_fs_t *fs = repos->fs;
  apr_array_header_t *revs = NULL;
  svn_revnum_t hist_end = end;
  svn_revnum_t hist_start = start;
  int i;

  SVN_ERR (svn_fs_youngest_rev (&head, fs, pool));

  if (! SVN_IS_VALID_REVNUM (start))
    start = head;

  if (! SVN_IS_VALID_REVNUM (end))
    end = head;

  /* Check that revisions are sane before ever invoking receiver. */
  if (start > head)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_REVISION, 0,
       _("No such revision %ld"), start);
  if (end > head)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_REVISION, 0,
       _("No such revision %ld"), end);

  /* Get an ordered copy of the start and end. */
  if (start > end)
    {
      hist_start = end;
      hist_end = start;
    }

  /* If paths were specified, then we only really care about revisions
     in which those paths were changed.  So we ask the filesystem for
     all the revisions in which any of the paths was changed.

     SPECIAL CASE: If we were given only path, and that path is empty,
     then the results are the same as if we were passed no paths at
     all.  Why?  Because the answer to the question "In which
     revisions was the root of the filesystem changed?" is always
     "Every single one of them."  And since this section of code is
     only about answering that question, and we already know the
     answer ... well, you get the picture.
  */
  if (paths 
      && (((paths->nelts == 1) 
           && (! svn_path_is_empty (APR_ARRAY_IDX (paths, 0, const char *))))
          || (paths->nelts > 1)))
    {
      svn_revnum_t current;
      apr_array_header_t *histories;
      svn_boolean_t any_histories_left = TRUE;
      int sent_count = 0;

      histories = apr_array_make (subpool, paths->nelts,
                                  sizeof (struct path_info *));
      /* Create a history object for each path so we can walk through
         them all at the same time until we have all changes or LIMIT
         is reached.

         There is some pool fun going on due to the fact that we have
         to hold on to the old pool with the history before we can
         get the next history. */
      for (i = 0; i < paths->nelts; i++)
        {
          const char *this_path = APR_ARRAY_IDX (paths, i, const char *);
          svn_fs_root_t *root;
          struct path_info *info = apr_palloc (subpool,
                                               sizeof (struct path_info));

          SVN_ERR (path_history_root (&root, fs, this_path, hist_end,
                                      authz_read_func, authz_read_baton,
                                      subpool));
          info->root = root;
          info->path = this_path;
          info->oldpool = svn_pool_create (subpool);
          info->newpool = svn_pool_create (subpool);
          SVN_ERR (svn_fs_node_history (&info->hist, info->root, info->path,
                                        info->oldpool));
          SVN_ERR (get_history (info, fs,
                                strict_node_history ? FALSE : TRUE,
                                authz_read_func, authz_read_baton,
                                hist_start));
          *((struct path_info **) apr_array_push (histories)) = info;
        }

      /* Loop through all the revisions in the range and add any
         where a path was changed to the array, or if they wanted
         history in reverse order just send it to them right away. */
      for (current = hist_end;
           current >= hist_start && any_histories_left;
           current = next_history_rev (histories))
        {
          svn_boolean_t changed = FALSE;
          svn_pool_clear (sendpool);
          any_histories_left = FALSE;
          for (i = 0; i < histories->nelts; i++)
            {
              struct path_info *info = APR_ARRAY_IDX (histories, i,
                                                      struct path_info *);

              /* Check history for this path in current rev. */
              SVN_ERR (check_history (&changed, info, fs, current,
                                      strict_node_history ? FALSE : TRUE,
                                      authz_read_func, authz_read_baton,
                                      hist_start));
              if (info->hist != NULL)
                any_histories_left = TRUE;
            }

          /* If any of the paths changed in this rev then add or send it. */
          if (changed)
            {
              /* If they wanted it in reverse order we can send it completely
                 streamily right now. */
              if (start > end)
                {
                  SVN_ERR (send_change_rev (current, fs,
                                            discover_changed_paths,
                                            authz_read_func, authz_read_baton,
                                            receiver, receiver_baton,
                                            sendpool));
                  if (limit && ++sent_count > limit)
                    break;
                }
              else
                {
                  /* This array must be allocated in pool -- it will be used
                     in the processing loop later. */
                  if (! revs)
                    revs = apr_array_make (pool, 64, sizeof (svn_revnum_t));

                  /* They wanted it in forward order, so we have to buffer up
                     a list of revs and process it later. */
                  *(svn_revnum_t*) apr_array_push (revs) = current;
                }
            }
        }
    }
  else
    {
      /* They want history for the root path, so every rev has a change. */
      int count = hist_end - hist_start + 1;
      if (limit && count > limit)
        count = limit;
      for (i = 0; i < count; ++i)
        {
          svn_pool_clear (sendpool);
          if (start > end)
            SVN_ERR (send_change_rev (hist_end - i, fs,
                                      discover_changed_paths,
                                      authz_read_func, authz_read_baton,
                                      receiver, receiver_baton, sendpool));
          else
            SVN_ERR (send_change_rev (hist_start + i, fs,
                                      discover_changed_paths,
                                      authz_read_func, authz_read_baton,
                                      receiver, receiver_baton, sendpool));
        }
    }

  svn_pool_destroy (subpool);
  if (revs)
    {
      /* Work loop for processing the revisions we found since they wanted
         history in forward order. */
      for (i = 0; i < revs->nelts; ++i)
        {
          svn_pool_clear (sendpool);
          SVN_ERR (send_change_rev (APR_ARRAY_IDX (revs, revs->nelts - i - 1,
                                                   svn_revnum_t),
                                    fs, discover_changed_paths,
                                    authz_read_func, authz_read_baton,
                                    receiver, receiver_baton, sendpool));
          if (limit && i >= limit)
            break;
        }
    }

  svn_pool_destroy (sendpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos_get_logs2 (svn_repos_t *repos,
                     const apr_array_header_t *paths,
                     svn_revnum_t start,
                     svn_revnum_t end,
                     svn_boolean_t discover_changed_paths,
                     svn_boolean_t strict_node_history,
                     svn_repos_authz_func_t authz_read_func,
                     void *authz_read_baton,
                     svn_log_message_receiver_t receiver,
                     void *receiver_baton,
                     apr_pool_t *pool)
{
  return svn_repos_get_logs3 (repos, paths, start, end, 0,
                              discover_changed_paths, strict_node_history,
                              authz_read_func, authz_read_baton, receiver,
                              receiver_baton, pool);
}


/* The 1.0 version of the function.  ### Remove in 2.0. */
svn_error_t *
svn_repos_get_logs (svn_repos_t *repos,
                    const apr_array_header_t *paths,
                    svn_revnum_t start,
                    svn_revnum_t end,
                    svn_boolean_t discover_changed_paths,
                    svn_boolean_t strict_node_history,
                    svn_log_message_receiver_t receiver,
                    void *receiver_baton,
                    apr_pool_t *pool)
{
  return svn_repos_get_logs3 (repos, paths, start, end, 0,
                              discover_changed_paths, strict_node_history,
                              NULL, NULL, /* no authz stuff */
                              receiver, receiver_baton, pool);
}
