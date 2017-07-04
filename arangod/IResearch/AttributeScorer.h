////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2017 ArangoDB GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Andrey Abramov
/// @author Vasiliy Nabatchikov
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGOD_IRESEARCH__ATTRIBUTE_SCORER_H
#define ARANGOD_IRESEARCH__ATTRIBUTE_SCORER_H 1

#include "search/scorers.hpp"

NS_BEGIN(arangodb)
NS_BEGIN(transaction)

class Methods; // forward declaration

NS_END // transaction
NS_END // arangodb

NS_BEGIN(arangodb)
NS_BEGIN(iresearch)

////////////////////////////////////////////////////////////////////////////////
/// @brief an iResearch scorer implementation based on jSON attributes of
///        ArangoDB documents
////////////////////////////////////////////////////////////////////////////////

class AttributeScorer: public irs::sort {
 public:
  DECLARE_SORT_TYPE();

  // for use with irs::order::add<T>(...) and default args (static build)
  DECLARE_FACTORY_DEFAULT(arangodb::transaction::Methods& trx, irs::string_ref const& attr);

  enum ValueType { ARRAY, BOOLEAN, NIL, NUMBER, OBJECT, STRING, UNKNOWN, eLast};

  explicit AttributeScorer(arangodb::transaction::Methods& trx, irs::string_ref const& attr);

  void orderNext(ValueType type) noexcept;
  virtual sort::prepared::ptr prepare() const override;

 private:
  std::string _attr;
  size_t _nextOrder;
  size_t _order[ValueType::eLast]; // type precedence order
  arangodb::transaction::Methods& _trx;
};

NS_END // iresearch
NS_END // arangodb

#endif