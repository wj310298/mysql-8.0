/* Copyright (c) 2015, 2017, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA */

#include "my_config.h"

#include <gtest/gtest.h>
#include <stddef.h>
#include <sys/types.h>
#include <algorithm>
#include <typeinfo>
#include <vector>

#include "dd.h"
#include "dd/cache/dictionary_client.h"
#include "dd/cache/element_map.h"
#include "dd/dd.h"
#include "dd/impl/cache/cache_element.h"
#include "dd/impl/cache/free_list.h"
#include "dd/impl/cache/shared_dictionary_cache.h"
#include "dd/impl/cache/storage_adapter.h"
#include "dd/impl/types/charset_impl.h"
#include "dd/impl/types/collation_impl.h"
#include "dd/impl/types/event_impl.h"
#include "dd/impl/types/procedure_impl.h"
#include "dd/impl/types/schema_impl.h"
#include "dd/impl/types/table_impl.h"
#include "dd/impl/types/tablespace_impl.h"
#include "dd/impl/types/view_impl.h"
// Avoid warning about deleting ptr to incomplete type on Win
#include "dd/properties.h"
#include "lex_string.h"
#include "mdl.h"
#include "my_compiler.h"
#include "test_mdl_context_owner.h"
#include "test_utils.h"


namespace dd {
bool operator==(const Weak_object &a, const Weak_object &b)
{
  String_type arep, brep;
  a.debug_print(arep);
  b.debug_print(brep);

  typedef String_type::iterator i_type;
  typedef std::pair<i_type, i_type> mismatch_type;

  bool arep_largest= (arep.size() > brep.size());

  String_type &largest= arep_largest ? arep : brep;
  String_type &smallest= arep_largest ? brep : arep;

  mismatch_type mismatch= std::mismatch(largest.begin(), largest.end(),
                                        smallest.begin());
  if (mismatch.first == largest.end())
  {
    return true;
  }

  String_type largediff= String_type(mismatch.first, largest.end());
  String_type smalldiff= String_type(mismatch.second, smallest.end());
  std::cout << "Debug representation not equal:\n"
            << String_type(largest.begin(), mismatch.first)
            << "\n<<<\n";
  if (arep_largest)
  {
    std::cout << largediff
              << "\n===\n"
              << smalldiff;
  }
  else
  {
    std::cout << smalldiff
              << "\n===\n"
              << largediff;
  }
  std::cout << "\n>>>\n" << std::endl;
  return false;
}

}

using dd_unittest::nullp;

namespace dd_cache_unittest {

class CacheStorageTest: public ::testing::Test, public Test_MDL_context_owner
{
public:
  dd::Schema_impl *mysql;

  void lock_object(const dd::Entity_object &eo)
  {
    MDL_request mdl_request;
    MDL_REQUEST_INIT(&mdl_request, MDL_key::TABLE,
                     MYSQL_SCHEMA_NAME.str, eo.name().c_str(),
                     MDL_EXCLUSIVE, MDL_TRANSACTION);
    EXPECT_FALSE(m_mdl_context.
                 acquire_lock(&mdl_request,
                              thd()->variables.lock_wait_timeout));
  }

protected:
    CacheStorageTest()
      : mysql(NULL)
  {
  }

  static void SetUpTestCase()
  {
    mdl_init();
  }

  static void TearDownTestCase()
  {
    dd::cache::Shared_dictionary_cache::shutdown();
    mdl_destroy();
  }

  virtual void SetUp()
  {
    m_init.SetUp();
    // Mark this as a dd system thread to skip MDL checks/asserts in the dd cache.
    thd()->system_thread= SYSTEM_THREAD_DD_INITIALIZE;
#ifndef DBUG_OFF
    dd::cache::Storage_adapter::s_use_fake_storage= true;
#endif /* !DBUG_OFF */
    dd::cache::Dictionary_client::Auto_releaser releaser(thd()->dd_client());
    mysql= new dd::Schema_impl();
    mysql->set_name("mysql");
    EXPECT_FALSE(thd()->dd_client()->store<dd::Schema>(mysql));
    EXPECT_LT(9999u, mysql->id());
    thd()->dd_client()->commit_modified_objects();

    mdl_locks_unused_locks_low_water= MDL_LOCKS_UNUSED_LOCKS_LOW_WATER_DEFAULT;
    max_write_lock_count= ULONG_MAX;
    m_mdl_context.init(this);
    EXPECT_FALSE(m_mdl_context.has_locks());
  }

  virtual void TearDown()
  {
    /*
      Explicit scope + auto releaser to make sure acquired objects are
      released before teardown of the thd.
    */
    {
      const dd::Schema *acquired_mysql= NULL;
      dd::cache::Dictionary_client::Auto_releaser releaser(thd()->dd_client());
      EXPECT_FALSE(thd()->dd_client()->acquire<dd::Schema>(mysql->id(), &acquired_mysql));
      EXPECT_NE(nullp<const dd::Schema>(), acquired_mysql);
      EXPECT_FALSE(thd()->dd_client()->drop(acquired_mysql));
      thd()->dd_client()->commit_modified_objects();
    }
    delete mysql;
    m_mdl_context.release_transactional_locks();
    m_mdl_context.destroy();
#ifndef DBUG_OFF
    dd::cache::Storage_adapter::s_use_fake_storage= false;
#endif /* !DBUG_OFF */
    m_init.TearDown();
  }

  virtual void notify_shared_lock(MDL_context_owner *in_use,
                                  bool needs_thr_lock_abort)
  {
    in_use->notify_shared_lock(NULL, needs_thr_lock_abort);
  }


  // Return dummy thd.
  THD *thd()
  {
    return m_init.thd();
  }

  my_testing::Server_initializer m_init; // Server initializer.

  MDL_context        m_mdl_context;
  MDL_request        m_request;

private:
  GTEST_DISALLOW_COPY_AND_ASSIGN_(CacheStorageTest);
};

template <typename T>
class CacheTest: public ::testing::Test
{
protected:
  virtual void SetUp()
  { }

  virtual void TearDown()
  { }

};

typedef ::testing::Types
<
  dd::Charset_impl,
  dd::Collation_impl,
  dd::Schema_impl,
  dd::Table_impl,
  dd::Tablespace_impl,
  dd::View_impl,
  dd::Event_impl,
  dd::Procedure_impl
> DDTypes;
TYPED_TEST_CASE(CacheTest, DDTypes);

// Helper class to create objects and wrap them in elements.
class CacheTestHelper
{
public:
  template <typename T>
  static
  std::vector<dd::cache::Cache_element<typename T::cache_partition_type>*>
    *create_elements(uint num)
  {
    char name[2]= {'a', '\0'};
    std::vector<dd::cache::Cache_element<typename T::cache_partition_type>*>
      *objects= new std::vector<
        dd::cache::Cache_element<typename T::cache_partition_type>*>();
    for (uint id= 1; id <= num; id++, name[0]++)
    {
      T *object= new T();
      object->set_id(id);
      object->set_name(dd::String_type(name));
      dd::cache::Cache_element<typename T::cache_partition_type> *element=
        new dd::cache::Cache_element<typename T::cache_partition_type>();
      element->set_object(object);
      element->recreate_keys();
      objects->push_back(element);
    }
    return objects;
  }

  template <typename T>
  static
  void delete_elements(std::vector<dd::cache::Cache_element
                       <typename T::cache_partition_type>*> *objects)
  {
    // Delete the objects and elements.
    for (typename std::vector<dd::cache::Cache_element
           <typename T::cache_partition_type>*>::iterator it=
             objects->begin();
         it != objects->end();)
    {
      // Careful about iterator invalidation.
      dd::cache::Cache_element
           <typename T::cache_partition_type> *element= *it;
      it++;
      delete(element->object());
      delete(element);
    }

    // Delete the vector with the elements pointer.
    objects->clear();
  }
};

// Test the free list.
TYPED_TEST(CacheTest, FreeList)
{
  // Create a free list and assert that it is empty.
  dd::cache::Free_list<dd::cache::Cache_element
    <typename TypeParam::cache_partition_type> > free_list;
  ASSERT_EQ(0U, free_list.length());

  // Create objects and wrap them in elements, add to vector of pointers.
  std::vector<dd::cache::Cache_element
    <typename TypeParam::cache_partition_type>*>
      *objects= CacheTestHelper::create_elements<TypeParam>(4);

  // Add the elements to the free list.
  for (typename std::vector<dd::cache::Cache_element
         <typename TypeParam::cache_partition_type>*>::iterator it=
           objects->begin();
       it != objects->end(); ++it)
    free_list.add_last(*it);

  // Now, length should be 4.
  ASSERT_EQ(4U, free_list.length());

  // Retrieving the least recently used element should return the first one.
  dd::cache::Cache_element<typename TypeParam::cache_partition_type>
          *element= free_list.get_lru();
  free_list.remove(element);
  ASSERT_EQ(3U, free_list.length());
  ASSERT_EQ(1U,    element->object()->id());
  ASSERT_EQ(dd::String_type("a"), element->object()->name());

  // Now let us remove the middle of the remaining elements.
  free_list.remove(objects->at(2));
  ASSERT_EQ(2U, free_list.length());

  // Get the two last elements and verify they are what we expect.
  element= free_list.get_lru();
  free_list.remove(element);
  ASSERT_EQ(1U, free_list.length());
  ASSERT_EQ(2U,    element->object()->id());
  ASSERT_EQ(dd::String_type("b"), element->object()->name());

  element= free_list.get_lru();
  free_list.remove(element);
  ASSERT_EQ(0U, free_list.length());
  ASSERT_EQ(4U,    element->object()->id());
  ASSERT_EQ(dd::String_type("d"), element->object()->name());

  // Cleanup.
  CacheTestHelper::delete_elements<TypeParam>(objects);
  delete objects;
}

// Test the element map. Use a template function to do this for each
// of the key types, since we have already used the template support in
// gtest to handle the different object types.
template <typename T, typename K>
void element_map_test()
{
  // Create an element map and assert that it is empty.
  dd::cache::Element_map<K, dd::cache::Cache_element
                              <typename T::cache_partition_type> >
    element_map;
  ASSERT_EQ(0U, element_map.size());

  // Create objects and wrap them in elements.
  std::vector<dd::cache::Cache_element
    <typename T::cache_partition_type>*>
      *objects= CacheTestHelper::create_elements<T>(4);

  // Add the elements to the map.
  for (typename std::vector<dd::cache::Cache_element
         <typename T::cache_partition_type>*>::iterator it=
           objects->begin();
       it != objects->end(); ++it)
  {
    // Template disambiguator necessary.
    const K *key= (*it)->template get_key<K>();
    if (key)
      element_map.put(*key, *it);
  }

  // Now, the map should contain 4 elements.
  ASSERT_EQ(4U, element_map.size());

  // For each of the elements, make sure they are present, and that we
  // get the same element in return.
  // Add the elements to the map.
  for (typename std::vector<dd::cache::Cache_element
         <typename T::cache_partition_type>*>::iterator it=
           objects->begin();
       it != objects->end(); ++it)
  {
    // Template disambiguator necessary.
    const K *key= (*it)->template get_key<K>();
    if (key)
    {
      ASSERT_TRUE(element_map.is_present(*key));
    }
  }

  // Remove an element, and make sure the key

  // Delete the array and the objects.
  CacheTestHelper::delete_elements<T>(objects);
  delete objects;
}

TYPED_TEST(CacheTest, Element_map_reverse)
{
  element_map_test<TypeParam,
                   const typename TypeParam::cache_partition_type*>();
}

TYPED_TEST(CacheTest, Element_map_id_key)
{
  element_map_test<TypeParam, typename TypeParam::id_key_type>();
}

TYPED_TEST(CacheTest, Element_map_name_key)
{
  element_map_test<TypeParam, typename TypeParam::name_key_type>();
}

TYPED_TEST(CacheTest, Element_map_aux_key)
{
  // The aux key behavior is not uniform, and this test is therefore omitted.
}


#ifndef DBUG_OFF
template <typename Intrfc_type, typename Impl_type>
void test_basic_store_and_get(CacheStorageTest *tst, THD *thd)
{
  dd::cache::Dictionary_client *dc= thd->dd_client();
  dd::cache::Dictionary_client::Auto_releaser releaser(dc);

  std::unique_ptr<Impl_type> created(new Impl_type());
  Intrfc_type *icreated= created.get();
  dd_unittest::set_attributes(created.get(), "global_test_object");

  tst->lock_object(*created.get());
  EXPECT_FALSE(dc->store(icreated));
  EXPECT_LT(9999u, icreated->id());

  // Acquire by id.
  const Intrfc_type *acquired= NULL;
  EXPECT_FALSE(dc->acquire<Intrfc_type>(icreated->id(), &acquired));
  EXPECT_NE(nullp<Intrfc_type>(), acquired);
  EXPECT_NE(icreated, acquired);
  EXPECT_EQ(*icreated, *acquired);

  const Intrfc_type *name_acquired= NULL;
  EXPECT_FALSE(dc->acquire(icreated->name(), &name_acquired));
  EXPECT_NE(nullp<Intrfc_type>(), name_acquired);
  EXPECT_EQ(acquired, name_acquired);

  EXPECT_FALSE(dc->drop(acquired));
  dc->commit_modified_objects();
}

TEST_F(CacheStorageTest, BasicStoreAndGetCharset)
{
  test_basic_store_and_get<dd::Charset, dd::Charset_impl>(this, thd());
}

TEST_F(CacheStorageTest, BasicStoreAndGetCollation)
{
  test_basic_store_and_get<dd::Collation, dd::Collation_impl>(this, thd());
}

TEST_F(CacheStorageTest, BasicStoreAndGetSchema)
{
  test_basic_store_and_get<dd::Schema, dd::Schema_impl>(this, thd());
}

TEST_F(CacheStorageTest, BasicStoreAndGetTablespace)
{
  test_basic_store_and_get<dd::Tablespace, dd::Tablespace_impl>(this, thd());
}


template <typename Intrfc_type, typename Impl_type>
void test_basic_store_and_get_with_schema(CacheStorageTest *tst, THD *thd)
{
  dd::cache::Dictionary_client *dc= thd->dd_client();
  dd::cache::Dictionary_client::Auto_releaser releaser(dc);

  std::unique_ptr<Impl_type> created(new Impl_type());
  Intrfc_type *icreated= created.get();
  created->set_schema_id(tst->mysql->id());
  dd_unittest::set_attributes(created.get(), "schema_qualified_test_object",
                              *tst->mysql);

  tst->lock_object(*created.get());
  EXPECT_FALSE(dc->store(icreated));
  EXPECT_LT(9999u, icreated->id());

  // Acquire by id.
  const Intrfc_type *acquired= NULL;
  EXPECT_FALSE(thd->dd_client()->acquire<Intrfc_type>(icreated->id(),
                                                      &acquired));
  EXPECT_NE(nullp<Intrfc_type>(), acquired);
  EXPECT_NE(icreated, acquired);
  EXPECT_EQ(*icreated, *acquired);

  // Acquire by schema-qualified name.
  const Intrfc_type *name_acquired= NULL;
  EXPECT_FALSE(dc->acquire(tst->mysql->name(), icreated->name(),
                           &name_acquired));
  EXPECT_NE(nullp<Intrfc_type>(), name_acquired);
  EXPECT_EQ(acquired, name_acquired);

  EXPECT_FALSE(dc->drop(acquired));
  dc->commit_modified_objects();
}

TEST_F(CacheStorageTest, BasicStoreAndGetTable)
{
  test_basic_store_and_get_with_schema<dd::Table, dd::Table_impl>(this, thd());
}

TEST_F(CacheStorageTest, BasicStoreAndGetView)
{
  test_basic_store_and_get_with_schema<dd::View, dd::View_impl>(this, thd());
}

TEST_F(CacheStorageTest, BasicStoreAndGetEvent)
{
  test_basic_store_and_get_with_schema<dd::Event, dd::Event_impl>(this, thd());
}

TEST_F(CacheStorageTest, BasicStoreAndGetRoutine)
{
  test_basic_store_and_get_with_schema<dd::Procedure, dd::Procedure_impl>(this, thd());
}


template <typename Intrfc_type, typename Impl_type>
void test_acquire_for_modification(CacheStorageTest *tst, THD *thd)
{
  dd::cache::Dictionary_client *dc= thd->dd_client();
  dd::cache::Dictionary_client::Auto_releaser releaser(dc);

  std::unique_ptr<Impl_type> created(new Impl_type());
  Intrfc_type *icreated= created.get();
  dd_unittest::set_attributes(created.get(), "global_test_object");

  tst->lock_object(*created.get());
  EXPECT_FALSE(dc->store(icreated));
  EXPECT_LT(9999u, icreated->id());

  const Intrfc_type *acquired= NULL;
  EXPECT_FALSE(dc->acquire<Intrfc_type>(icreated->id(), &acquired));

  // Acquire by id.
  Intrfc_type *modified= NULL;
  {
    dd::cache::Dictionary_client::Auto_releaser releaser2(dc);

    EXPECT_FALSE(dc->acquire_for_modification<Intrfc_type>(icreated->id(),
                                                           &modified));
    EXPECT_NE(nullp<Intrfc_type>(), modified);

    // acquire should return the same object
    EXPECT_FALSE(dc->acquire<Intrfc_type>(icreated->id(), &acquired));
    EXPECT_NE(nullp<Intrfc_type>(), acquired);
    EXPECT_NE(modified, acquired);
    EXPECT_EQ(*modified, *acquired);
  }
  // acquire should still return the same object
  EXPECT_FALSE(dc->acquire<Intrfc_type>(icreated->id(), &acquired));
  EXPECT_NE(nullp<Intrfc_type>(), acquired);
  EXPECT_NE(modified, acquired);
  EXPECT_EQ(*modified, *acquired);

  dc->commit_modified_objects();
  EXPECT_FALSE(dc->acquire<Intrfc_type>(icreated->id(), &acquired));
  EXPECT_FALSE(dc->drop(acquired));
  dc->commit_modified_objects();
}

TEST_F(CacheStorageTest, AquireForModificationCharset)
{
  test_acquire_for_modification<dd::Charset, dd::Charset_impl>(this, thd());
}

TEST_F(CacheStorageTest, AquireForModificationCollation)
{
  test_acquire_for_modification<dd::Collation, dd::Collation_impl>(this, thd());
}

TEST_F(CacheStorageTest, AquireForModificationSchema)
{
  test_acquire_for_modification<dd::Schema, dd::Schema_impl>(this, thd());
}

TEST_F(CacheStorageTest, AquireForModificationTablespace)
{
  test_acquire_for_modification<dd::Tablespace, dd::Tablespace_impl>(this, thd());
}


template <typename Intrfc_type, typename Impl_type>
void test_acquire_for_modification_with_schema(CacheStorageTest *tst, THD *thd)
{
  dd::cache::Dictionary_client *dc= thd->dd_client();
  dd::cache::Dictionary_client::Auto_releaser releaser(dc);

  std::unique_ptr<Impl_type> created(new Impl_type());
  Intrfc_type *icreated= created.get();
  created->set_schema_id(tst->mysql->id());
  dd_unittest::set_attributes(created.get(), "schema_qualified_test_object",
                              *tst->mysql);

  tst->lock_object(*created.get());
  EXPECT_FALSE(dc->store(icreated));
  EXPECT_LT(9999u, icreated->id());

  const Intrfc_type *acquired= NULL;
  EXPECT_FALSE(dc->acquire<Intrfc_type>(icreated->id(), &acquired));

  // Acquire by id.
  Intrfc_type *modified= NULL;
  {
    dd::cache::Dictionary_client::Auto_releaser releaser2(dc);

    EXPECT_FALSE(dc->acquire_for_modification<Intrfc_type>(icreated->id(),
                                                           &modified));
    EXPECT_NE(nullp<Intrfc_type>(), modified);

    // acquire should return the same object
    EXPECT_FALSE(dc->acquire<Intrfc_type>(icreated->id(), &acquired));
    EXPECT_NE(nullp<Intrfc_type>(), acquired);
    EXPECT_NE(modified, acquired);
    EXPECT_EQ(*modified, *acquired);
  }
  // acquire should still return the same object
  EXPECT_FALSE(dc->acquire<Intrfc_type>(icreated->id(), &acquired));
  EXPECT_NE(nullp<Intrfc_type>(), acquired);
  EXPECT_NE(modified, acquired);
  EXPECT_EQ(*modified, *acquired);

  dc->commit_modified_objects();
  EXPECT_FALSE(dc->acquire<Intrfc_type>(icreated->id(), &acquired));
  EXPECT_FALSE(dc->drop(acquired));
  dc->commit_modified_objects();
}

TEST_F(CacheStorageTest, AcquireForModificationTable)
{
  test_acquire_for_modification_with_schema<dd::Table, dd::Table_impl>(this, thd());
}

TEST_F(CacheStorageTest, AcquireForModificationView)
{
  test_acquire_for_modification_with_schema<dd::View, dd::View_impl>(this, thd());
}

TEST_F(CacheStorageTest, AcquireForModificationEvent)
{
  test_acquire_for_modification_with_schema<dd::Event, dd::Event_impl>(this, thd());
}

TEST_F(CacheStorageTest, AcquireForModificationProcedure)
{
  test_acquire_for_modification_with_schema<dd::Procedure, dd::Procedure_impl>(this, thd());
}


template <typename Intrfc_type, typename Impl_type>
void test_acquire_and_rename(CacheStorageTest *tst, THD *thd)
{
  dd::cache::Dictionary_client *dc= thd->dd_client();
  dd::cache::Dictionary_client::Auto_releaser releaser(dc);

  std::unique_ptr<Impl_type> created(new Impl_type());
  Intrfc_type *icreated= created.get();
  dd_unittest::set_attributes(created.get(), "old_name");

  tst->lock_object(*created.get());
  EXPECT_FALSE(dc->store(icreated));
  EXPECT_LT(9999u, icreated->id());

  const Intrfc_type *old_const= NULL;
  EXPECT_FALSE(dc->acquire<Intrfc_type>(icreated->id(), &old_const));
  Intrfc_type *old_modified= NULL;
  EXPECT_FALSE(dc->acquire_for_modification<Intrfc_type>(icreated->id(),
                                                         &old_modified));

  dd_unittest::set_attributes(old_modified, "new_name");
  dc->update(old_modified);

  // Should be possible to acquire with the new name.
  Intrfc_type *new_modified= NULL;
  EXPECT_FALSE(dc->acquire_for_modification<Intrfc_type>(old_modified->name(),
                                                         &new_modified));
  EXPECT_NE(nullp<Intrfc_type>(), new_modified);
  EXPECT_NE(new_modified, old_modified);
  EXPECT_EQ(*new_modified, *old_modified);

  const Intrfc_type *new_object= NULL;
  EXPECT_FALSE(dc->acquire<Intrfc_type>(old_modified->name(), &new_object));
  EXPECT_NE(nullp<Intrfc_type>(), new_object);
  EXPECT_EQ(new_object, old_modified); // equal due to update() above.
  EXPECT_EQ(*new_object, *old_modified);

  // But not by the old object name.
  EXPECT_FALSE(dc->acquire_for_modification<Intrfc_type>("old_name",
                                                         &old_modified));
  EXPECT_EQ(nullp<Intrfc_type>(), old_modified);

  EXPECT_FALSE(dc->acquire<Intrfc_type>("old_name", &old_const));
  EXPECT_EQ(nullp<Intrfc_type>(), old_const);

  dc->commit_modified_objects();
  EXPECT_FALSE(dc->acquire<Intrfc_type>(icreated->id(), &new_object));
  EXPECT_FALSE(dc->drop(new_object));
  dc->commit_modified_objects();
}

TEST_F(CacheStorageTest, AquireAndRenameCharset)
{
  test_acquire_and_rename<dd::Charset, dd::Charset_impl>(this, thd());
}

TEST_F(CacheStorageTest, AquireAndRenameCollation)
{
  test_acquire_and_rename<dd::Collation, dd::Collation_impl>(this, thd());
}

TEST_F(CacheStorageTest, AquireAndRenameSchema)
{
  test_acquire_and_rename<dd::Schema, dd::Schema_impl>(this, thd());
}

TEST_F(CacheStorageTest, AquireAndRenameTablespace)
{
  test_acquire_and_rename<dd::Tablespace, dd::Tablespace_impl>(this, thd());
}


template <typename Intrfc_type, typename Impl_type>
void test_acquire_and_rename_with_schema(CacheStorageTest *tst, THD *thd)
{
  dd::cache::Dictionary_client *dc= thd->dd_client();
  dd::cache::Dictionary_client::Auto_releaser releaser(dc);

  std::unique_ptr<Impl_type> created(new Impl_type());
  Intrfc_type *icreated= created.get();
  created->set_schema_id(tst->mysql->id());
  dd_unittest::set_attributes(created.get(), "schema_old_name",
                              *tst->mysql);

  tst->lock_object(*created.get());
  EXPECT_FALSE(dc->store(icreated));
  EXPECT_LT(9999u, icreated->id());

  const Intrfc_type *old_const= NULL;
  EXPECT_FALSE(dc->acquire(icreated->id(), &old_const));
  Intrfc_type *old_modified= NULL;
  EXPECT_FALSE(dc->acquire_for_modification(icreated->id(), &old_modified));

  dd_unittest::set_attributes(old_modified, "schema_new_name",
                              *tst->mysql);
  dc->update(old_modified);

  // Should be possible to acquire with the new name.
  Intrfc_type *new_modified= NULL;
  EXPECT_FALSE(dc->acquire_for_modification(tst->mysql->name(),
                                            old_modified->name(),
                                            &new_modified));
  EXPECT_NE(nullp<Intrfc_type>(), new_modified);
  EXPECT_NE(new_modified, old_modified);
  EXPECT_EQ(*new_modified, *old_modified);

  const Intrfc_type *new_object= NULL;
  EXPECT_FALSE(dc->acquire(tst->mysql->name(), old_modified->name(),
                           &new_object));
  EXPECT_NE(nullp<Intrfc_type>(), new_object);
  EXPECT_EQ(new_object, old_modified);
  EXPECT_EQ(*new_object, *old_modified);

  // But not by the old object name.
  EXPECT_FALSE(dc->acquire_for_modification(tst->mysql->name(),
                                            "schema_old_name",
                                            &old_modified));
  EXPECT_EQ(nullp<Intrfc_type>(), old_modified);

  EXPECT_FALSE(dc->acquire(tst->mysql->name(), "schema_old_name", &old_const));
  EXPECT_EQ(nullp<Intrfc_type>(), old_const);

  dc->commit_modified_objects();
  EXPECT_FALSE(dc->acquire<Intrfc_type>(icreated->id(), &new_object));
  EXPECT_FALSE(dc->drop(new_object));
  dc->commit_modified_objects();
}

TEST_F(CacheStorageTest, AcquireAndRenameTable)
{
  test_acquire_and_rename_with_schema<dd::Table, dd::Table_impl>(this, thd());
}

TEST_F(CacheStorageTest, AcquireAndRenameView)
{
  test_acquire_and_rename_with_schema<dd::View, dd::View_impl>(this, thd());
}

TEST_F(CacheStorageTest, AcquireAndRenameEvent)
{
  test_acquire_and_rename_with_schema<dd::Event, dd::Event_impl>(this, thd());
}

TEST_F(CacheStorageTest, AcquireAndRenameProcedure)
{
  test_acquire_and_rename_with_schema<dd::Procedure, dd::Procedure_impl>(this, thd());
}


template <typename Intrfc_type, typename Impl_type>
void test_acquire_and_move(CacheStorageTest *tst, THD *thd)
{
  dd::cache::Dictionary_client *dc= thd->dd_client();
  dd::cache::Dictionary_client::Auto_releaser releaser(dc);

  std::unique_ptr<Impl_type> created(new Impl_type());
  Intrfc_type *icreated= created.get();
  created->set_schema_id(tst->mysql->id());
  dd_unittest::set_attributes(created.get(), "schema_name",
                              *tst->mysql);

  tst->lock_object(*created.get());
  EXPECT_FALSE(dc->store(icreated));
  EXPECT_LT(9999u, icreated->id());

  dd::Schema_impl *new_schema= new dd::Schema_impl();
  new_schema->set_name("schema1");
  EXPECT_FALSE(dc->store<dd::Schema>(new_schema));
  EXPECT_LT(9999u, new_schema->id());

  const Intrfc_type *old_const= NULL;
  EXPECT_FALSE(dc->acquire(icreated->id(), &old_const));
  Intrfc_type *old_modified= NULL;
  EXPECT_FALSE(dc->acquire_for_modification(icreated->id(), &old_modified));

  // Move object to a new schema, but keep object name.
  dd_unittest::set_attributes(old_modified, created->name(),
                              *new_schema);
  dc->update(old_modified);

  // Should be possible to acquire in the new schema.
  Intrfc_type *new_modified= NULL;
  EXPECT_FALSE(dc->acquire_for_modification(new_schema->name(),
                                            created->name(),
                                            &new_modified));
  EXPECT_NE(nullp<Intrfc_type>(), new_modified);
  EXPECT_NE(new_modified, old_modified);
  EXPECT_EQ(*new_modified, *old_modified);

  const Intrfc_type *new_object= NULL;
  EXPECT_FALSE(dc->acquire(new_schema->name(), old_modified->name(),
                           &new_object));
  EXPECT_NE(nullp<Intrfc_type>(), new_object);
  EXPECT_EQ(new_object, old_modified);
  EXPECT_EQ(*new_object, *old_modified);

  // But not in the old schema.
  EXPECT_FALSE(dc->acquire_for_modification(tst->mysql->name(),
                                            created->name(),
                                            &old_modified));
  EXPECT_EQ(nullp<Intrfc_type>(), old_modified);

  EXPECT_FALSE(dc->acquire(tst->mysql->name(), created->name(), &old_const));
  EXPECT_EQ(nullp<Intrfc_type>(), old_const);

  dc->commit_modified_objects();
  EXPECT_FALSE(dc->acquire<Intrfc_type>(icreated->id(), &new_object));
  EXPECT_FALSE(dc->drop(new_object));

  // Cleanup: Acquire and delete the new schema.
  const dd::Schema *s= nullptr;
  EXPECT_FALSE(dc->acquire(new_schema->id(), &s));
  EXPECT_FALSE(dc->drop(s));
  dc->commit_modified_objects();
  delete new_schema;
}

TEST_F(CacheStorageTest, AcquireAndMoveTable)
{
  test_acquire_and_move<dd::Table, dd::Table_impl>(this, thd());
}

TEST_F(CacheStorageTest, AcquireAndMoveView)
{
  test_acquire_and_move<dd::View, dd::View_impl>(this, thd());
}

TEST_F(CacheStorageTest, AcquireAndMoveEvent)
{
  test_acquire_and_move<dd::Event, dd::Event_impl>(this, thd());
}

TEST_F(CacheStorageTest, AcquireAndMoveProcedure)
{
  test_acquire_and_move<dd::Procedure, dd::Procedure_impl>(this, thd());
}


template <typename Intrfc_type, typename Impl_type>
void test_acquire_and_drop(CacheStorageTest *tst, THD *thd)
{
  dd::cache::Dictionary_client *dc= thd->dd_client();
  dd::cache::Dictionary_client::Auto_releaser releaser(dc);

  std::unique_ptr<Impl_type> created(new Impl_type());
  Intrfc_type *icreated= created.get();
  dd_unittest::set_attributes(created.get(), "drop_test_object");

  tst->lock_object(*created.get());
  EXPECT_FALSE(dc->store(icreated));
  EXPECT_LT(9999u, icreated->id());

  const Intrfc_type *acquired= NULL;
  EXPECT_FALSE(dc->acquire<Intrfc_type>(icreated->id(), &acquired));

  Intrfc_type *modified= NULL;
  EXPECT_FALSE(dc->acquire_for_modification<Intrfc_type>(icreated->id(),
                                                         &modified));

  EXPECT_FALSE(dc->drop(acquired));

  // Should not be possible to acquire
  EXPECT_FALSE(dc->acquire<Intrfc_type>(icreated->name(), &acquired));
  EXPECT_EQ(nullp<Intrfc_type>(), acquired);
  EXPECT_FALSE(dc->acquire_for_modification<Intrfc_type>(icreated->name(),
                                                        &modified));
  EXPECT_EQ(nullp<Intrfc_type>(), modified);
  dc->commit_modified_objects();
}

TEST_F(CacheStorageTest, AquireAndDropCharset)
{
  test_acquire_and_drop<dd::Charset, dd::Charset_impl>(this, thd());
}

TEST_F(CacheStorageTest, AquireAndDropCollation)
{
  test_acquire_and_drop<dd::Collation, dd::Collation_impl>(this, thd());
}

TEST_F(CacheStorageTest, AquireAndDropSchema)
{
  test_acquire_and_drop<dd::Schema, dd::Schema_impl>(this, thd());
}

TEST_F(CacheStorageTest, AquireAndDropTablespace)
{
  test_acquire_and_drop<dd::Tablespace, dd::Tablespace_impl>(this, thd());
}


template <typename Intrfc_type, typename Impl_type>
void test_acquire_and_drop_with_schema(CacheStorageTest *tst, THD *thd)
{
  dd::cache::Dictionary_client *dc= thd->dd_client();
  dd::cache::Dictionary_client::Auto_releaser releaser(dc);

  std::unique_ptr<Impl_type> created(new Impl_type());
  Intrfc_type *icreated= created.get();
  created->set_schema_id(tst->mysql->id());
  dd_unittest::set_attributes(created.get(), "schema_drop_test_object",
                              *tst->mysql);

  tst->lock_object(*created.get());
  EXPECT_FALSE(dc->store(icreated));
  EXPECT_LT(9999u, icreated->id());

  const Intrfc_type *acquired= NULL;
  EXPECT_FALSE(dc->acquire(icreated->id(), &acquired));

  Intrfc_type *modified= NULL;
  EXPECT_FALSE(dc->acquire_for_modification(icreated->id(), &modified));

  EXPECT_FALSE(dc->drop(acquired));

  // Should not be possible to acquire
  EXPECT_FALSE(dc->acquire(tst->mysql->name(), icreated->name(),
                          &acquired));
  EXPECT_EQ(nullp<Intrfc_type>(), acquired);
  EXPECT_FALSE(dc->acquire_for_modification(tst->mysql->name(),
                                           icreated->name(),
                                           &modified));
  EXPECT_EQ(nullp<Intrfc_type>(), modified);
  dc->commit_modified_objects();
}

TEST_F(CacheStorageTest, AcquireAndDropTable)
{
  test_acquire_and_drop_with_schema<dd::Table, dd::Table_impl>(this, thd());
}

TEST_F(CacheStorageTest, AcquireAndDropView)
{
  test_acquire_and_drop_with_schema<dd::View, dd::View_impl>(this, thd());
}

TEST_F(CacheStorageTest, AcquireAndDropEvent)
{
  test_acquire_and_drop_with_schema<dd::Event, dd::Event_impl>(this, thd());
}

TEST_F(CacheStorageTest, AcquireAndDropProcedure)
{
  test_acquire_and_drop_with_schema<dd::Procedure, dd::Procedure_impl>(this, thd());
}


TEST_F(CacheStorageTest, CommitNewObject)
{
  dd::cache::Dictionary_client *dc= thd()->dd_client();
  dd::cache::Dictionary_client::Auto_releaser releaser(dc);

  dd::Table_impl *created= new dd::Table_impl();
  dd::Table *icreated= created;
  created->set_schema_id(mysql->id());
  dd_unittest::set_attributes(created, "new_object", *mysql);
  lock_object(*created);
  EXPECT_FALSE(dc->store(icreated));
  EXPECT_LT(9999u, icreated->id());

  dc->commit_modified_objects();
  delete created;
}


TEST_F(CacheStorageTest, GetTableBySePrivateId)
{
  dd::cache::Dictionary_client *dc= thd()->dd_client();
  dd::cache::Dictionary_client::Auto_releaser releaser(dc);

  std::unique_ptr<dd::Table> obj(dd::create_object<dd::Table>());
  dd_unittest::set_attributes(obj.get(), "table_object", *mysql);
  obj->set_engine("innodb");
  obj->set_se_private_id(0xEEEE); // Storing some magic number

  dd::Partition *part_obj= obj->add_partition();
  part_obj->set_name("table_part2");
  part_obj->set_level(1);
  part_obj->set_se_private_id(0xAFFF);
  part_obj->set_engine("innodb");
  part_obj->set_number(3);
  part_obj->set_comment("Partition comment");
  part_obj->set_tablespace_id(1);

  lock_object(*obj.get());
  EXPECT_FALSE(dc->store(obj.get()));

  dd::String_type schema_name;
  dd::String_type table_name;

  EXPECT_FALSE(dc->get_table_name_by_se_private_id("innodb", 0xEEEE,
                                                   &schema_name, &table_name));
  EXPECT_EQ(dd::String_type("mysql"), schema_name);
  EXPECT_EQ(obj->name(), table_name);

  // Get table object.
  const dd::Table *tab= NULL;

  EXPECT_FALSE(dc->acquire<dd::Table>(schema_name, table_name, &tab));
  EXPECT_NE(nullp<const dd::Table>(), tab);
  if (tab)
  {
    EXPECT_EQ(tab->name(), table_name);
    EXPECT_EQ(*obj, *tab);

    const dd::Table *obj2= NULL;
    EXPECT_FALSE(dc->acquire<dd::Table>("mysql", obj->name(), &obj2));
    EXPECT_NE(nullp<const dd::Table>(), tab);
    EXPECT_EQ(*obj, *obj2);

    EXPECT_FALSE(dc->drop(obj2));
    dc->commit_modified_objects();
  }
}

TEST_F(CacheStorageTest, TestRename)
{
  dd::cache::Dictionary_client &dc= *thd()->dd_client();
  dd::cache::Dictionary_client::Auto_releaser releaser(&dc);

  std::unique_ptr<dd::Table> temp_table(dd::create_object<dd::Table>());
  dd_unittest::set_attributes(temp_table.get(), "temp_table", *mysql);

  lock_object(*temp_table.get());
  EXPECT_FALSE(dc.store(temp_table.get()));

  {
    // Disable foreign key checks, we need to set this before
    // call to open_tables().
    thd()->variables.option_bits|= OPTION_NO_FOREIGN_KEY_CHECKS;

    dd::cache::Dictionary_client::Auto_releaser releaser(&dc);

    // Get 'test.temp_table' dd object. Schema id for 'mysql' is 1.
    const dd::Schema *sch= NULL;
    EXPECT_FALSE(dc.acquire<dd::Schema>(mysql->id(), &sch));
    EXPECT_NE(nullp<const dd::Schema>(), sch);

    const dd::Table *t= NULL;
    EXPECT_FALSE(dc.acquire<dd::Table>(sch->name(), "temp_table", &t));
    EXPECT_NE(nullp<const dd::Table>(), t);
    if (t)
    {
      dd::Table *temp_table= nullptr;
      EXPECT_FALSE(dc.acquire_for_modification(t->id(), &temp_table));

      temp_table->set_name("updated_table_name");

      // Change name of columns and indexes
      for (const dd::Column *c : *temp_table->columns())
        const_cast<dd::Column*>(c)->set_name(c->name() + "_changed");
      for (dd::Index *i : *temp_table->indexes())
        i->set_name(i->name() + "_changed");

      // Store the object.
      lock_object(*temp_table);
      dc.update(temp_table);

      // Enable foreign key checks
      thd()->variables.option_bits&= ~OPTION_NO_FOREIGN_KEY_CHECKS;
    }
    {
      // Get id of original object to be modified
      const dd::Table *temp_table= NULL;
      EXPECT_FALSE(dc.acquire<dd::Table>("mysql", "updated_table_name",
                                         &temp_table));
      EXPECT_NE(nullp<const dd::Table>(), temp_table);
      EXPECT_FALSE(dc.acquire<dd::Table>(sch->name(), "updated_table_name", &t));
      if (t)
      {
        EXPECT_FALSE(dc.drop(t));
      }
    }
    if (t)
    {
      // The old name is not available anymnore.
      const dd::Table *t= NULL;
      EXPECT_FALSE(dc.acquire<dd::Table>(sch->name(), "temp_table", &t));
      EXPECT_EQ(nullp<const dd::Table>(), t);
    }
    dc.commit_modified_objects();
  }
}


TEST_F(CacheStorageTest, TestSchema)
{
  dd::cache::Dictionary_client &dc= *thd()->dd_client();
  dd::cache::Dictionary_client::Auto_releaser releaser(&dc);

  std::unique_ptr<dd::Schema_impl> s(new dd::Schema_impl());
  s->set_name("schema1");
  EXPECT_FALSE(dc.store<dd::Schema>(s.get()));
  EXPECT_LT(9999u, s->id());

  std::unique_ptr<dd::Table_impl> t(new dd::Table_impl());
  t->set_name("table1");
  t->set_schema_id(s->id());
  EXPECT_FALSE(dc.store<dd::Table>(t.get()));
  EXPECT_LT(9999u, t->id());

  s->set_id(-1);
  s->set_name("schema2");
  EXPECT_FALSE(dc.store<dd::Schema>(s.get()));
  EXPECT_LT(9999u, s->id());

  {
    // Get Schema object for "schema1" and "schema2".
    const dd::Schema *s1= NULL;
    const dd::Schema *s2= NULL;

    EXPECT_FALSE(dc.acquire<dd::Schema>("schema1", &s1));
    EXPECT_FALSE(dc.acquire<dd::Schema>("schema2", &s2));

    if (s1 && s2)
    {
      MDL_REQUEST_INIT(&m_request, MDL_key::TABLE,
                       "schema1", "table1",
                       MDL_EXCLUSIVE,
                       MDL_TRANSACTION);

      m_mdl_context.acquire_lock(&m_request,
                                 thd()->variables.lock_wait_timeout);


      // Get "schema1.table1" table from cache.
      const dd::Table *s1_t1= NULL;
      EXPECT_FALSE(dc.acquire<dd::Table>("schema1", "table1", &s1_t1));
      EXPECT_NE(nullp<const dd::Table>(), s1_t1);

      // Try to get "schema2.table1"(non existing) table.
      const dd::Table *s2_t1= NULL;
      EXPECT_FALSE(dc.acquire<dd::Table>("schema2", "table1", &s2_t1));
      EXPECT_EQ(nullp<const dd::Table>(), s2_t1);

      EXPECT_FALSE(dc.drop(s1_t1));
      EXPECT_FALSE(dc.drop(s2));
      EXPECT_FALSE(dc.drop(s1));
    }
  }
  dc.commit_modified_objects();
}


// Testing Transaction::max_se_private_id() method.
// Tables
TEST_F(CacheStorageTest, TestTransactionMaxSePrivateId)
{
  dd::cache::Dictionary_client &dc= *thd()->dd_client();
  dd::cache::Dictionary_client::Auto_releaser releaser(&dc);

  std::unique_ptr<dd::Table> tab1(dd::create_object<dd::Table>());
  std::unique_ptr<dd::Table> tab2(dd::create_object<dd::Table>());
  std::unique_ptr<dd::Table> tab3(dd::create_object<dd::Table>());

  dd_unittest::set_attributes(tab1.get(), "table1", *mysql);
  tab1->set_se_private_id(5);
  tab1->set_engine("innodb");
  lock_object(*tab1.get());
  dc.store(tab1.get());

  dd_unittest::set_attributes(tab2.get(), "table2", *mysql);
  tab2->set_se_private_id(10);
  tab2->set_engine("innodb");
  lock_object(*tab2.get());
  EXPECT_FALSE(dc.store(tab2.get()));

  dd_unittest::set_attributes(tab3.get(), "table3", *mysql);
  tab3->set_se_private_id(20);
  tab3->set_engine("unknown");
  lock_object(*tab3.get());
  dc.store(tab3.get());

  // Needs working dd::get_dictionary()
  //dd::Object_id max_id;
  //EXPECT_FALSE(dc.get_tables_max_se_private_id("innodb", &max_id));
  //EXPECT_EQ(10u, max_id);
  //EXPECT_FALSE(dc.get_tables_max_se_private_id("unknown", &max_id));
  //EXPECT_EQ(20u, max_id);

  dd::Table *tab1_new= NULL;
  EXPECT_FALSE(dc.acquire_uncached_table_by_se_private_id("innodb", 5, &tab1_new));
  EXPECT_NE(nullp<dd::Table>(), tab1_new);

  dd::Table *tab2_new= NULL;
  EXPECT_FALSE(dc.acquire_uncached_table_by_se_private_id("innodb", 10, &tab2_new));
  EXPECT_NE(nullp<dd::Table>(), tab2_new);

  dd::Table *tab3_new= NULL;
  EXPECT_FALSE(dc.acquire_uncached_table_by_se_private_id("unknown", 20, &tab3_new));
  EXPECT_NE(nullp<dd::Table>(), tab3_new);

  // Drop the objects
  const dd::Table *tab1_new_c= NULL;
  const dd::Table *tab2_new_c= NULL;
  const dd::Table *tab3_new_c= NULL;
  EXPECT_FALSE(dc.acquire<dd::Table>("mysql", "table1", &tab1_new_c));
  EXPECT_FALSE(dc.acquire<dd::Table>("mysql", "table2", &tab2_new_c));
  EXPECT_FALSE(dc.acquire<dd::Table>("mysql", "table3", &tab3_new_c));
  EXPECT_FALSE(dc.drop(tab1_new_c));
  EXPECT_FALSE(dc.drop(tab2_new_c));
  EXPECT_FALSE(dc.drop(tab3_new_c));
  dc.commit_modified_objects();
}


/*
  The following test cases have been commented out since they test
  Dictionary_client member functions which bypass Storage_adapter and
  access the dictionary tables directly.
 */

// Test fetching multiple objects.
// Dictionary_client::fetch_catalog_components()
// Needs working Dictionary_impl::instance()
// TEST_F(CacheStorageTest, TestFetchCatalogComponents)
// {
//   dd::cache::Dictionary_client &dc= *thd()->dd_client();
//   dd::cache::Dictionary_client::Auto_releaser releaser(&dc);

//   std::unique_ptr<dd::Iterator<const dd::Schema> > schemas;
//   EXPECT_FALSE(dc.fetch_catalog_components(&schemas));

//   while (true)
//   {
//     const dd::Schema *s= schemas->next();

//     if (!s)
//     {
//       break;
//     }
//   }
// }

//
// Dictionary_client::fetch_schema_components()
//
// TEST_F(CacheStorageTest, TestFetchSchemaComponents)
// {
//   dd::cache::Dictionary_client &dc= *thd()->dd_client();
//   dd::cache::Dictionary_client::Auto_releaser releaser(&dc);

//   const dd::Schema *s= NULL;
//   EXPECT_FALSE(dc.acquire<dd::Schema>("mysql", &s));
//   EXPECT_NE(nullp<dd::Schema>(), s);

//   std::unique_ptr<dd::Iterator<const dd::Table> > tables;
//   EXPECT_FALSE(dc.fetch_schema_components(s, &tables));

//   while (true)
//   {
//     const dd::Table *t= tables->next();

//     if (!t)
//       break;
//   }
// }


//
// Dictionary_client::fetch_global_components()
//
// TEST_F(CacheStorageTest, TestFetchGlobalComponents)
// {
//   dd::cache::Dictionary_client &dc= *thd()->dd_client();
//   dd::cache::Dictionary_client::Auto_releaser releaser(&dc);

//   // Create a new tablespace.
//   dd::Object_id tablespace_id MY_ATTRIBUTE((unused));
//   {
//     std::unique_ptr<dd::Tablespace> obj(dd::create_object<dd::Tablespace>());
//     dd_unittest::set_attributes(obj.get(), "test_tablespace");

//     //lock_object(thd, obj);
//     EXPECT_FALSE(dc.store(obj.get()));
//     tablespace_id= obj->id();
//   }
//   ha_commit_trans(thd(), false, true);

//   // List all tablespaces
//   {
//     std::unique_ptr<dd::Iterator<const dd::Tablespace> > tablespaces;
//     EXPECT_FALSE(dc.fetch_global_components(&tablespaces));

//     while (true)
//     {
//       const dd::Tablespace *t= tablespaces->next();

//       if (!t)
//         break;
//     }
//   }

//   // Drop tablespace
//   {
//     MDL_request mdl_request;
//     MDL_REQUEST_INIT(&mdl_request, MDL_key::TABLESPACE,
//                      "test_tablespace", "",
//                      MDL_EXCLUSIVE,
//                      MDL_TRANSACTION);
//     (void) thd()->mdl_context.acquire_lock(&mdl_request,
//                                          thd()->variables.lock_wait_timeout);
//     const dd::Tablespace *obj= NULL;
//     EXPECT_FALSE(dc.acquire<dd::Tablespace>(tablespace_id, &obj));
//     EXPECT_NE(nullp<const dd::Tablespace>(), obj);
//     EXPECT_FALSE(dc.drop(const_cast<dd::Tablespace*>(obj)));
//   }
//   EXPECT_FALSE(ha_commit_trans(thd(), false, true));
// }


TEST_F(CacheStorageTest, TestCacheLookup)
{
  dd::cache::Dictionary_client &dc= *thd()->dd_client();
  dd::cache::Dictionary_client::Auto_releaser releaser(&dc);

  dd::String_type obj_name= dd::Table::OBJECT_TABLE().name() +
    dd::String_type("_cacheissue");
  dd::Object_id id;
  //
  // Create table object
  //
  {
    std::unique_ptr<dd::Table> obj(dd::create_object<dd::Table>());
    dd_unittest::set_attributes(obj.get(), obj_name, *mysql);

    obj->set_engine("innodb");
    obj->set_se_private_id(0xFFFA); // Storing some magic number

    lock_object(*obj.get());
    EXPECT_FALSE(dc.store(obj.get()));
  }

  //
  // Step 1:
  // Get Table object given se_private_id = 0xFFFA
  // This should release the object reference in cache.
  //
  {
    dd::String_type sch_name, tab_name;
    EXPECT_FALSE(dc.get_table_name_by_se_private_id("innodb",
                                                    0xFFFA,
                                                    &sch_name,
                                                    &tab_name));
    EXPECT_LT(0u, sch_name.size());
    EXPECT_LT(0u, tab_name.size());

    const dd::Table *obj= NULL;
    EXPECT_FALSE(dc.acquire<dd::Table>(sch_name, tab_name, &obj));
    EXPECT_NE(nullp<const dd::Table>(), obj);
    id= obj->id();
  }

  //
  // Step 2:
  // Get Table object given id and drop it.
  // This should remove object from the cache and delete it.
  //

  //
  // Get from cache and then drop it.
  //
  {
    const dd::Table *obj= NULL;
    EXPECT_FALSE(dc.acquire<dd::Table>(id, &obj));
    EXPECT_NE(nullp<const dd::Table>(), obj);
    EXPECT_FALSE(dc.drop(obj));
  }

  //
  // Step 3:
  // Again, try to get Table object with se_private_id = 0xFFFA
  // This should not get object reference in cache which was stored
  // by Step 1. Make sure we get no object.
  //

  {
    dd::String_type sch_name, tab_name;
    EXPECT_FALSE(dc.get_table_name_by_se_private_id("innodb",
                                                   0XFFFA,
                                                   &sch_name,
                                                   &tab_name));
    EXPECT_EQ(0u, sch_name.size());
    EXPECT_EQ(0u, tab_name.size());
  }
  dc.commit_modified_objects();
}

TEST_F(CacheStorageTest, TestTriggers)
{
  dd::cache::Dictionary_client &dc= *thd()->dd_client();
  dd::cache::Dictionary_client::Auto_releaser releaser(&dc);

  dd::String_type obj_name= dd::Table::OBJECT_TABLE().name() +
    dd::String_type("_trigs");
  dd::Object_id id MY_ATTRIBUTE((unused));

  //
  // Create table object
  //
  {
    std::unique_ptr<dd::Table> obj(dd::create_object<dd::Table>());
    dd_unittest::set_attributes(obj.get(), obj_name, *mysql);

    //
    // Store table trigger information
    //

    dd::Trigger *trig_obj=
      obj->add_trigger(dd::Trigger::enum_action_timing::AT_AFTER,
                       dd::Trigger::enum_event_type::ET_INSERT);
    trig_obj->set_name(obj->name() + "trigger1");
    trig_obj->set_definer("definer_username", "definer_hostname");
    trig_obj->set_client_collation_id(1);
    trig_obj->set_connection_collation_id(1);
    trig_obj->set_schema_collation_id(1);

    dd::Trigger *trig_obj2=
      obj->add_trigger_preceding(trig_obj,
                                 dd::Trigger::enum_action_timing::AT_AFTER,
                                 dd::Trigger::enum_event_type::ET_INSERT);
    trig_obj2->set_name(obj->name() + "trigger2");
    trig_obj2->set_definer("definer_username", "definer_hostname");
    trig_obj2->set_client_collation_id(1);
    trig_obj2->set_connection_collation_id(1);
    trig_obj2->set_schema_collation_id(1);

    dd::Trigger *trig_obj3= obj->add_trigger_following(trig_obj2,
                                                       dd::Trigger::enum_action_timing::AT_AFTER,
                                                       dd::Trigger::enum_event_type::ET_INSERT);
    trig_obj3->set_name(obj->name() + "trigger3");
    trig_obj3->set_definer("definer_username", "definer_hostname");
    trig_obj3->set_client_collation_id(1);
    trig_obj3->set_connection_collation_id(1);
    trig_obj3->set_schema_collation_id(1);

    dd::Trigger *trig_obj1=
      obj->add_trigger(dd::Trigger::enum_action_timing::AT_BEFORE,
                       dd::Trigger::enum_event_type::ET_UPDATE);
    trig_obj1->set_name("newtrigger1");
    trig_obj1->set_definer("definer_username", "definer_hostname");
    trig_obj1->set_client_collation_id(1);
    trig_obj1->set_connection_collation_id(1);
    trig_obj1->set_schema_collation_id(1);

    lock_object(*obj.get());
    EXPECT_FALSE(dc.store(obj.get()));
    id= obj->id();
  }


  //
  // Test iterator.
  //

  {
    const dd::Table *obj= nullptr;
    EXPECT_FALSE(dc.acquire<dd::Table>(id, &obj));
    EXPECT_NE(nullp<const dd::Table>(), obj);

    // Validate number of triggers expected.
    const dd::Table::Trigger_collection &tc= obj->triggers();
    dd::Table::Trigger_collection::const_iterator it= tc.begin();
    EXPECT_TRUE((*it)->name() == "newtrigger0"); ++it;
    EXPECT_TRUE((*it)->name() == "tables_trigstrigger2"); ++it;
    EXPECT_TRUE((*it)->name() == "tables_trigstrigger3"); ++it;
    EXPECT_TRUE((*it)->name() == "tables_trigstrigger1"); ++it;
    EXPECT_TRUE((*it)->name() == "newtrigger1"); ++it;
    EXPECT_TRUE(it == tc.end());

    EXPECT_FALSE(dc.drop(const_cast<dd::Table*>(obj)));
  }
  dc.commit_modified_objects();

}
#endif /* !DBUG_OFF */
}
