#
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
#
import unittest, weakref, setup_path
import os, tempfile, gc
import svn.core, svn.client, libsvn.core
from svn.core import *
from libsvn.core import application_pool, GenericSWIGWrapper
import utils

# Test case for the new automatic pool management infrastructure

class PoolTestCase(unittest.TestCase):

  def assertNotNone(self, value):
    """Assert that the specified value is not None"""
    return self.assertNotEqual(value, None)

  def assertNone(self, value):
    """Assert that the specified value is None"""
    return self.assertEqual(value, None)

  def test_object_struct_members(self):
    """Check that object struct members work correctly"""

    # Test good object assignment operations
    client_ctx = svn.client.svn_client_create_context()
    auth = svn.core.svn_auth_open([])
    client_ctx.auth_baton = auth

    # Check that parent pools are set correctly on struct accesses
    self.assertEqual(client_ctx.auth_baton._parent_pool, auth._parent_pool)

    # Test bad object assignment operations
    def test_bad_assignment(self):
      head_revision = svn.core.svn_opt_revision_t()
      head_revision.kind = auth
    self.assertRaises(TypeError, test_bad_assignment)

  def test_object_hash_struct_members(self):
    """Check that struct members which are hashes of objects work correctly"""

    cfg = svn.core.svn_config_create(False)
    client_ctx = svn.client.svn_client_create_context()
    category = svn.core.SVN_CONFIG_CATEGORY_SERVERS
    client_ctx.config = { category: cfg }

    # Check that parent pools are set correctly
    self.assertEqual(client_ctx.config[category]._parent_pool,
      cfg._parent_pool)

    # Test invalid assignment
    def test_bad_assignment(self):
      client_ctx.config = 42
    self.assertRaises(TypeError, test_bad_assignment)

  def test_assert_valid(self):
    """Test assert_valid method on proxy objects"""

    # Test assert_valid with destroy()
    client_ctx = svn.client.svn_client_create_context()
    auth = svn.core.svn_auth_open([])
    wrapped_auth = GenericSWIGWrapper(auth, auth._parent_pool)
    client_ctx.auth_baton = auth
    auth.assert_valid()
    wrapped_auth.assert_valid()
    client_ctx.auth_baton.assert_valid()
    auth._parent_pool.destroy()
    self.assertRaises(AssertionError, lambda: auth.assert_valid())
    self.assertRaises(AssertionError, lambda: wrapped_auth.assert_valid())
    self.assertRaises(AssertionError, lambda: client_ctx.auth_baton)

    # Test assert_valid with clear()
    client_ctx = svn.client.svn_client_create_context()
    auth = svn.core.svn_auth_open([])
    wrapped_auth = GenericSWIGWrapper(auth, auth._parent_pool)
    client_ctx.auth_baton = auth
    auth.assert_valid()
    wrapped_auth.assert_valid()
    client_ctx.auth_baton.assert_valid()
    auth._parent_pool.clear()
    self.assertRaises(AssertionError, lambda: auth.assert_valid())
    self.assertRaises(AssertionError, lambda: wrapped_auth.assert_valid())
    self.assertRaises(AssertionError, lambda: client_ctx.auth_baton)

  def test_integer_struct_members(self):
    """Check that integer struct members work correctly"""

    # Test good integer assignment operations
    rev = svn.core.svn_opt_revision_t()
    rev.kind = svn.core.svn_opt_revision_number
    rev.value.number = 10
    self.assertEqual(rev.kind, svn.core.svn_opt_revision_number)
    self.assertEqual(rev.value.number, 10)

    # Test bad integer assignment operations
    def test_bad_assignment(self):
      client_ctx = svn.client.svn_client_create_context()
      client_ctx.config = 2
    self.assertRaises(TypeError, test_bad_assignment)

  def test_pool(self):
    # Create pools
    parent_pool = Pool()
    parent_pool_ref = weakref.ref(parent_pool)
    pool = Pool(Pool(parent_pool))
    pool = Pool(pool)

    # Make sure proper exceptions are raised with incorrect input
    self.assertRaises(TypeError, lambda: Pool("abcd"))

    # Check that garbage collection is working OK
    self.assertNotNone(parent_pool_ref())
    top_pool_ref = weakref.ref(parent_pool._parent_pool)
    del parent_pool
    self.assertNotNone(parent_pool_ref())
    self.assertNotNone(top_pool_ref())
    pool.clear()
    newpool = libsvn.core.svn_pool_create(pool)
    libsvn.core.apr_pool_destroy(newpool)
    self.assertNotNone(newpool)
    pool.clear()
    self.assertNotNone(parent_pool_ref())
    del pool
    self.assertNotNone(parent_pool_ref())
    del newpool
    self.assertNone(parent_pool_ref())
    self.assertNone(top_pool_ref())

    # Make sure anonymous pools are destroyed properly
    anonymous_pool_ref = weakref.ref(Pool())
    self.assertNone(anonymous_pool_ref())

  def test_compatibility_layer(self):
    # Create a new pool
    pool = Pool()
    parent_pool_ref = weakref.ref(pool)
    pool = svn_pool_create(Pool(pool))
    pool_ref = weakref.ref(pool)

    # Make sure proper exceptions are raised with incorrect input
    self.assertRaises(TypeError, lambda: svn_pool_create("abcd"))

    # Test whether pools are destroyed properly
    pool = svn_pool_create(pool)
    self.assertNotNone(pool_ref())
    self.assertNotNone(parent_pool_ref())
    del pool
    self.assertNone(pool_ref())
    self.assertNone(parent_pool_ref())

    # Ensure that AssertionErrors are raised when a pool is deleted twice
    newpool = Pool()
    newpool2 = Pool(newpool)
    svn_pool_clear(newpool)
    self.assertRaises(AssertionError, lambda: libsvn.core.apr_pool_destroy(newpool2))
    self.assertRaises(AssertionError, lambda: svn_pool_destroy(newpool2))
    svn_pool_destroy(newpool)
    self.assertRaises(AssertionError, lambda: svn_pool_destroy(newpool))

    # Try to allocate memory from a destroyed pool
    self.assertRaises(AssertionError, lambda: svn_pool_create(newpool))

    # Create and destroy a pool
    svn_pool_destroy(svn_pool_create())

    # Make sure anonymous pools are destroyed properly
    anonymous_pool_ref = weakref.ref(svn_pool_create())
    self.assertNone(anonymous_pool_ref())

    # Try to cause a segfault using apr_terminate
    svn.core.apr_terminate()
    svn.core.apr_initialize()
    svn.core.apr_terminate()
    svn.core.apr_terminate()

    # Destroy the application pool
    svn_pool_destroy(libsvn.core.application_pool)

    # Double check that the application pool has been deleted
    self.assertNone(libsvn.core.application_pool)

    # Try to allocate memory from the old application pool
    self.assertRaises(AssertionError, lambda: svn_pool_create(application_pool))

    # Bring the application pool back to life
    svn_pool_create()

    # Double check that the application pool has been created
    self.assertNotNone(libsvn.core.application_pool)

    # We can still destroy and create pools at will
    svn_pool_destroy(svn_pool_create())

  def _test_pools_in_circular_reference(self, finalizer=False):

    class Circular(object):

      def __init__(self, pool):
        self.pool = pool
        self.loop = None

      if finalizer:
        def __del__(self):
          self.pool = self.loop = None

    def create_circularl():
      pool = Pool(libsvn.core.application_pool)
      subpool1 = Pool(pool)
      subpool2 = Pool(pool)
      circularly1 = Circular(pool)
      circularly2 = Circular(subpool2)
      circularly3 = Circular(subpool1)
      circularly1.loop = circularly3
      circularly2.loop = circularly1
      circularly3.loop = circularly2
      refs = weakref.WeakValueDictionary()
      refs['pool'] = pool
      refs['subpool1'] = subpool1
      refs['subpool2'] = subpool2
      return refs

    refs = create_circularl()
    self.assertEqual({'pool', 'subpool1', 'subpool2'},
                     set(name for name, pool in refs.items()
                              if pool is not None))
    gc.collect()
    self.assertEqual(set(), set(name for name, pool in refs.items()
                                     if pool is not None))

  def test_pools_in_circular_reference_without_finalizer(self):
    self._test_pools_in_circular_reference(finalizer=False)

  @unittest.skipIf(not utils.IS_PY3,
                   "Python 2 cannot collect garbage which involves circular "
                   "references with finalizer")
  def test_pools_in_circular_reference_with_finalizer(self):
    self._test_pools_in_circular_reference(finalizer=True)

def suite():
  return unittest.defaultTestLoader.loadTestsFromTestCase(PoolTestCase)

if __name__ == '__main__':
  runner = unittest.TextTestRunner()
  runner.run(suite())
