/*
 * server_init.c :   parse server configuration file
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by CollabNet (http://www.Collab.Net)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of CollabNet.
 */


#include <apr_hash.h>
#include <apr_dso.h>
#include "svn_svr.h"
#include "svn_parse.h"


/* 
   svn_svr_load_plugin() : 
         Utility to load & register a server plugin into a policy

   Input:    * a policy in which to register the plugin
             * pathname of the shared library to load
             * name of the initialization routine in the plugin
            
   Returns:  error structure or SVN_NO_ERROR

   ASSUMES that apr_dso_init() has already been called!

*/

svn_error_t *
svn_svr_load_plugin (svn_svr_policies_t *policy,
                     const svn_string_t *path,
                     const svn_string_t *init_routine)
{
  apr_dso_handle_t *library;
  apr_dso_handle_sym_t initfunc;
  apr_status_t result;
  svn_error_t *error;

  /* Load the plugin */
  result = apr_dso_load (&library, path->data, policy->pool);

  if (result != APR_SUCCESS)
    {
      char *msg =
        apr_psprintf (policy->pool,
                     "svn_svr_load_plugin(): can't load DSO %s", path->data); 
      return svn_error_create (result, NULL, NULL, policy->pool, msg);
    }
  

  /* Find the plugin's initialization routine. */
  
  result = apr_dso_sym (&initfunc, library, init_routine->data);

  if (result != APR_SUCCESS)
    {
      char *msg =
        apr_psprintf (policy->pool,
                     "svn_svr_load_plugin(): can't find symbol %s",
                      init_routine->data); 
      return svn_error_create (result, NULL, NULL, policy->pool, msg);
    }

  /* Call the plugin's initialization routine.  

     This causes the plugin to call svn_svr_register_plugin(), the end
     result of which is a new plugin structure safely nestled within
     our policy structure.  */

  error = (*initfunc) (policy, library);

  if (error)
    {
      return svn_error_quick_wrap
        (error, "svn_svr_load_plugin(): plugin initialization failed.");
    }
  
  return SVN_NO_ERROR;
}





/*  svn__svr_load_all_plugins :  NOT EXPORTED

    Loops through hash of plugins, loads each using APR's DSO
    routines.  Each plugin ultimately registers (appends) itself into
    the policy structure.  */

svn_error_t *
svn__svr_load_all_plugins (apr_hash_t *plugins, svn_svr_policies_t *policy)
{
  apr_hash_index_t *hash_index;
  void *key, *val;
  size_t keylen;
  svn_error_t *err;
  
  /* Initialize the APR DSO mechanism*/
  apr_status_t result = apr_dso_init();

  if (result != APR_SUCCESS)
    {
      char *msg = "svr__load_plugins(): fatal: can't apr_dso_init() ";
      return (svn_error_create (result, NULL, NULL, policy->pool, msg));
    }

  /* Loop through the hash of plugins from configdata */

  for (hash_index = apr_hash_first (plugins);    /* get first hash entry */
       hash_index;                              /* NULL if out of entries */
       hash_index = apr_hash_next (hash_index))  /* get next hash entry */
    {
      svn_string_t keystring, *valstring;

      apr_hash_this (hash_index, &key, &keylen, &val);

      keystring.data = key;
      keystring.len = keylen;
      keystring.blocksize = keylen;
      valstring = val;

      err = svn_svr_load_plugin (policy, &keystring, val);
      if (err)
        return 
          svn_error_quick_wrap 
          (err, "svn__svr_load_all_plugins(): a plugin failed to load.");
    }
  
  return SVN_NO_ERROR;
}





/* 
   svn_svr_init()   -- create a new, empty "policy" structure

   Input:  ptr to policy ptr, pool
    
   Returns: alloc's empty policy structure, 
            returns svn_error_t * or SVN_NO_ERROR

*/

svn_error_t *
svn_svr_init (svn_svr_policies_t **policy, 
              apr_pool_t *pool)
{
  apr_status_t result;

  /* First, allocate a `policy' structure and all of its internal
     lists */

  *policy = 
    (svn_svr_policies_t *) apr_palloc (pool, sizeof(svn_svr_policies_t));

  *policy->repos_aliases = apr_make_hash (pool);
  *policy->global_restrictions = apr_make_hash (pool);
  *policy->plugins = apr_make_hash (pool);

  /* Set brain-dead warning handler as default */

  *policy->warning = svn_handle_warning;
  *policy->data = NULL;

  /* A policy structure has its own private memory pool, a sub-pool of
     the pool passed in.  */
  *policy->pool = svn_pool_create (pool);

  return SVN_NO_ERROR;
}



svn_error_t *
svn_svr_load_policy (svn_svr_policies_t *policy, 
                     const char *filename)
{
  apr_hash_t *configdata;
  svn_error_t *err;

  /* Parse the file, get a hash-of-hashes back */
  err = svn_parse (&configdata, filename, policy->pool);
  if (err)
    return (svn_error_quick_wrap
            (err, "svn_svr_load_policy():  parser failed."));


  /* Ben sez:  we need a debugging system here.  Let's get one quick. (TODO)
     i.e.  
            if (DEBUGLVL >= 2) {  printf...;  svn_uberhash_print(); }
  */
  svn_uberhash_print (configdata, stdout);


  /* Now walk through our Uberhash, filling in the policy as we go. */
  {
    apr_hash_index_t *hash_index;
    void *key, *val;
    size_t keylen;

    for (hash_index = apr_hash_first (configdata); /* get first hash entry */
         hash_index;                              /* NULL if out of entries */
         hash_index = apr_hash_next (hash_index))  /* get next hash entry */
      {
        /* Retrieve key and val from current hash entry */
        apr_hash_this (hash_index, &key, &keylen, &val);

        /* Figure out which `section' of svn.conf we're looking at */

        if (strcmp ((((svn_string_t *) key)->data), "repos_aliases") == 0)
          {
            /* The "val" is a pointer to a hash full of repository
               aliases, alrady as we want them.  Just store this value
               in our policy structure! */

            policy->repos_aliases = (apr_hash_t *) val;
          }

        else if (strcmp ((((svn_string_t *) key)->data), "security") == 0)
          {
            /* The "val" is a pointer to a hash full of security
               commands; again, we just store a pointer to this hash
               in our policy (the commands are interpreted elsewhere) */

            policy->global_restrictions = (apr_hash_t *) val;
          }

        else if (strcmp ((((svn_string_t *) key)->data), "plugins") == 0)
          {
            /* The "val" is a pointer to a hash containing plugin
               libraries to load up.  We'll definitely do that here
               and now! */
            
            svn__svr_load_all_plugins ((apr_hash_t *) val, policy);
          }

        else
          {
            policy->warning 
              (policy->data, 
               "svn_parse():  ignoring unknown section: `%s'",
               ((svn_string_t *) key)->data);
          }
      }    /* for (hash_index...)  */
       
  } /* closing of Uberhash walk-through */
  
  return SVN_NO_ERROR;
}




/* Add a plugin structure to a server policy structure.
   Called by each plugin's init() routine. */

svn_error_t *
svn_svr_register_plugin (svn_svr_policies_t *policy,
                         svn_svr_plugin_t *new_plugin)
{
  apr_hash_set (policy->plugins,         /* the policy's plugin hashtable */
               new_plugin->name->data,  /* key = name of the plugin */
               new_plugin->name->len,   /* length of this name */
               new_plugin);             /* val = ptr to the plugin itself */

  /* Hm... how would this routine fail? :)  */

  return SVN_NO_ERROR;
}








/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
