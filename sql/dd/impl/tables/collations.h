/* Copyright (c) 2014, 2017, Oracle and/or its affiliates. All rights reserved.

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

#ifndef DD_TABLES__COLLATIONS_INCLUDED
#define DD_TABLES__COLLATIONS_INCLUDED

#include <string>

#include "dd/impl/types/dictionary_object_table_impl.h"

class THD;

namespace dd {
class Global_name_key;
class Dictionary_object;
class Raw_record;

namespace tables {

///////////////////////////////////////////////////////////////////////////

class Collations : public Dictionary_object_table_impl
{
public:
  static const Collations &instance();

  static const String_type &table_name()
  {
    static String_type s_table_name("collations");
    return s_table_name;
  }
public:
  enum enum_fields
  {
    FIELD_ID,
    FIELD_NAME,
    FIELD_CHARACTER_SET_ID,
    FIELD_IS_COMPILED,
    FIELD_SORT_LENGTH,
    FIELD_PAD_ATTRIBUTE
  };

public:
  Collations();

  virtual bool populate(THD *thd) const;

  virtual const String_type &name() const
  { return Collations::table_name(); }

  virtual Dictionary_object *create_dictionary_object(const Raw_record &) const;

public:
  static bool update_object_key(Global_name_key *key,
                                const String_type &collation_name);
};

///////////////////////////////////////////////////////////////////////////

}
}

#endif // DD_TABLES__COLLATIONS_INCLUDED
